#ifndef IPIZZA_HPP
#define IPIZZA_HPP

#include "PizzaType.hpp"
#include <vector>
#include <memory>

class IPizza {
public:
    virtual ~IPizza() = default;
    
    virtual PizzaType getType() const = 0;
    virtual PizzaSize getSize() const = 0;
    virtual std::vector<Ingredient> getIngredients() const = 0;
    virtual int getCookingTime() const = 0;
    virtual std::string getName() const = 0;
    virtual bool isCooked() const = 0;
    virtual void cook() = 0;
};

using PizzaPtr = std::unique_ptr<IPizza>;

#endif