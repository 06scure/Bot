#pragma once
#include <chrono>
#include <time.h>

namespace demo::time {
using Clock = std::chrono::steady_clock;

// 操作耗时：不受系统日期校准影响。
inline double elapsedMs(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// V4L2 时间戳来自 CLOCK_MONOTONIC，不能与标准库时钟的原点混算。
inline double frameAgeMs(long long seconds, long long microseconds) {
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
    return (now.tv_sec - seconds) * 1000.0 + now.tv_nsec / 1e6 - microseconds / 1000.0;
}
}
