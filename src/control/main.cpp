#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include <thread>
#include <csignal>
#include <atomic>
#include <iomanip>
#include <sstream>

#include "control_plane.grpc.pb.h"
#include "control_plane.pb.h"

#include "tinykube/node_registry.hpp"
#include "tinykube/time.hpp"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::Status;

const int64_t HEARTBEAT_TIMEOUT_MS = 3000; // 3 seconds

std::atomic<bool> g_running{true};
std::unique_ptr<Server> g_server;

// Utility functions for pretty printing
std::string status_to_string(tinykube::NodeStatus status) {
    switch (status) {
        case tinykube::NodeStatus::RESERVED:   return "RESERVED";
        case tinykube::NodeStatus::READY:      return "READY";
        case tinykube::NodeStatus::NOT_READY:  return "NOT_READY";
        case tinykube::NodeStatus::SUSPECT:    return "SUSPECT";
        case tinykube::NodeStatus::UNKNOWN:    return "UNKNOWN";
        default:                               return "INVALID";
    }
}

std::string status_to_emoji(tinykube::NodeStatus status) {
    switch (status) {
        case tinykube::NodeStatus::RESERVED:   return "🔒";
        case tinykube::NodeStatus::READY:      return "✅";
        case tinykube::NodeStatus::NOT_READY:  return "⏳";
        case tinykube::NodeStatus::SUSPECT:    return "⚠️";
        case tinykube::NodeStatus::UNKNOWN:    return "❓";
        default:                               return "❌";
    }
}

std::string format_time_ago(int64_t last_seen_ms, int64_t current_ms) {
    int64_t diff_ms = current_ms - last_seen_ms;
    
    if (diff_ms < 1000) {
        return "just now";
    } else if (diff_ms < 60000) {
        return std::to_string(diff_ms / 1000) + "s ago";
    } else if (diff_ms < 3600000) {
        return std::to_string(diff_ms / 60000) + "m ago";
    } else {
        return std::to_string(diff_ms / 3600000) + "h ago";
    }
}

void print_node_table(const std::vector<tinykube::NodeState>& nodes) {
    if (nodes.empty()) {
        std::cout << "\n📭 No nodes registered yet\n" << std::endl;
        return;
    }

    int64_t current_time = tinykube::now_ms();
    
    std::cout << "\n┌─────────────────────────────────────────────────────────────────────────┐" << std::endl;
    std::cout << "│                           🖥️  TinyKube Cluster Status                    │" << std::endl;
    std::cout << "├─────────────────────────────────────────────────────────────────────────┤" << std::endl;
    std::cout << "│ Node Name        │ Status     │ Peer Address         │ Last Seen      │" << std::endl;
    std::cout << "├─────────────────────────────────────────────────────────────────────────┤" << std::endl;
    
    for (const auto& node : nodes) {
        std::cout << "│ " 
                  << std::left << std::setw(16) << node.name << " │ "
                  << status_to_emoji(node.status) << " " << std::left << std::setw(8) << status_to_string(node.status) << " │ "
                  << std::left << std::setw(20) << node.peer << " │ "
                  << std::left << std::setw(14) << format_time_ago(node.last_seen_ms, current_time) << " │"
                  << std::endl;
    }
    
    std::cout << "└─────────────────────────────────────────────────────────────────────────┘" << std::endl;
    
    // Summary statistics
    int ready_count = 0, suspect_count = 0, not_ready_count = 0, other_count = 0;
    for (const auto& node : nodes) {
        switch (node.status) {
            case tinykube::NodeStatus::READY:     ready_count++; break;
            case tinykube::NodeStatus::SUSPECT:   suspect_count++; break;
            case tinykube::NodeStatus::NOT_READY: not_ready_count++; break;
            default:                              other_count++; break;
        }
    }
    
    std::cout << "📊 Summary: " 
              << ready_count << " ready, "
              << suspect_count << " suspect, "
              << not_ready_count << " not ready, "
              << other_count << " other"
              << " (total: " << nodes.size() << " nodes)\n" << std::endl;
}

class ControlPlaneServiceImpl final : public tinykube::ControlPlane::Service {
public:
    Status RegisterNode(ServerContext* context, 
                       const tinykube::RegisterRequest* request,
                       tinykube::RegisterResponse* response) override {
        
        const std::string& node_name = request->node().name();
        
        // Validate node name
        if (node_name.empty()) {
            std::cout << "❌ Registration rejected: empty node name from " << context->peer() << std::endl;
            response->set_accepted(false);
            response->set_reason("Node name cannot be empty");
            return Status::OK;
        }
                        
        std::cout << "📋 Node registration request received from: " 
                  << node_name << "(" << context->peer() << ")" << std::endl;

        // Check if node already exists
        if (node_registry_.exists(node_name)) {
            std::cout << "⚠️ Node " << node_name << " already registered, updating..." << std::endl;
        }

        tinykube::NodeState node_state;
        node_state.name = node_name;
        node_state.peer = context->peer();
        node_state.last_seen_ms = tinykube::now_ms();
        node_state.status = tinykube::NodeStatus::READY;
        node_registry_.upsert(node_state);
        
        // Accept the node
        response->set_accepted(true);
        response->set_reason("Welcome to TinyKube cluster!");
        
        std::cout << "✅ Node " << node_name << " registered successfully (total: " 
                  << node_registry_.size() << " nodes)" << std::endl;
        
        return Status::OK;
    }
    
    Status StreamHeartbeats(ServerContext* context,
                           ServerReader<tinykube::Heartbeat>* reader,
                           tinykube::Empty* response) override {
        
        std::cout << "💓 Starting heartbeat stream from " << context->peer() << std::endl;
        
        tinykube::Heartbeat heartbeat;
        int heartbeat_count = 0;
        
        while (reader->Read(&heartbeat)) {
            const std::string& node_name = heartbeat.node_name();
            
            // Validate that the node is registered
            if (!node_registry_.exists(node_name)) {
                std::cout << "⚠️ Received heartbeat from unregistered node: " << node_name << std::endl;
                continue;  // Ignore heartbeats from unknown nodes
            }
            
            node_registry_.touch(node_name, tinykube::now_ms());
            heartbeat_count++;
            
            std::cout << "💗 Heartbeat #" << heartbeat_count << " from " << node_name 
                      << " (client time: " << heartbeat.now_unix_ms() << "ms)" << std::endl;
        }
        
        std::cout << "💔 Heartbeat stream ended (received " << heartbeat_count 
                  << " heartbeats)" << std::endl;
        return Status::OK;
    }
    void monitor_nodes() {
        node_registry_.sweep(tinykube::now_ms(), HEARTBEAT_TIMEOUT_MS);
        
        auto nodes = node_registry_.snapshot();
        
        // Print the beautiful table
        print_node_table(nodes);
    }
private:
    tinykube::NodeRegistry node_registry_;
};

void signal_handler(int signal) {
    std::cout << "\n🛑 Received signal " << signal << ", shutting down gracefully..." << std::endl;
    g_running.store(false);
    if (g_server) {
        g_server->Shutdown();
    }
}

int main() {
    std::string server_address("0.0.0.0:50051");
    ControlPlaneServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    
    g_server = builder.BuildAndStart();
    
    std::cout << "🚀 TinyKube Control Plane server listening on " << server_address << std::endl;
    std::cout << "📡 Ready to accept node registrations and heartbeats!" << std::endl;
    std::cout << "🛑 Press Ctrl+C to stop" << std::endl;
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    std::thread server_thread([&]{ g_server->Wait(); });
    std::thread monitor([&]{
        int monitor_cycle = 0;
        while (g_running.load()) {
            monitor_cycle++;
            std::cout << "\n🔍 Cluster Health Check #" << monitor_cycle 
                      << " (" << tinykube::now_ms() << ")" << std::endl;
            service.monitor_nodes();
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    });

    server_thread.join();
    monitor.join();
    
    std::cout << "👋 Server shutdown complete" << std::endl;
    return 0;
}