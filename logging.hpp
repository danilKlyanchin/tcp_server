#pragma once

#include <iostream>
#include <mutex>
#include <utility>

namespace logging_detail {
inline std::mutex log_mutex;
}

template <typename... Args>
void LogInfo(Args&&... args) noexcept {
    try {
        std::lock_guard lock(logging_detail::log_mutex);
        (std::cout << ... << std::forward<Args>(args));
        std::cout << '\n';
    } catch (...) {
    }
}

template <typename... Args>
void LogError(Args&&... args) noexcept {
    try {
        std::lock_guard lock(logging_detail::log_mutex);
        (std::cerr << ... << std::forward<Args>(args));
        std::cerr << '\n';
    } catch (...) {
    }
}
