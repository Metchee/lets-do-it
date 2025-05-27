#ifndef PIZZAFACTORY_HPP
#define PIZZAFACTORY_HPP

#include "Pizza.hpp"
#include <memory>

class PizzaFactory {
public:
    static PizzaPtr createPizza(PizzaType type, PizzaSize size, double multiplier = 1.0);
    static PizzaPtr createPizza(const std::string& type, const std::string& size, double multiplier = 1.0);
};

#endif