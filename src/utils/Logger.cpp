#include "utils/Logger.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

std::unique_ptr<Logger> Logger::_instance = nullptr;
Mutex Logger::_mutex;

Logger::Logger() : _currentLevel(LogLevel::INFO), _consoleOutput(false) {}

Logger::~Logger() {
    if (_logFile.is_open()) {
        _logFile.close();
    }
}

Logger& Logger::getInstance() {
    ScopedLock lock(_mutex);
    if (!_instance) {
        _instance = std::unique_ptr<Logger>(new Logger());
    }
    return *_instance;
}

void Logger::setLogLevel(LogLevel level) {
    ScopedLock lock(_mutex);
    _currentLevel = level;
}

void Logger::enableConsoleOutput(bool enable) {
    ScopedLock lock(_mutex);
    _consoleOutput = enable;
}

void Logger::enableFileOutput(const std::string& filename) {
    ScopedLock lock(_mutex);
    if (_logFile.is_open()) {
        _logFile.close();
    }
    _logFile.open(filename, std::ios::app);
}

void Logger::log(LogLevel level, const std::string& message) {
    if (level < _currentLevel) {
        return;
    }
    
    ScopedLock lock(_mutex);
    
    std::string timestamp = getCurrentTime();
    std::string levelStr = levelToString(level);
    std::string logMessage = "[" + timestamp + "] [" + levelStr + "] " + message;
    
    if (_consoleOutput) {
        std::cout << logMessage << std::endl;
    }
    
    if (_logFile.is_open()) {
        _logFile << logMessage << std::endl;
        _logFile.flush();
    }
}

void Logger::debug(const std::string& message) {
    log(LogLevel::DEBUG, message);
}

void Logger::info(const std::string& message) {
    log(LogLevel::INFO, message);
}

void Logger::warning(const std::string& message) {
    log(LogLevel::WARNING, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::ERROR, message);
}

void Logger::logPizzaOrder(const std::string& pizzaInfo) {
    info("Pizza ordered: " + pizzaInfo);
}

void Logger::logPizzaReady(const std::string& pizzaInfo) {
    info("Pizza ready: " + pizzaInfo);
}

void Logger::logKitchenStatus(int kitchenId, const std::string& status) {
    info("Kitchen " + std::to_string(kitchenId) + ": " + status);
}

std::string Logger::getCurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}