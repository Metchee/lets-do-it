#include "ipc/Serialization.hpp"
#include <sstream>
#include <algorithm>

SerializedPizza::SerializedPizza(PizzaType t, PizzaSize s, int ct, bool cooked)
    : type(t), size(s), cookingTime(ct), isCooked(cooked) {}

std::string SerializedPizza::pack() const {
    std::ostringstream oss;
    oss << static_cast<int>(type) << "|" 
        << static_cast<int>(size) << "|" 
        << cookingTime << "|" 
        << (isCooked ? 1 : 0);
    return oss.str();
}

void SerializedPizza::unpack(const std::string& data) {
    auto parts = Serializer::split(data, '|');
    if (parts.size() != 4) {
        throw std::invalid_argument("Invalid serialized pizza data");
    }
    
    type = static_cast<PizzaType>(std::stoi(parts[0]));
    size = static_cast<PizzaSize>(std::stoi(parts[1]));
    cookingTime = std::stoi(parts[2]);
    isCooked = std::stoi(parts[3]) == 1;
}

KitchenStatus::KitchenStatus(int id, int active, int total, int queue, int capacity)
    : kitchenId(id), activeCooks(active), totalCooks(total), 
      pizzasInQueue(queue), maxCapacity(capacity) {
    ingredients.resize(9, 5);
}

std::string KitchenStatus::pack() const {
    std::ostringstream oss;
    oss << kitchenId << "|" << activeCooks << "|" << totalCooks << "|" 
        << pizzasInQueue << "|" << maxCapacity << "|";
    
    for (size_t i = 0; i < ingredients.size(); ++i) {
        oss << ingredients[i];
        if (i < ingredients.size() - 1) oss << ",";
    }
    
    return oss.str();
}

void KitchenStatus::unpack(const std::string& data) {
    auto parts = Serializer::split(data, '|');
    if (parts.size() != 6) {
        throw std::invalid_argument("Invalid kitchen status data");
    }
    
    kitchenId = std::stoi(parts[0]);
    activeCooks = std::stoi(parts[1]);
    totalCooks = std::stoi(parts[2]);
    pizzasInQueue = std::stoi(parts[3]);
    maxCapacity = std::stoi(parts[4]);
    
    auto ingredientParts = Serializer::split(parts[5], ',');
    ingredients.clear();
    for (const auto& ing : ingredientParts) {
        ingredients.push_back(std::stoi(ing));
    }
}

std::string Serializer::serialize(const SerializedPizza& pizza) {
    return pizza.pack();
}

std::string Serializer::serialize(const KitchenStatus& status) {
    return status.pack();
}

std::string Serializer::serialize(const std::vector<PizzaOrder>& orders) {
    std::ostringstream oss;
    for (size_t i = 0; i < orders.size(); ++i) {
        oss << static_cast<int>(orders[i].type) << ":" 
            << static_cast<int>(orders[i].size) << ":" 
            << orders[i].quantity;
        if (i < orders.size() - 1) oss << ";";
    }
    return oss.str();
}

SerializedPizza Serializer::deserializePizza(const std::string& data) {
    SerializedPizza pizza;
    pizza.unpack(data);
    return pizza;
}

KitchenStatus Serializer::deserializeKitchenStatus(const std::string& data) {
    KitchenStatus status;
    status.unpack(data);
    return status;
}

std::vector<PizzaOrder> Serializer::deserializeOrders(const std::string& data) {
    std::vector<PizzaOrder> orders;
    auto orderParts = split(data, ';');
    
    for (const auto& orderStr : orderParts) {
        auto parts = split(orderStr, ':');
        if (parts.size() == 3) {
            PizzaOrder order;
            order.type = static_cast<PizzaType>(std::stoi(parts[0]));
            order.size = static_cast<PizzaSize>(std::stoi(parts[1]));
            order.quantity = std::stoi(parts[2]);
            orders.push_back(order);
        }
    }
    
    return orders;
}

std::vector<std::string> Serializer::split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    
    while (std::getline(ss, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    
    return tokens;
}

std::string Serializer::join(const std::vector<std::string>& vec, char delimiter) {
    std::ostringstream oss;
    for (size_t i = 0; i < vec.size(); ++i) {
        oss << vec[i];
        if (i < vec.size() - 1) oss << delimiter;
    }
    return oss.str();
}