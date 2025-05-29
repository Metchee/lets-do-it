#ifndef KITCHEN_HPP
#define KITCHEN_HPP

#include "IKitchen.hpp"
#include "pizza/Pizza.hpp"
#include "threading/ThreadPool.hpp"
#include "threading/Mutex.hpp"
#include "ipc/PipeIPC.hpp"
#include "utils/Timer.hpp"
#include <queue>
#include <map>
#include <memory>
#include <atomic>

class Kitchen : public IKitchen {
private:
    int _id;
    int _numCooks;
    double _multiplier;
    int _restockTime;
    
    std::unique_ptr<ThreadPool> _threadPool;
    std::queue<SerializedPizza> _pizzaQueue;
    std::map<Ingredient, int> _ingredients;
    
    Mutex _queueMutex;
    Mutex _ingredientMutex;
    
    std::unique_ptr<PipeIPC> _ipc;
    std::atomic<bool> _active;
    std::atomic<int> _activeCooks;
    std::atomic<int> _pendingPizzas;
    
    Timer _lastActivityTimer;
    std::thread _restockThread;
    std::thread _communicationThread;

public:
    Kitchen(int id, int numCooks, double multiplier, int restockTime);
    ~Kitchen();
    
    Kitchen(const Kitchen&) = delete;
    Kitchen& operator=(const Kitchen&) = delete;
    
    bool canAcceptPizza() const override;
    bool addPizza(const SerializedPizza& pizza) override;
    void start() override;
    void stop() override;
    bool isActive() const override;
    KitchenStatus getStatus() const override;
    int getId() const override;
    void updateLastActivity() override;
    bool shouldClose() const override;
    
    void setIPC(std::unique_ptr<PipeIPC> ipc);
    
    void runAsChildProcess();
    
    void incrementPendingPizzas();
    void decrementPendingPizzas();
    int getPendingPizzaCount() const;
    void decrementQueueSize();

private:
    void cookPizza(const SerializedPizza& pizza);
    void restockIngredients();
    void communicateWithReception();
    bool hasIngredients(const SerializedPizza& pizza);
    void consumeIngredients(const SerializedPizza& pizza);
    void initializeIngredients();
    void restockLoop();
};

#endif