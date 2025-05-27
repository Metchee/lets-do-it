#include "utils/Timer.hpp"

Timer::Timer() : _running(false) {
    reset();
}

void Timer::start() {
    _startTime = std::chrono::steady_clock::now();
    _running = true;
}

void Timer::stop() {
    _running = false;
}

void Timer::reset() {
    _startTime = std::chrono::steady_clock::now();
    _running = false;
}

double Timer::getElapsedSeconds() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - _startTime);
    return duration.count() / 1000.0;
}

int Timer::getElapsedMilliseconds() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - _startTime);
    return static_cast<int>(duration.count());
}

bool Timer::isRunning() const {
    return _running;
}

void Timer::sleep(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

void Timer::sleepSeconds(double seconds) {
    int milliseconds = static_cast<int>(seconds * 1000);
    sleep(milliseconds);
}

void Timer::cookingTimer(int cookingTimeMs, std::function<void()> onComplete) {
    std::thread([cookingTimeMs, onComplete]() {
        sleep(cookingTimeMs);
        if (onComplete) {
            onComplete();
        }
    }).detach();
}