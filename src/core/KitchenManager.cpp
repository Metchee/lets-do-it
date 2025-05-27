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
    
    // Clean up dead kitchens first
    cleanupDeadKitchens();
    
    // Find the best kitchen for load balancing
    int bestKitchenIndex = findBestKitchen();
    
    if (bestKitchenIndex == -1) {
        // No suitable kitchen found, create a new one
        createNewKitchen();
        bestKitchenIndex = _kitchens.size() - 1;
    }
    
    if (bestKitchenIndex >= 0 && bestKitchenIndex < static_cast<int>(_kitchens.size())) {
        auto& kitchenProcess = _kitchens[bestKitchenIndex];
        
        // Check if kitchen can accept the pizza
        if (!kitchenProcess->kitchen->addPizza(pizza)) {
            return false;
        }
        
        // Send pizza via IPC
        if (kitchenProcess->ipc && kitchenProcess->ipc->isReady()) {
            try {
                bool success = kitchenProcess->ipc->send("PIZZA:" + pizza.pack());
                if (success) {
                    LOG_INFO("Distributed pizza to kitchen " + std::to_string(kitchenProcess->kitchen->getId()));
                    return true;
                } else {
                    LOG_ERROR("Failed to send pizza via IPC to kitchen " + std::to_string(kitchenProcess->kitchen->getId()));
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
    
    // Fork the kitchen process
    pid_t pid = fork();
    
    if (pid == -1) {
        throw KitchenException("Failed to fork kitchen process");
    }
    
    if (pid == 0) {
        // Child process - run the kitchen
        // Configure separate logging for child process
        Logger& logger = Logger::getInstance();
        logger.enableConsoleOutput(false); // No console output for child
        logger.enableFileOutput("kitchen_" + std::to_string(kitchenId) + ".log");
        
        ipc->setupChild();
        kitchen->setIPC(std::move(ipc));
        kitchen->runAsChildProcess();
        exit(0);
    } else {
        // Parent process - manage the kitchen
        ipc->setupParent();
        kitchen->start(); // Minimal setup in parent
        
        auto kitchenProcess = std::make_unique<KitchenProcess>(
            std::move(kitchen), std::move(ipc), pid);
        
        _kitchens.push_back(std::move(kitchenProcess));
        
        LOG_INFO("Created new kitchen " + std::to_string(kitchenId) + 
                 " with PID " + std::to_string(pid));
        
        // Give the child process time to initialize
        Timer::sleep(100);
    }
}

void KitchenManager::closeInactiveKitchens() {
    ScopedLock lock(_kitchensMutex);
    
    for (auto it = _kitchens.begin(); it != _kitchens.end();) {
        auto& kitchenProcess = *it;
        
        // Check if child process is still alive first
        int status;
        pid_t result = waitpid(kitchenProcess->pid, &status, WNOHANG);
        
        if (result == kitchenProcess->pid) {
            // Child has already terminated
            LOG_INFO("Kitchen " + std::to_string(kitchenProcess->kitchen->getId()) + 
                     " process already terminated");
            it = _kitchens.erase(it);
            continue;
        }
        
        // Check if kitchen should close (inactive for too long)
        if (kitchenProcess->kitchen->shouldClose()) {
            LOG_INFO("Closing inactive kitchen " + 
                     std::to_string(kitchenProcess->kitchen->getId()));
            
            // Send termination signal to child process
            if (kill(kitchenProcess->pid, SIGTERM) == 0) {
                // Wait for child to terminate with timeout
                for (int i = 0; i < 10; ++i) {  // Wait up to 1 second
                    result = waitpid(kitchenProcess->pid, &status, WNOHANG);
                    if (result == kitchenProcess->pid) {
                        break;
                    }
                    usleep(100000);  // Sleep 100ms
                }
                
                // Force kill if still alive
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
        return;
    }
    
    for (const auto& kitchenProcess : _kitchens) {
        if (!kitchenProcess->active) continue;
        
        // Request fresh status from child process
        KitchenStatus status;
        if (kitchenProcess->ipc && kitchenProcess->ipc->isReady()) {
            try {
                // Send status request
                if (kitchenProcess->ipc->send("STATUS_REQUEST")) {
                    LOG_INFO("Sent STATUS_REQUEST to kitchen " + std::to_string(kitchenProcess->kitchen->getId()));
                    
                    // Wait for response with longer timeout
                    bool gotResponse = false;
                    for (int i = 0; i < 50; ++i) { // Wait up to 500ms
                        std::string response = kitchenProcess->ipc->receive();
                        if (!response.empty()) {
                            LOG_INFO("Received response from kitchen " + std::to_string(kitchenProcess->kitchen->getId()) + ": " + 
                                    (response.length() > 100 ? response.substr(0, 100) + "..." : response));
                            
                            if (response.substr(0, 7) == "STATUS:") {
                                status.unpack(response.substr(7));
                                gotResponse = true;
                                LOG_INFO("Successfully parsed status from kitchen " + std::to_string(kitchenProcess->kitchen->getId()));
                                break;
                            }
                        }
                        Timer::sleep(10);
                    }
                    
                    if (!gotResponse) {
                        LOG_WARNING("No response from kitchen " + std::to_string(kitchenProcess->kitchen->getId()) + ", using fallback");
                        // Fallback to parent's simulated status
                        status = kitchenProcess->kitchen->getStatus();
                    }
                } else {
                    LOG_ERROR("Failed to send STATUS_REQUEST to kitchen " + std::to_string(kitchenProcess->kitchen->getId()));
                    status = kitchenProcess->kitchen->getStatus();
                }
                
            } catch (const std::exception& e) {
                // Fallback to parent's simulated status
                status = kitchenProcess->kitchen->getStatus();
                LOG_ERROR("Failed to get real status from kitchen " + 
                         std::to_string(kitchenProcess->kitchen->getId()) + ": " + e.what());
            }
        } else {
            status = kitchenProcess->kitchen->getStatus();
            LOG_WARNING("IPC not ready for kitchen " + std::to_string(kitchenProcess->kitchen->getId()));
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
            // Send termination signal
            kill(kitchenProcess->pid, SIGTERM);
            
            // Wait for child to terminate
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
        
        KitchenStatus status = kitchenProcess->kitchen->getStatus();
        
        // Can't accept more pizzas
        if (status.pizzasInQueue >= status.maxCapacity) {
            continue;
        }
        
        // Calculate load (pizzas in queue + active cooks)
        int load = status.pizzasInQueue + status.activeCooks;
        
        if (load < minLoad) {
            minLoad = load;
            bestIndex = static_cast<int>(i);
        }
    }
    
    return bestIndex;
}

void KitchenManager::cleanupDeadKitchens() {
    for (auto it = _kitchens.begin(); it != _kitchens.end();) {
        auto& kitchenProcess = *it;
        
        // Check if child process is still alive
        int status;
        pid_t result = waitpid(kitchenProcess->pid, &status, WNOHANG);
        
        if (result == kitchenProcess->pid) {
            // Child has terminated
            LOG_INFO("Kitchen " + std::to_string(kitchenProcess->kitchen->getId()) + 
                     " process terminated");
            it = _kitchens.erase(it);
        } else {
            ++it;
        }
    }
}