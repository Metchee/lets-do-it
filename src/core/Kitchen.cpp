#include "core/Kitchen.hpp"
#include "utils/Logger.hpp"
#include "utils/Exception.hpp"
#include <unistd.h>
#include <algorithm>

Kitchen::Kitchen(int id, int numCooks, double multiplier, int restockTime)
    : _id(id), _numCooks(numCooks), _multiplier(multiplier), _restockTime(restockTime),
      _active(false), _activeCooks(0), _pendingPizzas(0) {
    
    _threadPool = std::make_unique<ThreadPool>(numCooks);
    initializeIngredients();
}

Kitchen::~Kitchen() {
    if (_active) {
        stop();
    }
}

bool Kitchen::canAcceptPizza() const {
    int totalLoad = static_cast<int>(_pendingPizzas) + static_cast<int>(_activeCooks);
    return totalLoad < (2 * _numCooks);
}

bool Kitchen::addPizza(const SerializedPizza& pizza) {
    (void)pizza;
    updateLastActivity();
    return canAcceptPizza();
}

void Kitchen::start() {
    _active = true;
    _lastActivityTimer.start();
}

void Kitchen::stop() {
    if (!_active) {
        return;
    }
    
    _active = false;
    
    if (_threadPool) {
        _threadPool->stop();
    }
    
    if (_restockThread.joinable()) {
        _restockThread.join();
    }
    
    if (_communicationThread.joinable()) {
        _communicationThread.join();
    }
    
    if (_ipc) {
        _ipc->close();
    }
}

bool Kitchen::isActive() const {
    return _active;
}

KitchenStatus Kitchen::getStatus() const {
    ScopedLock queueLock(const_cast<Mutex&>(_queueMutex));
    ScopedLock ingredientLock(const_cast<Mutex&>(_ingredientMutex));
    
    KitchenStatus status(_id, static_cast<int>(_activeCooks), _numCooks, 
                        _pizzaQueue.size(), 2 * _numCooks);
    
    status.ingredients.clear();
    for (int i = 1; i <= 256; i *= 2) {
        auto it = _ingredients.find(static_cast<Ingredient>(i));
        if (it != _ingredients.end()) {
            status.ingredients.push_back(it->second);
        } else {
            status.ingredients.push_back(0);
        }
    }
    
    return status;
}

int Kitchen::getId() const {
    return _id;
}

void Kitchen::updateLastActivity() {
    _lastActivityTimer.reset();
    _lastActivityTimer.start();
}

bool Kitchen::shouldClose() const {
    if (_activeCooks > 0) {
        return false;
    }
    
    {
        ScopedLock lock(const_cast<Mutex&>(_queueMutex));
        if (!_pizzaQueue.empty()) {
            return false;
        }
    }
    
    return _lastActivityTimer.isRunning() && _lastActivityTimer.getElapsedSeconds() > 30.0;
}

void Kitchen::setIPC(std::unique_ptr<PipeIPC> ipc) {
    _ipc = std::move(ipc);
}

void Kitchen::runAsChildProcess() {
    try {
        initializeKitchenProcess();
        startRestockThread();
        runMainProcessLoop();
        cleanupKitchenProcess();
        
    } catch (const std::exception& e) {
        LOG_ERROR("Kitchen " + std::to_string(_id) + " error: " + e.what());
    }
}

void Kitchen::initializeKitchenProcess() {
    _active = true;
    _lastActivityTimer.start();
    initializeIngredients();
}

void Kitchen::startRestockThread() {
    try {
        _restockThread = std::thread(&Kitchen::restockLoop, this);
    } catch (const std::exception& e) {
        LOG_ERROR("Kitchen " + std::to_string(_id) + " failed to start restock thread: " + e.what());
    }
}

void Kitchen::runMainProcessLoop() {
    int loopCount = 0;
    
    while (_active && loopCount < 10000) {
        loopCount++;
        
        bool receivedSomething = processIncomingMessages();
        processPizzaQueue();
        sendPeriodicStatus(loopCount);
        
        if (!receivedSomething && shouldClose()) {
            break;
        }
        
        Timer::sleep(receivedSomething ? 10 : 100);
    }
}

bool Kitchen::processIncomingMessages() {
    bool receivedSomething = false;
    
    if (_ipc && _ipc->isReady()) {
        try {
            std::string message = _ipc->receive();
            if (!message.empty()) {
                if (handlePizzaMessage(message) || handleStatusMessage(message)) {
                    receivedSomething = true;
                    updateLastActivity();
                }
            }
        } catch (const std::exception& e) {
        }
    }
    
    return receivedSomething;
}

bool Kitchen::handlePizzaMessage(const std::string& message) {
    if (message.substr(0, 6) == "PIZZA:") {
        try {
            SerializedPizza pizza;
            pizza.unpack(message.substr(6));
            
            {
                ScopedLock lock(_queueMutex);
                _pizzaQueue.push(pizza);
            }
            
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("Kitchen " + std::to_string(_id) + " failed to process pizza: " + e.what());
        }
    }
    return false;
}

bool Kitchen::handleStatusMessage(const std::string& message) {
    if (message == "STATUS_REQUEST") {
        try {
            KitchenStatus status = getStatus();
            std::string statusMsg = "STATUS:" + status.pack();
            return _ipc->send(statusMsg);
        } catch (const std::exception& e) {
            LOG_ERROR("Kitchen " + std::to_string(_id) + " failed to send status: " + e.what());
        }
    }
    return false;
}

void Kitchen::processPizzaQueue() {
    while (static_cast<int>(_activeCooks) < _numCooks) {
        SerializedPizza nextPizza;
        bool hasPizza = false;
        
        {
            ScopedLock lock(_queueMutex);
            if (!_pizzaQueue.empty()) {
                nextPizza = _pizzaQueue.front();
                _pizzaQueue.pop();
                hasPizza = true;
            }
        }
        
        if (hasPizza) {
            std::thread cookingThread([this, nextPizza]() {
                this->cookPizza(nextPizza);
            });
            cookingThread.detach();
        } else {
            break;
        }
    }
}

void Kitchen::sendPeriodicStatus(int loopCount) {
    static int lastStatusSent = 0;
    
    if (loopCount % 100 == 0 && loopCount > lastStatusSent + 50) {
        try {
            if (_ipc && _ipc->isReady()) {
                KitchenStatus status = getStatus();
                std::string statusMsg = "STATUS:" + status.pack();
                _ipc->send(statusMsg);
                lastStatusSent = loopCount;
            }
        } catch (const std::exception& e) {
        }
    }
}

void Kitchen::cleanupKitchenProcess() {
    _active = false;
    
    if (_restockThread.joinable()) {
        _restockThread.join();
    }
    
    if (_ipc) {
        _ipc->close();
    }
}

void Kitchen::cookPizza(const SerializedPizza& pizza) {
    _activeCooks++;
    decrementPendingPizzas();
    updateLastActivity();
    
    if (!hasIngredients(pizza)) {
        removePizzaFromQueue();
        _activeCooks--;
        return;
    }
    
    consumeIngredients(pizza);
    
    Timer::sleep(pizza.cookingTime);
    
    removePizzaFromQueue();
    
    std::string pizzaInfo = PizzaTypeHelper::pizzaTypeToString(pizza.type) + " " +
                           PizzaTypeHelper::pizzaSizeToString(pizza.size);
    
    if (_ipc && _ipc->isReady()) {
        SerializedPizza readyPizza = pizza;
        readyPizza.isCooked = true;
        try {
            std::string completedMsg = "COMPLETED:" + readyPizza.pack();
            _ipc->send(completedMsg);
        } catch (const std::exception& e) {
            LOG_ERROR("Kitchen " + std::to_string(_id) + " IPC error: " + e.what());
        }
    }
    
    _activeCooks--;
    updateLastActivity();
}

void Kitchen::removePizzaFromQueue() {
    ScopedLock lock(_queueMutex);
    if (!_pizzaQueue.empty()) {
        _pizzaQueue.pop();
    }
}

void Kitchen::restockIngredients() {
    ScopedLock lock(_ingredientMutex);
    
    for (auto& ingredient : _ingredients) {
        ingredient.second = std::min(ingredient.second + 1, 10);
    }
}

void Kitchen::communicateWithReception() {
    while (_active) {
        try {
            if (_ipc && _ipc->isReady()) {
                KitchenStatus status = getStatus();
                *_ipc << status;
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Kitchen " + std::to_string(_id) + " communication error: " + e.what());
        }
        
        Timer::sleep(1000);
    }
}

bool Kitchen::hasIngredients(const SerializedPizza& pizza) {
    ScopedLock lock(_ingredientMutex);
    
    std::vector<Ingredient> required = PizzaTypeHelper::getIngredientsForPizza(pizza.type);
    
    for (Ingredient ing : required) {
        auto it = _ingredients.find(ing);
        if (it == _ingredients.end() || it->second <= 0) {
            return false;
        }
    }
    
    return true;
}

void Kitchen::consumeIngredients(const SerializedPizza& pizza) {
    ScopedLock lock(_ingredientMutex);
    
    std::vector<Ingredient> required = PizzaTypeHelper::getIngredientsForPizza(pizza.type);
    
    for (Ingredient ing : required) {
        auto it = _ingredients.find(ing);
        if (it != _ingredients.end()) {
            it->second = std::max(0, it->second - 1);
        }
    }
}

void Kitchen::initializeIngredients() {
    ScopedLock lock(_ingredientMutex);
    
    _ingredients[Dough] = 5;
    _ingredients[Tomato] = 5;
    _ingredients[Gruyere] = 5;
    _ingredients[Ham] = 5;
    _ingredients[Mushrooms] = 5;
    _ingredients[Steak] = 5;
    _ingredients[Eggplant] = 5;
    _ingredients[GoatCheese] = 5;
    _ingredients[ChiefLove] = 5;
}

void Kitchen::restockLoop() {
    while (_active) {
        Timer::sleep(_restockTime);
        if (_active) {
            restockIngredients();
        }
    }
}

void Kitchen::decrementQueueSize() {
}

void Kitchen::incrementPendingPizzas() {
    _pendingPizzas++;
}

void Kitchen::decrementPendingPizzas() {
    if (_pendingPizzas > 0) {
        _pendingPizzas--;
    }
}

int Kitchen::getPendingPizzaCount() const {
    return static_cast<int>(_pendingPizzas);
}