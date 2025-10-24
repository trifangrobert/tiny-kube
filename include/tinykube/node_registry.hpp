#pragma once
#include <unordered_map>
#include <mutex>
#include <vector>

#include "tinykube/types.hpp"

namespace tinykube {
    class NodeRegistry {
    public:
        void upsert(const NodeState& node) {
            // this acts as a lock/unlock with RAII
            std::lock_guard<std::mutex> lock(mutex_);
            nodes_[node.name] = node;
        }

        void touch(const std::string& node_name, int64_t now_ms) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (nodes_.contains(node_name)) {
                nodes_[node_name].last_seen_ms = now_ms;
                nodes_[node_name].status = NodeStatus::READY;
            }
        }

        bool remove(const std::string& node_name) {
            std::lock_guard<std::mutex> lock(mutex_);
            return nodes_.erase(node_name) > 0;
        }

        bool exists(const std::string& node_name) const {
            std::lock_guard<std::mutex> lock(mutex_);
            return nodes_.contains(node_name);
        }

        size_t size() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return nodes_.size();
        }

        std::vector<NodeState> snapshot() {
            std::lock_guard<std::mutex> lock(mutex_);
            std::vector<NodeState> snapshot;
            snapshot.reserve(nodes_.size());
            for (const auto& [_, state] : nodes_) {
                snapshot.push_back(state);
            }
            return snapshot;
        }
    private:
        std::unordered_map<std::string, NodeState> nodes_;
        mutable std::mutex mutex_;
    };
} // namespace tinykube