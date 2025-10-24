#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>

#include "control_plane.grpc.pb.h"
#include "control_plane.pb.h"

#include "tinykube/node_registry.hpp"
#include "tinykube/time.hpp"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::Status;

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
private:
    tinykube::NodeRegistry node_registry_;
};

int main() {
    std::string server_address("0.0.0.0:50051");
    ControlPlaneServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    
    std::unique_ptr<Server> server(builder.BuildAndStart());
    
    std::cout << "🚀 TinyKube Control Plane server listening on " << server_address << std::endl;
    std::cout << "📡 Ready to accept node registrations and heartbeats!" << std::endl;
    std::cout << "🛑 Press Ctrl+C to stop" << std::endl;

    server->Wait();
    return 0;
}