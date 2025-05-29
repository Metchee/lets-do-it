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
    bool canAccept = totalLoad < (2 * _numCooks);
    
    if (!canAccept) {
        LOG_INFO("Kitchen " + std::to_string(_id) + " at capacity: " + 
                 std::to_string(totalLoad) + "/" + std::to_string(2 * _numCooks));
    }
    
    return canAccept;
}

bool Kitchen::addPizza(const SerializedPizza& pizza) {
    (void)pizza;
    updateLastActivity();
    return canAcceptPizza();
}

void Kitchen::start() {
    _active = true;
    _lastActivityTimer.start();
    LOG_INFO("Kitchen " + std::to_string(_id) + " started with " + 
             std::to_string(_numCooks) + " cooks");
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
    
    LOG_INFO("Kitchen " + std::to_string(_id) + " stopped");
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
        LOG_INFO("Kitchen " + std::to_string(_id) + " child process starting");
        
        _active = true;
        _lastActivityTimer.start();
        
        initializeIngredients();
        LOG_INFO("Kitchen " + std::to_string(_id) + " ingredients initialized");
        
        try {
            _restockThread = std::thread(&Kitchen::restockLoop, this);
            LOG_INFO("Kitchen " + std::to_string(_id) + " restock thread started");
        } catch (const std::exception& e) {
            LOG_ERROR("Kitchen " + std::to_string(_id) + " failed to start restock thread: " + e.what());
        }
        
        LOG_INFO("Kitchen " + std::to_string(_id) + " IPC ready: " + (_ipc && _ipc->isReady() ? "YES" : "NO"));
        
        int loopCount = 0;
        int messagesReceived = 0;
        int lastStatusSent = 0;
        std::vector<std::thread> cookingThreads;
        
        while (_active && loopCount < 10000) {
            loopCount++;
            if (loopCount % 50 == 0) {
                LOG_INFO("Kitchen " + std::to_string(_id) + " main loop iteration " + 
                         std::to_string(loopCount) + ", messages received: " + std::to_string(messagesReceived));
            }
            
            bool receivedSomething = false;
            
            if (_ipc && _ipc->isReady()) {
                try {
                    std::string message = _ipc->receive();
                    if (!message.empty()) {
                        messagesReceived++;
                        LOG_INFO("Kitchen " + std::to_string(_id) + " received message #" + 
                                std::to_string(messagesReceived) + ": " + 
                                (message.length() > 50 ? message.substr(0, 50) + "..." : message));
                        
                        if (message.substr(0, 6) == "PIZZA:") {
                            try {
                                SerializedPizza pizza;
                                pizza.unpack(message.substr(6));
                                
                                LOG_INFO("Kitchen " + std::to_string(_id) + " successfully parsed pizza type " + 
                                         std::to_string(static_cast<int>(pizza.type)));
                                
                                {
                                    ScopedLock lock(_queueMutex);
                                    _pizzaQueue.push(pizza);
                                    LOG_INFO("Kitchen " + std::to_string(_id) + " added pizza to queue (size: " + 
                                             std::to_string(_pizzaQueue.size()) + ")");
                                }
                                
                                updateLastActivity();
                                receivedSomething = true;
                                
                            } catch (const std::exception& e) {
                                LOG_ERROR("Kitchen " + std::to_string(_id) + " failed to process pizza: " + e.what());
                            }
                        } else if (message == "STATUS_REQUEST") {
                            try {
                                KitchenStatus status = getStatus();
                                std::string statusMsg = "STATUS:" + status.pack();
                                if (_ipc->send(statusMsg)) {
                                    LOG_INFO("Kitchen " + std::to_string(_id) + " sent status update successfully");
                                } else {
                                    LOG_ERROR("Kitchen " + std::to_string(_id) + " failed to send status update");
                                }
                                updateLastActivity();
                                receivedSomething = true;
                            } catch (const std::exception& e) {
                                LOG_ERROR("Kitchen " + std::to_string(_id) + " failed to send status: " + e.what());
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    if (loopCount % 100 == 0) {
                        LOG_ERROR("Kitchen " + std::to_string(_id) + " IPC receive error: " + e.what());
                    }
                }
            }
            
            while (static_cast<int>(_activeCooks) < _numCooks) {
                SerializedPizza nextPizza;
                bool hasPizza = false;
                
                {
                    ScopedLock lock(_queueMutex);
                    if (!_pizzaQueue.empty()) {
                        nextPizza = _pizzaQueue.front();
                        hasPizza = true;
                    }
                }
                
                if (hasPizza) {
                    cookingThreads.emplace_back([this, nextPizza]() {
                        this->cookPizza(nextPizza);
                    });
                    cookingThreads.back().detach();
                    LOG_INFO("Kitchen " + std::to_string(_id) + " started cooking thread for queued pizza");
                } else {
                    break;
                }
            }
            
            if (loopCount % 100 == 0 && loopCount > lastStatusSent + 50) {
                try {
                    if (_ipc && _ipc->isReady()) {
                        KitchenStatus status = getStatus();
                        std::string statusMsg = "STATUS:" + status.pack();
                        _ipc->send(statusMsg);
                        lastStatusSent = loopCount;
                        LOG_INFO("Kitchen " + std::to_string(_id) + " sent periodic status update");
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("Kitchen " + std::to_string(_id) + " failed to send periodic status: " + e.what());
                }
            }
            
            if (!receivedSomething && shouldClose()) {
                LOG_INFO("Kitchen " + std::to_string(_id) + " closing due to inactivity");
                break;
            }
            
            Timer::sleep(receivedSomething ? 10 : 100);
        }
        
        if (loopCount >= 10000) {
            LOG_WARNING("Kitchen " + std::to_string(_id) + " reached loop limit, exiting");
        }
        
        _active = false;
        LOG_INFO("Kitchen " + std::to_string(_id) + " starting cleanup");
        
        if (_restockThread.joinable()) {
            LOG_INFO("Kitchen " + std::to_string(_id) + " joining restock thread");
            _restockThread.join();
        }
        
        if (_ipc) {
            LOG_INFO("Kitchen " + std::to_string(_id) + " closing IPC");
            _ipc->close();
        }
        
        LOG_INFO("Kitchen " + std::to_string(_id) + " child process exiting cleanly");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Kitchen " + std::to_string(_id) + " error: " + e.what());
    } catch (...) {
        LOG_ERROR("Kitchen " + std::to_string(_id) + " unknown error");
    }
}

void Kitchen::cookPizza(const SerializedPizza& pizza) {
    _activeCooks++;
    decrementPendingPizzas();
    updateLastActivity();
    
    LOG_INFO("Kitchen " + std::to_string(_id) + " cook started preparing pizza type " + 
             std::to_string(static_cast<int>(pizza.type)) + 
             " (active cooks: " + std::to_string(static_cast<int>(_activeCooks)) + ")");
    
    if (!hasIngredients(pizza)) {
        LOG_WARNING("Kitchen " + std::to_string(_id) + " missing ingredients for pizza type " + 
                    std::to_string(static_cast<int>(pizza.type)));
        
        {
            ScopedLock lock(_queueMutex);
            if (!_pizzaQueue.empty()) {
                _pizzaQueue.pop();
                LOG_INFO("Kitchen " + std::to_string(_id) + " removed pizza from queue due to missing ingredients (remaining: " + 
                         std::to_string(_pizzaQueue.size()) + ")");
            }
        }
        
        _activeCooks--;
        return;
    }
    
    consumeIngredients(pizza);
    
    LOG_INFO("Kitchen " + std::to_string(_id) + " cooking pizza type " + 
             std::to_string(static_cast<int>(pizza.type)) + " for " + 
             std::to_string(pizza.cookingTime) + "ms");
    
    Timer::sleep(pizza.cookingTime);
    
    {
        ScopedLock lock(_queueMutex);
        if (!_pizzaQueue.empty()) {
            _pizzaQueue.pop();
            LOG_INFO("Kitchen " + std::to_string(_id) + " removed cooked pizza from queue (remaining: " + 
                     std::to_string(_pizzaQueue.size()) + ")");
        }
    }
    
    LOG_INFO("Kitchen " + std::to_string(_id) + " finished cooking pizza type " + 
             std::to_string(static_cast<int>(pizza.type)) + " - PIZZA READY!");
    
    if (_ipc && _ipc->isReady()) {
        SerializedPizza readyPizza = pizza;
        readyPizza.isCooked = true;
        try {
            *_ipc << readyPizza;
        } catch (const std::exception& e) {
            LOG_ERROR("Kitchen " + std::to_string(_id) + " IPC error: " + e.what());
        }
    }
    
    _activeCooks--;
    updateLastActivity();
    
    LOG_INFO("Kitchen " + std::to_string(_id) + " cook finished (active cooks: " + 
             std::to_string(static_cast<int>(_activeCooks)) + 
             ", pending: " + std::to_string(static_cast<int>(_pendingPizzas)) + ")");
}

void Kitchen::restockIngredients() {
    ScopedLock lock(_ingredientMutex);
    
    for (auto& ingredient : _ingredients) {
        ingredient.second = std::min(ingredient.second + 1, 10);
    }
    
    LOG_DEBUG("Kitchen " + std::to_string(_id) + " restocked ingredients");
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
    LOG_INFO("Kitchen " + std::to_string(_id) + " pending pizzas: " + std::to_string(static_cast<int>(_pendingPizzas)));
}

void Kitchen::decrementPendingPizzas() {
    if (_pendingPizzas > 0) {
        _pendingPizzas--;
        LOG_INFO("Kitchen " + std::to_string(_id) + " pending pizzas: " + std::to_string(static_cast<int>(_pendingPizzas)));
    }
}

int Kitchen::getPendingPizzaCount() const {
    return static_cast<int>(_pendingPizzas);
}