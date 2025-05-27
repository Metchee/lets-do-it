#ifndef MUTEX_HPP
#define MUTEX_HPP

#include <mutex>

class Mutex {
private:
    std::mutex _mutex;

public:
    Mutex() = default;
    ~Mutex() = default;
    
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    
    void lock();
    void unlock();
    bool try_lock();
    
    std::mutex& getMutex();
};

class ScopedLock {
private:
    Mutex& _mutex;
    
public:
    explicit ScopedLock(Mutex& mutex);
    ~ScopedLock();
    
    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;
};

#endif