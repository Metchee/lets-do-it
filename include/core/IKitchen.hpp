#ifndef IKITCHEN_HPP
#define IKITCHEN_HPP

#include "pizza/PizzaType.hpp"
#include "ipc/Serialization.hpp"
#include <vector>

class IKitchen {
public:
    virtual ~IKitchen() = default;
    
    virtual bool canAcceptPizza() const = 0;
    virtual bool addPizza(const SerializedPizza& pizza) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isActive() const = 0;
    virtual KitchenStatus getStatus() const = 0;
    virtual int getId() const = 0;
    virtual void updateLastActivity() = 0;
    virtual bool shouldClose() const = 0;
};

#endif