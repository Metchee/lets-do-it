#include "pizza/Pizza.hpp"
#include "utils/Timer.hpp"
#include <thread>
#include <algorithm>
#include <cctype>
#include <stdexcept>
Pizza::Pizza(PizzaType type, PizzaSize size, double multiplier)
    : _type(type), _size(size), _cooked(false) {
    initializeIngredients();
    calculateCookingTime(multiplier);
    _name = PizzaTypeHelper::pizzaTypeToString(type) + " " + 
            PizzaTypeHelper::pizzaSizeToString(size);
}

PizzaType Pizza::getType() const {
    return _type;
}

PizzaSize Pizza::getSize() const {
    return _size;
}

std::vector<Ingredient> Pizza::getIngredients() const {
    return _ingredients;
}

int Pizza::getCookingTime() const {
    return _cookingTime;
}

std::string Pizza::getName() const {
    return _name;
}

bool Pizza::isCooked() const {
    return _cooked;
}

void Pizza::cook() {
    Timer::sleep(_cookingTime);
    _cooked = true;
}

void Pizza::initializeIngredients() {
    _ingredients = PizzaTypeHelper::getIngredientsForPizza(_type);
}

void Pizza::calculateCookingTime(double multiplier) {
    int baseTime = PizzaTypeHelper::getCookingTime(_type);
    _cookingTime = static_cast<int>(baseTime * 1000 * multiplier);
}

std::string PizzaTypeHelper::pizzaTypeToString(PizzaType type) {
    switch (type) {
        case Regina: return "Regina";
        case Margarita: return "Margarita";
        case Americana: return "Americana";
        case Fantasia: return "Fantasia";
        default: return "Unknown";
    }
}

std::string PizzaTypeHelper::pizzaSizeToString(PizzaSize size) {
    switch (size) {
        case S: return "S";
        case M: return "M";
        case L: return "L";
        case XL: return "XL";
        case XXL: return "XXL";
        default: return "Unknown";
    }
}

PizzaType PizzaTypeHelper::stringToPizzaType(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    if (lower == "regina") return Regina;
    if (lower == "margarita") return Margarita;
    if (lower == "americana") return Americana;
    if (lower == "fantasia") return Fantasia;
    
    throw std::invalid_argument("Unknown pizza type: " + str);
}

PizzaSize PizzaTypeHelper::stringToPizzaSize(const std::string& str) {
    std::string upper = str;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    
    if (upper == "S") return S;
    if (upper == "M") return M;
    if (upper == "L") return L;
    if (upper == "XL") return XL;
    if (upper == "XXL") return XXL;
    
    throw std::invalid_argument("Unknown pizza size: " + str);
}

std::vector<Ingredient> PizzaTypeHelper::getIngredientsForPizza(PizzaType type) {
    switch (type) {
        case Margarita:
            return {Dough, Tomato, Gruyere};
        case Regina:
            return {Dough, Tomato, Gruyere, Ham, Mushrooms};
        case Americana:
            return {Dough, Tomato, Gruyere, Steak};
        case Fantasia:
            return {Dough, Tomato, Eggplant, GoatCheese, ChiefLove};
        default:
            return {};
    }
}

int PizzaTypeHelper::getCookingTime(PizzaType type) {
    switch (type) {
        case Margarita: return 1;
        case Regina: return 2;
        case Americana: return 2;
        case Fantasia: return 4;
        default: return 1;
    }
}