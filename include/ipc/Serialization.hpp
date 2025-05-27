#ifndef SERIALIZATION_HPP
#define SERIALIZATION_HPP

#include "pizza/PizzaType.hpp"
#include <string>
#include <vector>

struct SerializedPizza {
    PizzaType type;
    PizzaSize size;
    int cookingTime;
    bool isCooked;
    
    SerializedPizza() = default;
    SerializedPizza(PizzaType t, PizzaSize s, int ct, bool cooked = false);
    
    std::string pack() const;
    void unpack(const std::string& data);
};

struct KitchenStatus {
    int kitchenId;
    int activeCooks;
    int totalCooks;
    int pizzasInQueue;
    int maxCapacity;
    std::vector<int> ingredients;
    
    KitchenStatus() = default;
    KitchenStatus(int id, int active, int total, int queue, int capacity);
    
    std::string pack() const;
    void unpack(const std::string& data);
};

class Serializer {
public:
    static std::string serialize(const SerializedPizza& pizza);
    static std::string serialize(const KitchenStatus& status);
    static std::string serialize(const std::vector<PizzaOrder>& orders);
    
    static SerializedPizza deserializePizza(const std::string& data);
    static KitchenStatus deserializeKitchenStatus(const std::string& data);
    static std::vector<PizzaOrder> deserializeOrders(const std::string& data);
    
    static std::vector<std::string> split(const std::string& str, char delimiter);
    static std::string join(const std::vector<std::string>& vec, char delimiter);
};

#endif