#ifndef PARSER_HPP
#define PARSER_HPP

#include "pizza/PizzaType.hpp"
#include <string>
#include <vector>

class Parser {
public:
    static std::vector<PizzaOrder> parseOrderCommand(const std::string& command);
    static bool isValidCommand(const std::string& command);
    
private:
    static std::vector<std::string> tokenize(const std::string& input);
    static std::string trim(const std::string& str);
    static bool isValidPizzaType(const std::string& type);
    static bool isValidPizzaSize(const std::string& size);
    static bool isValidQuantity(const std::string& quantity);
    static int parseQuantity(const std::string& quantity);
};

#endif