#ifndef TIMER_HPP
#define TIMER_HPP

#include <chrono>
#include <functional>
#include <thread>

class Timer {
private:
    std::chrono::steady_clock::time_point _startTime;
    bool _running;

public:
    Timer();
    
    void start();
    void stop();
    void reset();
    
    double getElapsedSeconds() const;
    int getElapsedMilliseconds() const;
    
    bool isRunning() const;
    
    static void sleep(int milliseconds);
    static void sleepSeconds(double seconds);
    
    static void cookingTimer(int cookingTimeMs, std::function<void()> onComplete);
};

#endif