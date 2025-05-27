#include "utils/Exception.hpp"

PlazzaException::PlazzaException(const std::string& message) : _message(message) {}

const char* PlazzaException::what() const noexcept {
    return _message.c_str();
}

ParsingException::ParsingException(const std::string& message) 
    : PlazzaException("Parsing Error: " + message) {}

IPCException::IPCException(const std::string& message) 
    : PlazzaException("IPC Error: " + message) {}

KitchenException::KitchenException(const std::string& message) 
    : PlazzaException("Kitchen Error: " + message) {}

ThreadException::ThreadException(const std::string& message) 
    : PlazzaException("Thread Error: " + message) {}