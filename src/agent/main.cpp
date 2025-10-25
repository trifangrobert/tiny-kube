#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <chrono>
#include <grpcpp/grpcpp.h>
#include "control_plane.grpc.pb.h"
#include "control_plane.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientWriter;
using grpc::Status;

class TinyKubeAgent {
private:
    std::unique_ptr<tinykube::ControlPlane::Stub> stub_;
    std::string node_name_;

public:
    TinyKubeAgent(std::shared_ptr<Channel> channel, const std::string& node_name)
        : stub_(tinykube::ControlPlane::NewStub(channel)), node_name_(node_name) {}

    bool RegisterWithControlPlane() {
        tinykube::RegisterRequest request;
        request.mutable_node()->set_name(node_name_);
        
        tinykube::RegisterResponse response;
        ClientContext context;

        std::cout << "📋 Attempting to register node: " << node_name_ << std::endl;

        Status status = stub_->RegisterNode(&context, request, &response);

        if (status.ok()) {
            if (response.accepted()) {
                std::cout << "✅ Registration successful: " << response.reason() << std::endl;
                return true;
            } else {
                std::cout << "❌ Registration rejected: " << response.reason() << std::endl;
                return false;
            }
        } else {
            std::cout << "🚫 RPC failed: " << status.error_message() << std::endl;
            return false;
        }
    }

    void StartHeartbeats() {
        std::cout << "💓 Starting heartbeat stream..." << std::endl;

        ClientContext context;
        tinykube::Empty response;
        
        std::unique_ptr<ClientWriter<tinykube::Heartbeat>> writer(
            stub_->StreamHeartbeats(&context, &response));

        for (int i = 0; i < 10; ++i) {  // Send 10 heartbeats
            tinykube::Heartbeat heartbeat;
            heartbeat.set_node_name(node_name_);
            
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            heartbeat.set_now_unix_ms(now);

            if (!writer->Write(heartbeat)) {
                std::cout << "💔 Failed to send heartbeat" << std::endl;
                break;
            }
            
            std::cout << "💗 Sent heartbeat #" << (i+1) << " at " << now << "ms" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        writer->WritesDone();
        Status status = writer->Finish();
        
        if (status.ok()) {
            std::cout << "✅ Heartbeat stream completed successfully" << std::endl;
        } else {
            std::cout << "❌ Heartbeat stream failed: " << status.error_message() << std::endl;
        }
    }
};

void print_usage(const char* program_name) {
    std::cout << "🤖 TinyKube Agent - Node Registration & Heartbeat Client\n" << std::endl;
    std::cout << "Usage: " << program_name << " [OPTIONS]" << std::endl;
    std::cout << "\nOptions:" << std::endl;
    std::cout << "  -n, --node-name <name>    Node name for registration (required)" << std::endl;
    std::cout << "  -s, --server <address>    Control plane server address (default: localhost:50051)" << std::endl;
    std::cout << "  -h, --help                Show this help message" << std::endl;
    std::cout << "\nExamples:" << std::endl;
    std::cout << "  " << program_name << " --node-name worker-1" << std::endl;
    std::cout << "  " << program_name << " -n worker-2 -s 192.168.1.100:50051" << std::endl;
    std::cout << "  " << program_name << " --node-name control-node --server localhost:9090\n" << std::endl;
}

int main(int argc, char* argv[]) {
    std::string server_address("localhost:50051");
    std::string node_name;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        else if (arg == "-n" || arg == "--node-name") {
            if (i + 1 < argc) {
                node_name = argv[++i];
            } else {
                std::cerr << "❌ Error: --node-name requires a value" << std::endl;
                print_usage(argv[0]);
                return 1;
            }
        }
        else if (arg == "-s" || arg == "--server") {
            if (i + 1 < argc) {
                server_address = argv[++i];
            } else {
                std::cerr << "❌ Error: --server requires a value" << std::endl;
                print_usage(argv[0]);
                return 1;
            }
        }
        else {
            std::cerr << "❌ Error: Unknown argument '" << arg << "'" << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    // Validate required arguments
    if (node_name.empty()) {
        std::cerr << "❌ Error: Node name is required!" << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    std::cout << "🤖 TinyKube Agent starting..." << std::endl;
    std::cout << "📛 Node Name: " << node_name << std::endl;
    std::cout << "🎯 Control Plane: " << server_address << std::endl;

    // Create channel to server
    auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
    TinyKubeAgent agent(channel, node_name);

    // First register with control plane
    if (agent.RegisterWithControlPlane()) {
        std::cout << "🎉 Agent registered successfully, starting heartbeats..." << std::endl;
        
        // Start sending heartbeats
        agent.StartHeartbeats();
    } else {
        std::cout << "💥 Failed to register with control plane, exiting..." << std::endl;
        return 1;
    }

    std::cout << "👋 Agent shutting down..." << std::endl;
    return 0;
}