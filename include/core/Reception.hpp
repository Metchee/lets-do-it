#ifndef RECEPTION_HPP
#define RECEPTION_HPP

#include "KitchenManager.hpp"
#include "utils/Parser.hpp"
#include "utils/Logger.hpp"
#include <string>
#include <atomic>

class Reception {
private:
    std::unique_ptr<KitchenManager> _kitchenManager;
    double _multiplier;
    int _numCooksPerKitchen;
    int _restockTime;
    std::atomic<bool> _running;

public:
    Reception(double multiplier, int numCooksPerKitchen, int restockTime);
    ~Reception();
    
    Reception(const Reception&) = delete;
    Reception& operator=(const Reception&) = delete;
    
    void run();
    void stop();
    
private:
    void processCommand(const std::string& command);
    void handleOrderCommand(const std::string& command);
    void handleStatusCommand();
    void showHelp();
    void displayWelcome();
    
    bool isRunning() const;
};

#endif