#include "threading/ThreadPool.hpp"

ThreadPool::ThreadPool(size_t numThreads) : _stop(false) {
    for (size_t i = 0; i < numThreads; ++i) {
        _workers.emplace_back(&ThreadPool::workerLoop, this);
    }
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::stop() {
    {
        ScopedLock lock(_mutex);
        _stop = true;
    }
    
    _condition.notifyAll();
    
    for (std::thread &worker : _workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

size_t ThreadPool::getWorkerCount() const {
    return _workers.size();
}

size_t ThreadPool::getPendingTasks() const {
    ScopedLock lock(const_cast<Mutex&>(_mutex));
    return _tasks.size();
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        
        {
            ScopedLock lock(_mutex);
            _condition.wait(_mutex, [this] { return _stop || !_tasks.empty(); });
            
            if (_stop && _tasks.empty()) {
                return;
            }
            
            task = std::move(_tasks.front());
            _tasks.pop();
        }
        
        task();
    }
}