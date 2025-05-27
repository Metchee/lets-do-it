#include "threading/ConditionVariable.hpp"

void ConditionVariable::wait(Mutex& mutex) {
    std::unique_lock<std::mutex> lock(mutex.getMutex(), std::adopt_lock);
    _cv.wait(lock);
    lock.release();
}

void ConditionVariable::wait(Mutex& mutex, std::function<bool()> predicate) {
    std::unique_lock<std::mutex> lock(mutex.getMutex(), std::adopt_lock);
    _cv.wait(lock, predicate);
    lock.release();
}

void ConditionVariable::notifyOne() {
    _cv.notify_one();
}

void ConditionVariable::notifyAll() {
    _cv.notify_all();
}