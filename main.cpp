#include "server/server.hpp"
#include <iostream>
#include <cstdlib>
#include <signal.h>

Server* g_server = NULL; // Global pointer for signal handling

void signalHandler(int signum) {
    std::cout << "\nSignal (" << signum << ") received. Shutting down server..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
    exit(signum);
}

int main(int argc, char* argv[]) {
    // Check arguments
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <port> <password>" << std::endl;
        return 1;
    }
    
    // Parse port number
    int port;
    try {
        port = std::atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            throw std::out_of_range("Port must be between 1 and 65535");
        }
    } catch (const std::exception& e) {
        std::cerr << "Invalid port number: " << e.what() << std::endl;
        return 1;
    }
    
    // Get password
    std::string password = argv[2];
    
    // Set up signal handling for clean shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Create and set up server
    try {
        Server server(port, password);
        g_server = &server;
        
        if (!server.setup()) {
            std::cerr << "Failed to set up server" << std::endl;
            return 1;
        }
        
        std::cout << "Server started on port " << port << std::endl;
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}