#include "ipc/PipeIPC.hpp"
#include "utils/Exception.hpp"
#include <sys/wait.h>
#include <fcntl.h>
#include <cstring>
#include <errno.h>
#include <cstdint>

PipeIPC::PipeIPC() : _parentToChildRead(-1), _parentToChildWrite(-1),
                     _childToParentRead(-1), _childToParentWrite(-1),
                     _isParent(true), _closed(false) {}

PipeIPC::~PipeIPC() {
    close();
}

bool PipeIPC::createPipes() {
    int parentToChild[2];
    int childToParent[2];
    
    if (pipe(parentToChild) == -1 || pipe(childToParent) == -1) {
        return false;
    }
    
    _parentToChildRead = parentToChild[0];
    _parentToChildWrite = parentToChild[1];
    _childToParentRead = childToParent[0];
    _childToParentWrite = childToParent[1];
    
    return true;
}

void PipeIPC::setupParent() {
    _isParent = true;
    if (_parentToChildRead != -1) {
        ::close(_parentToChildRead);
        _parentToChildRead = -1;
    }
    if (_childToParentWrite != -1) {
        ::close(_childToParentWrite);
        _childToParentWrite = -1;
    }
}

void PipeIPC::setupChild() {
    _isParent = false;
    if (_parentToChildWrite != -1) {
        ::close(_parentToChildWrite);
        _parentToChildWrite = -1;
    }
    if (_childToParentRead != -1) {
        ::close(_childToParentRead);
        _childToParentRead = -1;
    }
}

bool PipeIPC::send(const std::string& message) {
    if (_closed) {
        return false;
    }
    
    int writeFd = _isParent ? _parentToChildWrite : _childToParentWrite;
    if (writeFd == -1) {
        return false;
    }
    
    uint32_t length = message.length();
    
    if (!writeData(writeFd, &length, sizeof(length))) {
        return false;
    }
    
    if (!writeData(writeFd, message.c_str(), length)) {
        return false;
    }
    
    return true;
}

std::string PipeIPC::receive() {
    if (_closed) {
        return "";
    }
    
    int readFd = _isParent ? _childToParentRead : _parentToChildRead;
    if (readFd == -1) {
        return "";
    }
    
    int flags = fcntl(readFd, F_GETFL, 0);
    fcntl(readFd, F_SETFL, flags | O_NONBLOCK);
    
    uint32_t length;
    
    if (!readData(readFd, &length, sizeof(length))) {
        fcntl(readFd, F_SETFL, flags);
        return "";
    }
    
    std::string message(length, '\0');
    if (!readData(readFd, &message[0], length)) {
        fcntl(readFd, F_SETFL, flags);
        return "";
    }
    
    fcntl(readFd, F_SETFL, flags);
    
    return message;
}

bool PipeIPC::isReady() const {
    if (_isParent) {
        return !_closed && _parentToChildWrite != -1 && _childToParentRead != -1;
    } else {
        return !_closed && _parentToChildRead != -1 && _childToParentWrite != -1;
    }
}

void PipeIPC::close() {
    if (_closed) {
        return;
    }
    
    if (_parentToChildRead != -1) {
        ::close(_parentToChildRead);
        _parentToChildRead = -1;
    }
    if (_parentToChildWrite != -1) {
        ::close(_parentToChildWrite);
        _parentToChildWrite = -1;
    }
    if (_childToParentRead != -1) {
        ::close(_childToParentRead);
        _childToParentRead = -1;
    }
    if (_childToParentWrite != -1) {
        ::close(_childToParentWrite);
        _childToParentWrite = -1;
    }
    
    _closed = true;
}

IIPC& PipeIPC::operator<<(const SerializedPizza& pizza) {
    std::string data = pizza.pack();
    send("PIZZA:" + data);
    return *this;
}

IIPC& PipeIPC::operator>>(SerializedPizza& pizza) {
    std::string message = receive();
    if (message.substr(0, 6) == "PIZZA:") {
        pizza.unpack(message.substr(6));
    }
    return *this;
}

IIPC& PipeIPC::operator<<(const KitchenStatus& status) {
    std::string data = status.pack();
    send("STATUS:" + data);
    return *this;
}

IIPC& PipeIPC::operator>>(KitchenStatus& status) {
    std::string message = receive();
    if (message.substr(0, 7) == "STATUS:") {
        status.unpack(message.substr(7));
    }
    return *this;
}

bool PipeIPC::writeData(int fd, const void* data, size_t size) {
    const char* ptr = static_cast<const char*>(data);
    size_t written = 0;
    
    while (written < size) {
        ssize_t result = write(fd, ptr + written, size - written);
        if (result == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return false;
        }
        written += result;
    }
    
    return true;
}

bool PipeIPC::readData(int fd, void* data, size_t size) {
    char* ptr = static_cast<char*>(data);
    size_t bytesRead = 0;
    
    while (bytesRead < size) {
        ssize_t result = read(fd, ptr + bytesRead, size - bytesRead);
        if (result == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return false;
            }
            return false;
        }
        if (result == 0) {
            return false;
        }
        bytesRead += result;
    }
    
    return true;
}