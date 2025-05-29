#include "utils/Parser.hpp"
#include "utils/Exception.hpp"
#include <sstream>
#include <regex>
#include <algorithm>
#include <cctype>

std::vector<PizzaOrder> Parser::parseOrderCommand(const std::string& command) {
    std::vector<PizzaOrder> orders;
    
    std::string cleanCommand = command;
    size_t commentPos = cleanCommand.find('#');
    if (commentPos != std::string::npos) {
        cleanCommand = cleanCommand.substr(0, commentPos);
    }
    
    cleanCommand = trim(cleanCommand);
    
    if (!isValidCommand(cleanCommand)) {
        throw ParsingException("Invalid command format");
    }
    
    std::vector<std::string> orderStrings = tokenize(cleanCommand);
    
    for (const std::string& orderStr : orderStrings) {
        std::istringstream iss(trim(orderStr));
        std::string type, size, quantity;
        
        if (!(iss >> type >> size >> quantity)) {
            throw ParsingException("Invalid order format: " + orderStr);
        }
        
        if (!isValidPizzaType(type) || !isValidPizzaSize(size) || !isValidQuantity(quantity)) {
            throw ParsingException("Invalid pizza specification: " + orderStr);
        }
        
        PizzaOrder order;
        order.type = PizzaTypeHelper::stringToPizzaType(type);
        order.size = PizzaTypeHelper::stringToPizzaSize(size);
        order.quantity = parseQuantity(quantity);
        
        orders.push_back(order);
    }
    
    return orders;
}

bool Parser::isValidCommand(const std::string& command) {
    if (command.empty()) {
        return false;
    }
    
    std::regex pattern(R"(^[a-zA-Z]+\s+(S|M|L|XL|XXL)\s+x[1-9][0-9]*(\s*;\s*[a-zA-Z]+\s+(S|M|L|XL|XXL)\s+x[1-9][0-9]*)*$)");
    return std::regex_match(command, pattern);
}

std::vector<std::string> Parser::tokenize(const std::string& input) {
    std::vector<std::string> tokens;
    std::istringstream iss(input);
    std::string token;
    
    while (std::getline(iss, token, ';')) {
        tokens.push_back(trim(token));
    }
    
    return tokens;
}

std::string Parser::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        return "";
    }
    
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

bool Parser::isValidPizzaType(const std::string& type) {
    std::string lower = type;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    return lower == "regina" || lower == "margarita" || 
           lower == "americana" || lower == "fantasia";
}

bool Parser::isValidPizzaSize(const std::string& size) {
    return size == "S" || size == "M" || size == "L" || 
           size == "XL" || size == "XXL";
}

bool Parser::isValidQuantity(const std::string& quantity) {
    if (quantity.empty() || quantity[0] != 'x') {
        return false;
    }
    
    std::string numStr = quantity.substr(1);
    if (numStr.empty()) {
        return false;
    }
    
    for (char c : numStr) {
        if (!std::isdigit(c)) {
            return false;
        }
    }
    
    int num = std::stoi(numStr);
    return num > 0 && num <= 99;
}

int Parser::parseQuantity(const std::string& quantity) {
    return std::stoi(quantity.substr(1));
}