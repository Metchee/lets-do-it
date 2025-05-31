#include "core/KitchenManager.hpp"
#include "utils/Logger.hpp"
#include "utils/Exception.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <iostream>
#include <algorithm>
#include <climits>

KitchenProcess::KitchenProcess(std::unique_ptr<Kitchen> k, std::unique_ptr<PipeIPC> i, pid_t p)
    : kitchen(std::move(k)), ipc(std::move(i)), pid(p), active(true) {}

KitchenManager::KitchenManager(int numCooksPerKitchen, double multiplier, int restockTime)
    : _numCooksPerKitchen(numCooksPerKitchen), _multiplier(multiplier), 
      _restockTime(restockTime), _nextKitchenId(1) {}

KitchenManager::~KitchenManager() {
    cleanup();
}

bool KitchenManager::distributePizza(const SerializedPizza& pizza) {
    ScopedLock lock(_kitchensMutex);
    
    cleanupDeadKitchens();
    checkForCompletedPizzas();
    
    int bestKitchenIndex = findBestKitchen();
    
    if (bestKitchenIndex == -1) {
        createNewKitchen();
        bestKitchenIndex = _kitchens.size() - 1;
    }
    
    return sendPizzaToKitchen(bestKitchenIndex, pizza);
}

bool KitchenManager::sendPizzaToKitchen(int kitchenIndex, const SerializedPizza& pizza) {
    if (kitchenIndex < 0 || kitchenIndex >= static_cast<int>(_kitchens.size())) {
        return false;
    }
    
    auto& kitchenProcess = _kitchens[kitchenIndex];
    
    if (!kitchenProcess->kitchen->canAcceptPizza()) {
        createNewKitchen();
        kitchenIndex = _kitchens.size() - 1;
        if (kitchenIndex < 0 || kitchenIndex >= static_cast<int>(_kitchens.size())) {
            return false;
        }
    }
    
    return sendPizzaViaIPC(_kitchens[kitchenIndex].get(), pizza);
}

bool KitchenManager::sendPizzaViaIPC(KitchenProcess* kitchenProcess, const SerializedPizza& pizza) {
    if (!kitchenProcess->ipc || !kitchenProcess->ipc->isReady()) {
        return false;
    }
    
    try {
        if (kitchenProcess->ipc->send("PIZZA:" + pizza.pack())) {
            kitchenProcess->kitchen->incrementPendingPizzas();
            kitchenProcess->kitchen->updateLastActivity();
            return true;
        } else {
            LOG_ERROR("Failed to send pizza via IPC to kitchen " + 
                     std::to_string(kitchenProcess->kitchen->getId()));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to distribute pizza: " + std::string(e.what()));
    }
    
    return false;
}

void KitchenManager::createNewKitchen() {
    auto kitchen = std::make_unique<Kitchen>(_nextKitchenId++, _numCooksPerKitchen, 
                                           _multiplier, _restockTime);
    auto ipc = std::make_unique<PipeIPC>();
    
    if (!ipc->createPipes()) {
        throw KitchenException("Failed to create IPC pipes for kitchen");
    }
    
    int kitchenId = kitchen->getId();
    forkKitchenProcess(std::move(kitchen), std::move(ipc), kitchenId);
    
    Timer::sleep(100);
}

pid_t KitchenManager::forkKitchenProcess(std::unique_ptr<Kitchen> kitchen, 
                                        std::unique_ptr<PipeIPC> ipc, int kitchenId) {
    pid_t pid = fork();
    
    if (pid == -1) {
        throw KitchenException("Failed to fork kitchen process");
    }
    
    if (pid == 0) {
        setupChildProcess(std::move(kitchen), std::move(ipc), kitchenId);
        exit(0);
    } else {
        setupParentProcess(std::move(kitchen), std::move(ipc), pid);
    }
    
    return pid;
}

void KitchenManager::setupChildProcess(std::unique_ptr<Kitchen> kitchen, 
                                      std::unique_ptr<PipeIPC> ipc, int kitchenId) {
    Logger& logger = Logger::getInstance();
    logger.enableConsoleOutput(false);
    logger.enableFileOutput("kitchen_" + std::to_string(kitchenId) + ".log");
    
    ipc->setupChild();
    kitchen->setIPC(std::move(ipc));
    kitchen->runAsChildProcess();
}

void KitchenManager::setupParentProcess(std::unique_ptr<Kitchen> kitchen, 
                                       std::unique_ptr<PipeIPC> ipc, pid_t pid) {
    ipc->setupParent();
    kitchen->start();
    
    auto kitchenProcess = std::make_unique<KitchenProcess>(
        std::move(kitchen), std::move(ipc), pid);
    
    _kitchens.push_back(std::move(kitchenProcess));
}

void KitchenManager::closeInactiveKitchens() {
    ScopedLock lock(_kitchensMutex);
    
    for (auto it = _kitchens.begin(); it != _kitchens.end();) {
        if (shouldCloseKitchen(*it)) {
            terminateKitchenProcess(*it);
            it = _kitchens.erase(it);
        } else {
            ++it;
        }
    }
}

bool KitchenManager::shouldCloseKitchen(const std::unique_ptr<KitchenProcess>& kitchenProcess) {
    int status;
    pid_t result = waitpid(kitchenProcess->pid, &status, WNOHANG);
    
    if (result == kitchenProcess->pid) {
        return true;
    }
    
    return kitchenProcess->kitchen->shouldClose();
}

void KitchenManager::terminateKitchenProcess(const std::unique_ptr<KitchenProcess>& kitchenProcess) {
    if (kill(kitchenProcess->pid, SIGTERM) == 0) {
        waitForKitchenTermination(kitchenProcess->pid);
    }
}

void KitchenManager::waitForKitchenTermination(pid_t pid) {
    int status;
    
    for (int i = 0; i < 10; ++i) {
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            return;
        }
        usleep(100000);
    }
    
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
}

void KitchenManager::checkForCompletedPizzas() {
    for (const auto& kitchenProcess : _kitchens) {
        if (!isKitchenReady(kitchenProcess.get())) {
            continue;
        }
        
        processKitchenMessages(kitchenProcess.get());
    }
}

bool KitchenManager::isKitchenReady(KitchenProcess* kitchenProcess) const {
    return kitchenProcess->active && 
           kitchenProcess->ipc && 
           kitchenProcess->ipc->isReady();
}

void KitchenManager::processKitchenMessages(KitchenProcess* kitchenProcess) {
    for (int i = 0; i < 20; ++i) {
        std::string message = receiveKitchenMessage(kitchenProcess);
        if (message.empty()) {
            break;
        }
        
        handleKitchenMessage(message, kitchenProcess->kitchen->getId());
    }
}

std::string KitchenManager::receiveKitchenMessage(KitchenProcess* kitchenProcess) const {
    try {
        return kitchenProcess->ipc->receive();
    } catch (const std::exception& e) {
        return "";
    }
}

void KitchenManager::handleKitchenMessage(const std::string& message, int kitchenId) {
    if (message.substr(0, 10) == "COMPLETED:") {
        handleCompletedPizza(message.substr(10), kitchenId);
    }
}

void KitchenManager::handleCompletedPizza(const std::string& pizzaData, int kitchenId) {
    try {
        SerializedPizza completedPizza;
        completedPizza.unpack(pizzaData);
        
        std::string pizzaInfo = PizzaTypeHelper::pizzaTypeToString(completedPizza.type) + " " +
                               PizzaTypeHelper::pizzaSizeToString(completedPizza.size);
        
        std::cout << "🍕 Pizza ready: " << pizzaInfo << " (Kitchen " << kitchenId << ")" << std::endl;
        
        LOG_INFO("Pizza ready: " + pizzaInfo);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to process completed pizza from kitchen " + 
                 std::to_string(kitchenId) + ": " + e.what());
    }
}

void KitchenManager::displayStatus() const {
    ScopedLock lock(const_cast<Mutex&>(_kitchensMutex));
    
    const_cast<KitchenManager*>(this)->checkForCompletedPizzas();
    
    displayStatusHeader();
    
    if (_kitchens.empty()) {
        displayNoKitchensMessage();
        return;
    }
    
    displayAllKitchens();
    displayStatusFooter();
}

void KitchenManager::displayStatusHeader() const {
    std::cout << "\n=== KITCHEN STATUS ===" << std::endl;
    std::cout << "Total kitchens: " << _kitchens.size() << std::endl;
}

void KitchenManager::displayNoKitchensMessage() const {
    std::cout << "No active kitchens" << std::endl;
    std::cout << "=====================" << std::endl;
}

void KitchenManager::displayAllKitchens() const {
    for (const auto& kitchenProcess : _kitchens) {
        if (kitchenProcess->active) {
            displaySingleKitchen(kitchenProcess.get());
        }
    }
}

void KitchenManager::displaySingleKitchen(KitchenProcess* kitchenProcess) const {
    KitchenStatus status = getKitchenStatus(kitchenProcess);
    displayKitchenInfo(status, kitchenProcess->pid);
}

KitchenStatus KitchenManager::getKitchenStatus(KitchenProcess* kitchenProcess) const {
    KitchenStatus status;
    bool gotRealStatus = false;
    
    if (isKitchenReady(kitchenProcess)) {
        gotRealStatus = requestKitchenStatus(kitchenProcess, status);
    }
    
    if (!gotRealStatus) {
        status = createFallbackStatus(kitchenProcess->kitchen->getId());
    }
    
    return status;
}

bool KitchenManager::requestKitchenStatus(KitchenProcess* kitchenProcess, KitchenStatus& status) const {
    try {
        if (!kitchenProcess->ipc->send("STATUS_REQUEST")) {
            return false;
        }
        
        return waitForStatusResponse(kitchenProcess, status);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to get status from kitchen " + 
                 std::to_string(kitchenProcess->kitchen->getId()) + ": " + e.what());
        return false;
    }
}

bool KitchenManager::waitForStatusResponse(KitchenProcess* kitchenProcess, KitchenStatus& status) const {
    for (int i = 0; i < 50; ++i) {
        std::string response = receiveKitchenMessage(kitchenProcess);
        if (!response.empty()) {
            if (response.substr(0, 7) == "STATUS:") {
                status.unpack(response.substr(7));
                return true;
            } else if (response.substr(0, 10) == "COMPLETED:") {
                const_cast<KitchenManager*>(this)->handleCompletedPizza(
                    response.substr(10), kitchenProcess->kitchen->getId());
            }
        }
        Timer::sleep(10);
    }
    
    return false;
}

KitchenStatus KitchenManager::createFallbackStatus(int kitchenId) const {
    KitchenStatus status;
    status.kitchenId = kitchenId;
    status.activeCooks = 0;
    status.totalCooks = _numCooksPerKitchen;
    status.pizzasInQueue = 0;
    status.maxCapacity = 2 * _numCooksPerKitchen;
    status.ingredients = {5, 5, 5, 5, 5, 5, 5, 5, 5};
    return status;
}

void KitchenManager::displayKitchenInfo(const KitchenStatus& status, pid_t pid) const {
    std::cout << "\nKitchen " << status.kitchenId << " (PID: " << pid << "):" << std::endl;
    std::cout << "  Active cooks: " << status.activeCooks << "/" << status.totalCooks << std::endl;
    std::cout << "  Pizzas in queue: " << status.pizzasInQueue << "/" << status.maxCapacity << std::endl;
    displayIngredients(status.ingredients);
}

void KitchenManager::displayIngredients(const std::vector<int>& ingredients) const {
    std::cout << "  Ingredients: ";
    
    const std::vector<std::string> ingredientNames = {
        "Dough", "Tomato", "Gruyere", "Ham", "Mushrooms", 
        "Steak", "Eggplant", "GoatCheese", "ChiefLove"
    };
    
    for (size_t i = 0; i < ingredients.size() && i < ingredientNames.size(); ++i) {
        std::cout << ingredientNames[i] << ":" << ingredients[i] << " ";
    }
    std::cout << std::endl;
}

void KitchenManager::displayStatusFooter() const {
    std::cout << "=====================" << std::endl;
}

std::vector<KitchenStatus> KitchenManager::getAllKitchenStatuses() const {
    ScopedLock lock(const_cast<Mutex&>(_kitchensMutex));
    
    std::vector<KitchenStatus> statuses;
    
    for (const auto& kitchenProcess : _kitchens) {
        if (kitchenProcess->active) {
            statuses.push_back(kitchenProcess->kitchen->getStatus());
        }
    }
    
    return statuses;
}

int KitchenManager::getKitchenCount() const {
    ScopedLock lock(const_cast<Mutex&>(_kitchensMutex));
    return _kitchens.size();
}

void KitchenManager::cleanup() {
    ScopedLock lock(_kitchensMutex);
    
    for (auto& kitchenProcess : _kitchens) {
        if (kitchenProcess->active) {
            terminateKitchenProcess(kitchenProcess);
        }
    }
    
    _kitchens.clear();
}

int KitchenManager::findBestKitchen() const {
    if (_kitchens.empty()) {
        return -1;
    }
    
    int bestIndex = -1;
    int minLoad = INT_MAX;
    
    for (size_t i = 0; i < _kitchens.size(); ++i) {
        const auto& kitchenProcess = _kitchens[i];
        
        if (!kitchenProcess->active || !kitchenProcess->kitchen->canAcceptPizza()) {
            continue;
        }
        
        int load = kitchenProcess->kitchen->getPendingPizzaCount();
        
        if (load < minLoad) {
            minLoad = load;
            bestIndex = static_cast<int>(i);
        }
        
        if (load == 0) {
            break;
        }
    }
    
    return bestIndex;
}

void KitchenManager::cleanupDeadKitchens() {
    for (auto it = _kitchens.begin(); it != _kitchens.end();) {
        auto& kitchenProcess = *it;
        
        int status;
        pid_t result = waitpid(kitchenProcess->pid, &status, WNOHANG);
        
        if (result == kitchenProcess->pid) {
            it = _kitchens.erase(it);
        } else {
            ++it;
        }
    }
}