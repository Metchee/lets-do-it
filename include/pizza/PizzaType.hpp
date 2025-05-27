#ifndef PIZZATYPE_HPP
#define PIZZATYPE_HPP

#include <string>
#include <vector>

enum PizzaType {
    Regina = 1,
    Margarita = 2,
    Americana = 4,
    Fantasia = 8
};

enum PizzaSize {
    S = 1,
    M = 2,
    L = 4,
    XL = 8,
    XXL = 16
};

enum Ingredient {
    Dough = 1,
    Tomato = 2,
    Gruyere = 4,
    Ham = 8,
    Mushrooms = 16,
    Steak = 32,
    Eggplant = 64,
    GoatCheese = 128,
    ChiefLove = 256
};

struct PizzaOrder {
    PizzaType type;
    PizzaSize size;
    int quantity;
};

class PizzaTypeHelper {
public:
    static std::string pizzaTypeToString(PizzaType type);
    static std::string pizzaSizeToString(PizzaSize size);
    static PizzaType stringToPizzaType(const std::string& str);
    static PizzaSize stringToPizzaSize(const std::string& str);
    static std::vector<Ingredient> getIngredientsForPizza(PizzaType type);
    static int getCookingTime(PizzaType type);
};

#endif