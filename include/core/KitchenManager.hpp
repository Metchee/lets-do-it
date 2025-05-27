#ifndef KITCHENMANAGER_HPP
#define KITCHENMANAGER_HPP

#include "Kitchen.hpp"
#include "threading/Mutex.hpp"
#include <vector>
#include <memory>
#include <map>

struct KitchenProcess {
    std::unique_ptr<Kitchen> kitchen;
    std::unique_ptr<PipeIPC> ipc;
    pid_t pid;
    bool active;
    
    KitchenProcess(std::unique_ptr<Kitchen> k, std::unique_ptr<PipeIPC> i, pid_t p);
};

class KitchenManager {
private:
    std::vector<std::unique_ptr<KitchenProcess>> _kitchens;
    int _numCooksPerKitchen;
    double _multiplier;
    int _restockTime;
    int _nextKitchenId;
    
    Mutex _kitchensMutex;

public:
    KitchenManager(int numCooksPerKitchen, double multiplier, int restockTime);
    ~KitchenManager();
    
    KitchenManager(const KitchenManager&) = delete;
    KitchenManager& operator=(const KitchenManager&) = delete;
    
    bool distributePizza(const SerializedPizza& pizza);
    void createNewKitchen();
    void closeInactiveKitchens();
    void displayStatus() const;
    
    std::vector<KitchenStatus> getAllKitchenStatuses() const;
    int getKitchenCount() const;
    
    void cleanup();

private:
    int findBestKitchen() const;
    void forkKitchen(int kitchenId);
    void cleanupDeadKitchens();
};

#endif