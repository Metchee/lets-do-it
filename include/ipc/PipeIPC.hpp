#ifndef PIPEIPC_HPP
#define PIPEIPC_HPP

#include "IPPC.hpp"
#include <unistd.h>

class PipeIPC : public IIPC {
private:
    int _parentToChildRead;
    int _parentToChildWrite;
    int _childToParentRead;
    int _childToParentWrite;
    bool _isParent;
    bool _closed;

public:
    PipeIPC();
    ~PipeIPC();
    
    PipeIPC(const PipeIPC&) = delete;
    PipeIPC& operator=(const PipeIPC&) = delete;
    
    bool createPipes();
    void setupParent();
    void setupChild();
    
    bool send(const std::string& message) override;
    std::string receive() override;
    bool isReady() const override;
    void close() override;
    
    IIPC& operator<<(const SerializedPizza& pizza) override;
    IIPC& operator>>(SerializedPizza& pizza) override;
    IIPC& operator<<(const KitchenStatus& status) override;
    IIPC& operator>>(KitchenStatus& status) override;

private:
    bool writeData(int fd, const void* data, size_t size);
    bool readData(int fd, void* data, size_t size);
};

#endif