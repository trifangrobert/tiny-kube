#pragma once
#include <cstdint>
#include <string>

namespace tinykube {
    enum class NodeStatus : uint8_t {
        RESERVED = 0,    // good practice
        READY = 1,       // healthy
        NOT_READY = 2,   // node exists but not ready (starting up)
        SUSPECT = 3,     // missed some heartbeats
        UNKNOWN = 4
    };

    struct NodeState {
        std::string name;
        std::string peer;
        int64_t last_seen_ms;
        NodeStatus status{NodeStatus::NOT_READY};

        bool is_healthy() const {
            return status == NodeStatus::READY;
        }

        bool is_suspect(int64_t current_time_ms, int64_t timeout_ms = 30000) const {
            return (current_time_ms - last_seen_ms) > timeout_ms;
        }

        bool is_not_ready(int64_t current_time_ms, int64_t not_ready_timeout_ms = 10000) const {
            return (current_time_ms - last_seen_ms) > not_ready_timeout_ms;
        }
    };



} // namespace tinykube