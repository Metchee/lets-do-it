#ifndef PIZZA_HPP
#define PIZZA_HPP

#include "IPizza.hpp"
#include <chrono>

class Pizza : public IPizza {
private:
    PizzaType _type;
    PizzaSize _size;
    std::vector<Ingredient> _ingredients;
    int _cookingTime;
    bool _cooked;
    std::string _name;

public:
    Pizza(PizzaType type, PizzaSize size, double multiplier = 1.0);
    ~Pizza() = default;

    PizzaType getType() const override;
    PizzaSize getSize() const override;
    std::vector<Ingredient> getIngredients() const override;
    int getCookingTime() const override;
    std::string getName() const override;
    bool isCooked() const override;
    void cook() override;

private:
    void initializeIngredients();
    void calculateCookingTime(double multiplier);
};

#endif