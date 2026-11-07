// 单调时钟与休眠
#pragma once

#include <chrono>
#include <thread>

namespace fx {

inline double nowSeconds() {
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

inline void sleepSeconds(double seconds) {
    if (seconds <= 0.0) return;
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}

}  // namespace fx
