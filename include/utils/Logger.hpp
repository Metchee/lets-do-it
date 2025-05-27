#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <string>
#include <fstream>
#include <memory>
#include "../threading/Mutex.hpp"

enum LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3
};

class Logger {
private:
    static std::unique_ptr<Logger> _instance;
    static Mutex _mutex;
    
    std::ofstream _logFile;
    LogLevel _currentLevel;
    bool _consoleOutput;

    Logger();

public:
    ~Logger();
    
    static Logger& getInstance();
    
    void setLogLevel(LogLevel level);
    void enableConsoleOutput(bool enable);
    void enableFileOutput(const std::string& filename);
    
    void log(LogLevel level, const std::string& message);
    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    
    void logPizzaOrder(const std::string& pizzaInfo);
    void logPizzaReady(const std::string& pizzaInfo);
    void logKitchenStatus(int kitchenId, const std::string& status);

private:
    std::string getCurrentTime();
    std::string levelToString(LogLevel level);
};

// Convenience macros
#define LOG_DEBUG(msg) Logger::getInstance().debug(msg)
#define LOG_INFO(msg) Logger::getInstance().info(msg)
#define LOG_WARNING(msg) Logger::getInstance().warning(msg)
#define LOG_ERROR(msg) Logger::getInstance().error(msg)

#endif