#include "pizza/PizzaFactory.hpp"

PizzaPtr PizzaFactory::createPizza(PizzaType type, PizzaSize size, double multiplier) {
    return std::make_unique<Pizza>(type, size, multiplier);
}

PizzaPtr PizzaFactory::createPizza(const std::string& type, const std::string& size, double multiplier) {
    PizzaType pizzaType = PizzaTypeHelper::stringToPizzaType(type);
    PizzaSize pizzaSize = PizzaTypeHelper::stringToPizzaSize(size);
    return createPizza(pizzaType, pizzaSize, multiplier);
}