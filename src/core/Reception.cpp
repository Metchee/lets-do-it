#include "core/Reception.hpp"
#include "pizza/PizzaFactory.hpp"
#include "utils/Exception.hpp"
#include <iostream>
#include <sstream>
#include <signal.h>

Reception::Reception(double multiplier, int numCooksPerKitchen, int restockTime)
    : _multiplier(multiplier), _numCooksPerKitchen(numCooksPerKitchen), 
      _restockTime(restockTime), _running(false) {
    
    _kitchenManager = std::make_unique<KitchenManager>(numCooksPerKitchen, multiplier, restockTime);
}

Reception::~Reception() {
    stop();
}

void Reception::run() {
    _running = true;
    
    displayWelcome();
    showHelp();
    
    // Handle Ctrl+C gracefully
    signal(SIGINT, [](int) {
        std::cout << "\nShutting down Plazza..." << std::endl;
        exit(0);
    });
    
    std::string input;
    
    while (_running) {
        std::cout << "plazza> ";
        std::getline(std::cin, input);
        
        if (std::cin.eof()) {
            break;
        }
        
        if (input.empty()) {
            continue;
        }
        
        try {
            processCommand(input);
        } catch (const PlazzaException& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            LOG_ERROR(e.what());
        }
        
        // Clean up inactive kitchens periodically (but not too aggressively)
        static int cleanupCounter = 0;
        if (++cleanupCounter >= 10) { // Only check every 10 commands
            _kitchenManager->closeInactiveKitchens();
            cleanupCounter = 0;
        }
    }
    
    LOG_INFO("Reception shutting down");
}

void Reception::stop() {
    _running = false;
    if (_kitchenManager) {
        _kitchenManager->cleanup();
    }
}

void Reception::processCommand(const std::string& command) {
    std::string trimmed = command;
    
    // Remove leading/trailing whitespace
    size_t start = trimmed.find_first_not_of(" \t");
    if (start == std::string::npos) {
        return;
    }
    
    size_t end = trimmed.find_last_not_of(" \t");
    trimmed = trimmed.substr(start, end - start + 1);
    
    // Check for special commands
    if (trimmed == "status") {
        handleStatusCommand();
    } else if (trimmed == "help") {
        showHelp();
    } else if (trimmed == "quit" || trimmed == "exit") {
        _running = false;
    } else {
        // Assume it's a pizza order
        handleOrderCommand(trimmed);
    }
}

void Reception::handleOrderCommand(const std::string& command) {
    try {
        std::vector<PizzaOrder> orders = Parser::parseOrderCommand(command);
        
        if (orders.empty()) {
            std::cout << "No valid orders found in command." << std::endl;
            return;
        }
        
        int totalPizzas = 0;
        for (const auto& order : orders) {
            totalPizzas += order.quantity;
        }
        
        std::cout << "Processing " << totalPizzas << " pizza(s)..." << std::endl;
        
        // Process each pizza individually for load balancing
        for (const auto& order : orders) {
            for (int i = 0; i < order.quantity; ++i) {
                SerializedPizza pizza(order.type, order.size, 
                                    PizzaTypeHelper::getCookingTime(order.type) * _multiplier * 1000);
                
                std::string pizzaName = PizzaTypeHelper::pizzaTypeToString(order.type) + " " +
                                      PizzaTypeHelper::pizzaSizeToString(order.size);
                
                if (_kitchenManager->distributePizza(pizza)) {
                    std::cout << "Ordered: " << pizzaName << std::endl;
                    LOG_INFO("Pizza ordered: " + pizzaName);
                } else {
                    std::cout << "Failed to order: " << pizzaName << " (no available kitchen)" << std::endl;
                    LOG_ERROR("Failed to order pizza: " + pizzaName);
                }
            }
        }
        
        // Small delay to let child processes initialize and show their logs
        Timer::sleep(200);
        
        // Clear any mixed output and show a fresh prompt indicator
        std::cout << std::endl;
        
    } catch (const ParsingException& e) {
        std::cout << "Invalid order format. " << e.what() << std::endl;
        std::cout << "Example: regina XXL x2; fantasia M x3; margarita S x1" << std::endl;
    }
}

void Reception::handleStatusCommand() {
    _kitchenManager->displayStatus();
}

void Reception::showHelp() {
    std::cout << "\n=== PLAZZA HELP ===" << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  status          - Show kitchen status" << std::endl;
    std::cout << "  help            - Show this help message" << std::endl;
    std::cout << "  quit/exit       - Exit the program" << std::endl;
    std::cout << "\nPizza ordering format:" << std::endl;
    std::cout << "  TYPE SIZE xQUANTITY [; TYPE SIZE xQUANTITY]*" << std::endl;
    std::cout << "\nAvailable pizza types:" << std::endl;
    std::cout << "  regina, margarita, americana, fantasia" << std::endl;
    std::cout << "\nAvailable sizes:" << std::endl;
    std::cout << "  S, M, L, XL, XXL" << std::endl;
    std::cout << "\nExample:" << std::endl;
    std::cout << "  regina XXL x2; fantasia M x3; margarita S x1" << std::endl;
    std::cout << "===================" << std::endl;
}

void Reception::displayWelcome() {
    std::cout << "\n";
    std::cout << "██████╗ ██╗      █████╗ ███████╗███████╗ █████╗ " << std::endl;
    std::cout << "██╔══██╗██║     ██╔══██╗╚══███╔╝╚══███╔╝██╔══██╗" << std::endl;
    std::cout << "██████╔╝██║     ███████║  ███╔╝   ███╔╝ ███████║" << std::endl;
    std::cout << "██╔═══╝ ██║     ██╔══██║ ███╔╝   ███╔╝  ██╔══██║" << std::endl;
    std::cout << "██║     ███████╗██║  ██║███████╗███████╗██║  ██║" << std::endl;
    std::cout << "╚═╝     ╚══════╝╚═╝  ╚═╝╚══════╝╚══════╝╚═╝  ╚═╝" << std::endl;
    std::cout << "\nWelcome to Plazza - The Ultimate Pizza Ordering System!" << std::endl;
    std::cout << "WHO SAID ANYTHING ABOUT PIZZAS?" << std::endl;
    std::cout << "\nConfiguration:" << std::endl;
    std::cout << "  Cooking multiplier: " << _multiplier << std::endl;
    std::cout << "  Cooks per kitchen: " << _numCooksPerKitchen << std::endl;
    std::cout << "  Restock time: " << _restockTime << "ms" << std::endl;
}

bool Reception::isRunning() const {
    return _running;
}