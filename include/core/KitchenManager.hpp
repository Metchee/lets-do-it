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
    void checkForCompletedPizzas();
    
    std::vector<KitchenStatus> getAllKitchenStatuses() const;
    int getKitchenCount() const;
    void cleanup();

private:
    bool sendPizzaToKitchen(int kitchenIndex, const SerializedPizza& pizza);
    bool sendPizzaViaIPC(KitchenProcess* kitchenProcess, const SerializedPizza& pizza);
    
    pid_t forkKitchenProcess(std::unique_ptr<Kitchen> kitchen, 
                            std::unique_ptr<PipeIPC> ipc, int kitchenId);
    void setupChildProcess(std::unique_ptr<Kitchen> kitchen, 
                          std::unique_ptr<PipeIPC> ipc, int kitchenId);
    void setupParentProcess(std::unique_ptr<Kitchen> kitchen, 
                           std::unique_ptr<PipeIPC> ipc, pid_t pid);
    
    bool shouldCloseKitchen(const std::unique_ptr<KitchenProcess>& kitchenProcess);
    void terminateKitchenProcess(const std::unique_ptr<KitchenProcess>& kitchenProcess);
    void waitForKitchenTermination(pid_t pid);
    
    int findBestKitchen() const;
    void cleanupDeadKitchens();
    bool isKitchenReady(KitchenProcess* kitchenProcess) const;
    void processKitchenMessages(KitchenProcess* kitchenProcess);
    std::string receiveKitchenMessage(KitchenProcess* kitchenProcess) const;
    void handleKitchenMessage(const std::string& message, int kitchenId);
    void handleCompletedPizza(const std::string& pizzaData, int kitchenId);
    
    void displayStatusHeader() const;
    void displayNoKitchensMessage() const;
    void displayStatusFooter() const;
    void displayAllKitchens() const;
    void displaySingleKitchen(KitchenProcess* kitchenProcess) const;
    void displayKitchenInfo(const KitchenStatus& status, pid_t pid) const;
    void displayIngredients(const std::vector<int>& ingredients) const;
    
    KitchenStatus getKitchenStatus(KitchenProcess* kitchenProcess) const;
    bool requestKitchenStatus(KitchenProcess* kitchenProcess, KitchenStatus& status) const;
    bool waitForStatusResponse(KitchenProcess* kitchenProcess, KitchenStatus& status) const;
    KitchenStatus createFallbackStatus(int kitchenId) const;
};

#endif