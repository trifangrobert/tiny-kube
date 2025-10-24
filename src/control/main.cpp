#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "control_plane.grpc.pb.h"
#include "control_plane.pb.h"

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
        
        std::cout << "📋 Node registration request received from: " 
                  << request->node().name() << std::endl;
        
        // For now, accept all nodes
        response->set_accepted(true);
        response->set_reason("Welcome to TinyKube cluster!");
        
        std::cout << "✅ Node " << request->node().name() << " registered successfully" << std::endl;
        
        return Status::OK;
    }
    
    Status StreamHeartbeats(ServerContext* context,
                           ServerReader<tinykube::Heartbeat>* reader,
                           tinykube::Empty* response) override {
        
        std::cout << "💓 Starting heartbeat stream..." << std::endl;
        
        tinykube::Heartbeat heartbeat;
        while (reader->Read(&heartbeat)) {
            std::cout << "💗 Heartbeat from " << heartbeat.node_name() 
                      << " at timestamp " << heartbeat.now_unix_ms() << "ms" << std::endl;
        }
        
        std::cout << "💔 Heartbeat stream ended" << std::endl;
        return Status::OK;
    }
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