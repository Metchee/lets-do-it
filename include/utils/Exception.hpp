#ifndef EXCEPTION_HPP
#define EXCEPTION_HPP

#include <exception>
#include <string>

class PlazzaException : public std::exception {
protected:
    std::string _message;

public:
    explicit PlazzaException(const std::string& message);
    const char* what() const noexcept override;
};

class ParsingException : public PlazzaException {
public:
    explicit ParsingException(const std::string& message);
};

class IPCException : public PlazzaException {
public:
    explicit IPCException(const std::string& message);
};

class KitchenException : public PlazzaException {
public:
    explicit KitchenException(const std::string& message);
};

class ThreadException : public PlazzaException {
public:
    explicit ThreadException(const std::string& message);
};

#endif