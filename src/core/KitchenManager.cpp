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
    
    int bestKitchenIndex = findBestKitchen();
    
    if (bestKitchenIndex == -1) {
        createNewKitchen();
        bestKitchenIndex = _kitchens.size() - 1;
    }
    
    if (bestKitchenIndex >= 0 && bestKitchenIndex < static_cast<int>(_kitchens.size())) {
        auto& kitchenProcess = _kitchens[bestKitchenIndex];
        
        if (!kitchenProcess->kitchen->canAcceptPizza()) {
            createNewKitchen();
            bestKitchenIndex = _kitchens.size() - 1;
            if (bestKitchenIndex < 0 || bestKitchenIndex >= static_cast<int>(_kitchens.size())) {
                return false;
            }
        }
        
        auto& finalKitchenProcess = _kitchens[bestKitchenIndex];
        
        if (finalKitchenProcess->ipc && finalKitchenProcess->ipc->isReady()) {
            try {
                bool success = finalKitchenProcess->ipc->send("PIZZA:" + pizza.pack());
                if (success) {
                    finalKitchenProcess->kitchen->incrementPendingPizzas();
                    finalKitchenProcess->kitchen->updateLastActivity();
                    
                    LOG_INFO("Distributed pizza to kitchen " + std::to_string(finalKitchenProcess->kitchen->getId()));
                    return true;
                } else {
                    LOG_ERROR("Failed to send pizza via IPC to kitchen " + std::to_string(finalKitchenProcess->kitchen->getId()));
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to distribute pizza: " + std::string(e.what()));
            }
        }
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
    
    pid_t pid = fork();
    
    if (pid == -1) {
        throw KitchenException("Failed to fork kitchen process");
    }
    
    if (pid == 0) {
        Logger& logger = Logger::getInstance();
        logger.enableConsoleOutput(false);
        logger.enableFileOutput("kitchen_" + std::to_string(kitchenId) + ".log");
        
        ipc->setupChild();
        kitchen->setIPC(std::move(ipc));
        kitchen->runAsChildProcess();
        exit(0);
    } else {
        ipc->setupParent();
        kitchen->start();
        
        auto kitchenProcess = std::make_unique<KitchenProcess>(
            std::move(kitchen), std::move(ipc), pid);
        
        _kitchens.push_back(std::move(kitchenProcess));
        
        LOG_INFO("Created new kitchen " + std::to_string(kitchenId) + 
                 " with PID " + std::to_string(pid));
        
        Timer::sleep(100);
    }
}

void KitchenManager::closeInactiveKitchens() {
    ScopedLock lock(_kitchensMutex);
    
    for (auto it = _kitchens.begin(); it != _kitchens.end();) {
        auto& kitchenProcess = *it;
        
        int status;
        pid_t result = waitpid(kitchenProcess->pid, &status, WNOHANG);
        
        if (result == kitchenProcess->pid) {
            LOG_INFO("Kitchen " + std::to_string(kitchenProcess->kitchen->getId()) + 
                     " process already terminated");
            it = _kitchens.erase(it);
            continue;
        }
        
        if (kitchenProcess->kitchen->shouldClose()) {
            LOG_INFO("Closing inactive kitchen " + 
                     std::to_string(kitchenProcess->kitchen->getId()));
            
            if (kill(kitchenProcess->pid, SIGTERM) == 0) {
                for (int i = 0; i < 10; ++i) {
                    result = waitpid(kitchenProcess->pid, &status, WNOHANG);
                    if (result == kitchenProcess->pid) {
                        break;
                    }
                    usleep(100000);
                }
                
                if (result != kitchenProcess->pid) {
                    kill(kitchenProcess->pid, SIGKILL);
                    waitpid(kitchenProcess->pid, &status, 0);
                }
            }
            
            it = _kitchens.erase(it);
        } else {
            ++it;
        }
    }
}

void KitchenManager::displayStatus() const {
    ScopedLock lock(const_cast<Mutex&>(_kitchensMutex));
    
    std::cout << "\n=== KITCHEN STATUS ===" << std::endl;
    std::cout << "Total kitchens: " << _kitchens.size() << std::endl;
    
    if (_kitchens.empty()) {
        std::cout << "No active kitchens" << std::endl;
        std::cout << "=====================" << std::endl;
        return;
    }
    
    for (const auto& kitchenProcess : _kitchens) {
        if (!kitchenProcess->active) continue;
        
        KitchenStatus status;
        bool gotRealStatus = false;
        
        if (kitchenProcess->ipc && kitchenProcess->ipc->isReady()) {
            try {
                if (kitchenProcess->ipc->send("STATUS_REQUEST")) {
                    LOG_INFO("Sent STATUS_REQUEST to kitchen " + std::to_string(kitchenProcess->kitchen->getId()));
                    
                    for (int i = 0; i < 100; ++i) {
                        std::string response = kitchenProcess->ipc->receive();
                        if (!response.empty()) {
                            LOG_INFO("Received response from kitchen " + std::to_string(kitchenProcess->kitchen->getId()) + ": " + 
                                    (response.length() > 100 ? response.substr(0, 100) + "..." : response));
                            
                            if (response.substr(0, 7) == "STATUS:") {
                                status.unpack(response.substr(7));
                                gotRealStatus = true;
                                LOG_INFO("Successfully parsed status from kitchen " + std::to_string(kitchenProcess->kitchen->getId()));
                                break;
                            }
                        }
                        Timer::sleep(10);
                    }
                    
                    if (!gotRealStatus) {
                        LOG_WARNING("No response from kitchen " + std::to_string(kitchenProcess->kitchen->getId()) + ", using fallback");
                    }
                } else {
                    LOG_ERROR("Failed to send STATUS_REQUEST to kitchen " + std::to_string(kitchenProcess->kitchen->getId()));
                }
                
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to get real status from kitchen " + 
                         std::to_string(kitchenProcess->kitchen->getId()) + ": " + e.what());
            }
        } else {
            LOG_WARNING("IPC not ready for kitchen " + std::to_string(kitchenProcess->kitchen->getId()));
        }
        
        if (!gotRealStatus) {
            status.kitchenId = kitchenProcess->kitchen->getId();
            status.activeCooks = 0;
            status.totalCooks = _numCooksPerKitchen;
            status.pizzasInQueue = 0;
            status.maxCapacity = 2 * _numCooksPerKitchen;
            status.ingredients = {5, 5, 5, 5, 5, 5, 5, 5, 5};
        }
        
        std::cout << "\nKitchen " << status.kitchenId << " (PID: " << kitchenProcess->pid << "):" << std::endl;
        std::cout << "  Active cooks: " << status.activeCooks << "/" << status.totalCooks << std::endl;
        std::cout << "  Pizzas in queue: " << status.pizzasInQueue << "/" << status.maxCapacity << std::endl;
        std::cout << "  Ingredients: ";
        
        const std::vector<std::string> ingredientNames = {
            "Dough", "Tomato", "Gruyere", "Ham", "Mushrooms", 
            "Steak", "Eggplant", "GoatCheese", "ChiefLove"
        };
        
        for (size_t i = 0; i < status.ingredients.size() && i < ingredientNames.size(); ++i) {
            std::cout << ingredientNames[i] << ":" << status.ingredients[i] << " ";
        }
        std::cout << std::endl;
    }
    
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
            kill(kitchenProcess->pid, SIGTERM);
            
            int status;
            waitpid(kitchenProcess->pid, &status, 0);
            
            LOG_INFO("Cleaned up kitchen " + 
                     std::to_string(kitchenProcess->kitchen->getId()));
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
        
        if (!kitchenProcess->active) {
            continue;
        }
        
        if (!kitchenProcess->kitchen->canAcceptPizza()) {
            LOG_INFO("Kitchen " + std::to_string(kitchenProcess->kitchen->getId()) + " is at capacity");
            continue;
        }
        
        int load = kitchenProcess->kitchen->getPendingPizzaCount();
        
        LOG_INFO("Kitchen " + std::to_string(kitchenProcess->kitchen->getId()) + 
                 " has load: " + std::to_string(load));
        
        if (load < minLoad) {
            minLoad = load;
            bestIndex = static_cast<int>(i);
        }
        
        if (load == 0) {
            break;
        }
    }
    
    if (bestIndex != -1) {
        LOG_INFO("Selected kitchen " + std::to_string(_kitchens[bestIndex]->kitchen->getId()) + 
                 " with load " + std::to_string(minLoad));
    } else {
        LOG_INFO("No kitchen can accept more pizzas");
    }
    
    return bestIndex;
}

void KitchenManager::cleanupDeadKitchens() {
    for (auto it = _kitchens.begin(); it != _kitchens.end();) {
        auto& kitchenProcess = *it;
        
        int status;
        pid_t result = waitpid(kitchenProcess->pid, &status, WNOHANG);
        
        if (result == kitchenProcess->pid) {
            LOG_INFO("Kitchen " + std::to_string(kitchenProcess->kitchen->getId()) + 
                     " process terminated");
            it = _kitchens.erase(it);
        } else {
            ++it;
        }
    }
}