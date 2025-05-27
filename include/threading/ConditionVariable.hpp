#ifndef CONDITIONVARIABLE_HPP
#define CONDITIONVARIABLE_HPP

#include "Mutex.hpp"
#include <condition_variable>
#include <functional>

class ConditionVariable {
private:
    std::condition_variable _cv;

public:
    ConditionVariable() = default;
    ~ConditionVariable() = default;
    
    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;
    
    void wait(Mutex& mutex);
    void wait(Mutex& mutex, std::function<bool()> predicate);
    
    template<typename Rep, typename Period>
    bool waitFor(Mutex& mutex, const std::chrono::duration<Rep, Period>& timeout);
    
    template<typename Rep, typename Period>
    bool waitFor(Mutex& mutex, const std::chrono::duration<Rep, Period>& timeout, 
                 std::function<bool()> predicate);
    
    void notifyOne();
    void notifyAll();
};

template<typename Rep, typename Period>
bool ConditionVariable::waitFor(Mutex& mutex, const std::chrono::duration<Rep, Period>& timeout) {
    std::unique_lock<std::mutex> lock(mutex.getMutex(), std::adopt_lock);
    bool result = _cv.wait_for(lock, timeout) == std::cv_status::no_timeout;
    lock.release();
    return result;
}

template<typename Rep, typename Period>
bool ConditionVariable::waitFor(Mutex& mutex, const std::chrono::duration<Rep, Period>& timeout,
                                std::function<bool()> predicate) {
    std::unique_lock<std::mutex> lock(mutex.getMutex(), std::adopt_lock);
    bool result = _cv.wait_for(lock, timeout, predicate);
    lock.release();
    return result;
}

#endif