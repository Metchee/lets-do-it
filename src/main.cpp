#include "core/Reception.hpp"
#include "utils/Logger.hpp"
#include "utils/Exception.hpp"
#include <iostream>
#include <cstdlib>
#include <signal.h>

void signalHandler(int signum) {
    std::cout << "\nReceived signal " << signum << ". Shutting down gracefully..." << std::endl;
    exit(0);
}

void printUsage() {
    std::cout << "Usage: ./plazza <multiplier> <cooks_per_kitchen> <restock_time_ms>" << std::endl;
    std::cout << "  multiplier: Cooking time multiplier (can be between 0-1 for faster cooking)" << std::endl;
    std::cout << "  cooks_per_kitchen: Number of cooks per kitchen" << std::endl;
    std::cout << "  restock_time_ms: Time in milliseconds for ingredient restocking" << std::endl;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    if (argc != 4) {
        printUsage();
        return 84;
    }
    
    try {
        double multiplier = std::stod(argv[1]);
        int cooksPerKitchen = std::stoi(argv[2]);
        int restockTime = std::stoi(argv[3]);
        
        if (multiplier <= 0 || cooksPerKitchen <= 0 || restockTime <= 0) {
            std::cerr << "Error: All parameters must be positive" << std::endl;
            return 84;
        }
        
        Logger& logger = Logger::getInstance();
        logger.enableConsoleOutput(true);
        logger.enableFileOutput("plazza.log");
        logger.setLogLevel(LogLevel::INFO);
        
        LOG_INFO("Starting Plazza with multiplier=" + std::to_string(multiplier) + 
                 ", cooks=" + std::to_string(cooksPerKitchen) + 
                 ", restock=" + std::to_string(restockTime) + "ms");
        
        Reception reception(multiplier, cooksPerKitchen, restockTime);
        reception.run();
        
    } catch (const PlazzaException& e) {
        std::cerr << "Plazza Error: " << e.what() << std::endl;
        LOG_ERROR(std::string("Plazza Error: ") + e.what());
        return 84;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        LOG_ERROR(std::string("Unexpected error: ") + e.what());
        return 84;
    }
    
    return 0;
}