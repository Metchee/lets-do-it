#ifndef THREADPOOL_HPP
#define THREADPOOL_HPP

#include "Mutex.hpp"
#include "ConditionVariable.hpp"
#include <vector>
#include <queue>
#include <thread>
#include <functional>
#include <memory>

class ThreadPool {
private:
    std::vector<std::thread> _workers;
    std::queue<std::function<void()>> _tasks;
    Mutex _mutex;
    ConditionVariable _condition;
    bool _stop;

public:
    explicit ThreadPool(size_t numThreads);
    ~ThreadPool();
    
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    
    template<typename F>
    void enqueue(F&& task);
    
    void stop();
    size_t getWorkerCount() const;
    size_t getPendingTasks() const;

private:
    void workerLoop();
};

template<typename F>
void ThreadPool::enqueue(F&& task) {
    {
        ScopedLock lock(_mutex);
        if (_stop) {
            return;
        }
        _tasks.emplace(std::forward<F>(task));
    }
    _condition.notifyOne();
}

#endif