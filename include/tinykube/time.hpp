#pragma once
#include <chrono>
#include <cstdint>

namespace tinykube {
    inline int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
} // namespace tinykube