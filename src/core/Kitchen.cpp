#include "core/Kitchen.hpp"
#include "utils/Logger.hpp"
#include "utils/Exception.hpp"
#include <unistd.h>
#include <algorithm>

Kitchen::Kitchen(int id, int numCooks, double multiplier, int restockTime)
    : _id(id), _numCooks(numCooks), _multiplier(multiplier), _restockTime(restockTime),
      _active(false), _activeCooks(0) {
    
    _threadPool = std::make_unique<ThreadPool>(numCooks);
    initializeIngredients();
}

Kitchen::~Kitchen() {
    if (_active) {
        stop();
    }
}

bool Kitchen::canAcceptPizza() const {
    ScopedLock lock(const_cast<Mutex&>(_queueMutex));
    return _pizzaQueue.size() < static_cast<size_t>(2 * _numCooks);
}

bool Kitchen::addPizza(const SerializedPizza& pizza) {
    // This method is used by the parent process to check capacity
    // The actual pizza sending is done via IPC
    if (!canAcceptPizza()) {
        return false;
    }
    
    // In parent process, we simulate adding to queue for capacity tracking
    {
        ScopedLock lock(_queueMutex);
        _pizzaQueue.push(pizza);
    }
    
    updateLastActivity();
    return true;
}

void Kitchen::start() {
    // This is called in parent process - minimal setup
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
    
    // Fill ingredient status
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
    // Don't close if there are pizzas in queue or cooks are active
    if (_activeCooks > 0) {
        return false;
    }
    
    {
        ScopedLock lock(const_cast<Mutex&>(_queueMutex));
        if (!_pizzaQueue.empty()) {
            return false;
        }
    }
    
    // Only close if inactive for more than 10 seconds AND no work to do
    // Increased timeout to give more time for status requests
    return _lastActivityTimer.isRunning() && _lastActivityTimer.getElapsedSeconds() > 10.0;
}

void Kitchen::setIPC(std::unique_ptr<PipeIPC> ipc) {
    _ipc = std::move(ipc);
}

void Kitchen::runAsChildProcess() {
    try {
        LOG_INFO("Kitchen " + std::to_string(_id) + " child process starting");
        
        // Initialize the kitchen in child process
        _active = true;
        _lastActivityTimer.start();
        
        LOG_INFO("Kitchen " + std::to_string(_id) + " basic initialization done");
        
        // Initialize ingredients first
        initializeIngredients();
        LOG_INFO("Kitchen " + std::to_string(_id) + " ingredients initialized");
        
        // Don't use ThreadPool in child process, use simple threads
        LOG_INFO("Kitchen " + std::to_string(_id) + " skipping thread pool creation, using direct threads");
        
        // Start restock thread carefully
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
        
        // Main loop to receive pizzas from parent
        while (_active && loopCount < 10000) { // Safety limit
            loopCount++;
            if (loopCount % 50 == 0) { // Debug log every 5 seconds
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
                                
                                // Add to queue
                                {
                                    ScopedLock lock(_queueMutex);
                                    _pizzaQueue.push(pizza);
                                    LOG_INFO("Kitchen " + std::to_string(_id) + " added pizza to queue (size: " + 
                                             std::to_string(_pizzaQueue.size()) + ")");
                                }
                                
                                // Start a cooking thread directly (limited to _numCooks)
                                if (static_cast<int>(_activeCooks) < _numCooks) {
                                    cookingThreads.emplace_back([this, pizza]() {
                                        this->cookPizza(pizza);
                                    });
                                    cookingThreads.back().detach(); // Detach to avoid memory issues
                                    LOG_INFO("Kitchen " + std::to_string(_id) + " started direct cooking thread");
                                } else {
                                    LOG_INFO("Kitchen " + std::to_string(_id) + " all cooks busy, pizza queued");
                                }
                                
                                updateLastActivity();
                                receivedSomething = true;
                                
                            } catch (const std::exception& e) {
                                LOG_ERROR("Kitchen " + std::to_string(_id) + " failed to process pizza: " + e.what());
                            }
                        } else if (message == "STATUS_REQUEST") {
                            // Parent is requesting status update
                            try {
                                KitchenStatus status = getStatus();
                                std::string statusMsg = "STATUS:" + status.pack();
                                if (_ipc->send(statusMsg)) {
                                    LOG_INFO("Kitchen " + std::to_string(_id) + " sent status update successfully");
                                } else {
                                    LOG_ERROR("Kitchen " + std::to_string(_id) + " failed to send status update");
                                }
                                updateLastActivity(); // Reset inactivity timer when serving status
                                receivedSomething = true;
                            } catch (const std::exception& e) {
                                LOG_ERROR("Kitchen " + std::to_string(_id) + " failed to send status: " + e.what());
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    if (loopCount % 100 == 0) { // Don't spam logs
                        LOG_ERROR("Kitchen " + std::to_string(_id) + " IPC receive error: " + e.what());
                    }
                }
            }
            
            // Send periodic status updates
            if (loopCount % 100 == 0 && loopCount > lastStatusSent + 50) { // Every 10 seconds
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
            
            // Check if should close only if we're not busy
            if (!receivedSomething && shouldClose()) {
                LOG_INFO("Kitchen " + std::to_string(_id) + " closing due to inactivity");
                break;
            }
            
            // Sleep less if we're receiving messages
            Timer::sleep(receivedSomething ? 10 : 100);
        }
        
        if (loopCount >= 10000) {
            LOG_WARNING("Kitchen " + std::to_string(_id) + " reached loop limit, exiting");
        }
        
        // Cleanup
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
    updateLastActivity(); // Update activity when starting to cook
    
    LOG_INFO("Kitchen " + std::to_string(_id) + " cook started preparing pizza type " + 
             std::to_string(static_cast<int>(pizza.type)));
    
    // Check if we have ingredients
    if (!hasIngredients(pizza)) {
        LOG_WARNING("Kitchen " + std::to_string(_id) + " missing ingredients for pizza type " + 
                    std::to_string(static_cast<int>(pizza.type)));
        _activeCooks--;
        return;
    }
    
    // Consume ingredients
    consumeIngredients(pizza);
    
    LOG_INFO("Kitchen " + std::to_string(_id) + " cooking pizza type " + 
             std::to_string(static_cast<int>(pizza.type)) + " for " + 
             std::to_string(pizza.cookingTime) + "ms");
    
    // Cook the pizza
    Timer::sleep(pizza.cookingTime);
    
    // Remove from queue (pizza is now cooked)
    {
        ScopedLock lock(_queueMutex);
        if (!_pizzaQueue.empty()) {
            _pizzaQueue.pop();
        }
    }
    
    // Pizza is ready
    LOG_INFO("Kitchen " + std::to_string(_id) + " finished cooking pizza type " + 
             std::to_string(static_cast<int>(pizza.type)) + " - PIZZA READY!");
    
    // Send ready notification via IPC
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
    updateLastActivity(); // Update activity when finishing cooking
    
    LOG_INFO("Kitchen " + std::to_string(_id) + " cook finished (active cooks: " + 
             std::to_string(static_cast<int>(_activeCooks)) + ")");
}

void Kitchen::restockIngredients() {
    ScopedLock lock(_ingredientMutex);
    
    for (auto& ingredient : _ingredients) {
        ingredient.second = std::min(ingredient.second + 1, 10); // Cap at 10
    }
    
    LOG_DEBUG("Kitchen " + std::to_string(_id) + " restocked ingredients");
}

void Kitchen::communicateWithReception() {
    while (_active) {
        try {
            if (_ipc && _ipc->isReady()) {
                // Send status updates periodically
                KitchenStatus status = getStatus();
                *_ipc << status;
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Kitchen " + std::to_string(_id) + " communication error: " + e.what());
        }
        
        Timer::sleep(1000); // Send status every second
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