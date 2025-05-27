#include "threading/Mutex.hpp"

void Mutex::lock() {
    _mutex.lock();
}

void Mutex::unlock() {
    _mutex.unlock();
}

bool Mutex::try_lock() {
    return _mutex.try_lock();
}

std::mutex& Mutex::getMutex() {
    return _mutex;
}

ScopedLock::ScopedLock(Mutex& mutex) : _mutex(mutex) {
    _mutex.lock();
}

ScopedLock::~ScopedLock() {
    _mutex.unlock();
}