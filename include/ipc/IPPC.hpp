#ifndef IPPC_HPP
#define IPPC_HPP

#include "Serialization.hpp"
#include <string>

class IIPC {
public:
    virtual ~IIPC() = default;
    
    virtual bool send(const std::string& message) = 0;
    virtual std::string receive() = 0;
    virtual bool isReady() const = 0;
    virtual void close() = 0;
    
    virtual IIPC& operator<<(const SerializedPizza& pizza) = 0;
    virtual IIPC& operator>>(SerializedPizza& pizza) = 0;
    virtual IIPC& operator<<(const KitchenStatus& status) = 0;
    virtual IIPC& operator>>(KitchenStatus& status) = 0;
};

#endif