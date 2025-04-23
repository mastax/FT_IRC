#include "server.hpp"
#include "../client/client.hpp"
#include "../channels/channels.hpp"
#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

Server::Server(unsigned int port, const std::string& password)
    : _serverSocket(-1), _port(port), _password(password) {
}

Server::~Server() {
    // Clean up clients
    for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
        delete it->second;
    }
    _clients.clear();

    // Clean up channels
    for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ++it) {
        delete it->second;
    }
    _channels.clear();

    // Close server socket
    if (_serverSocket != -1) {
        close(_serverSocket);
    }
}

bool Server::setup() {
    // Create socket
    _serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (_serverSocket == -1) {
        std::cerr << "Error creating socket: " << strerror(errno) << std::endl;
    return false;
}

    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        std::cerr << "Error setting socket options" << std::endl;
        close(_serverSocket);
        return false;
    }

    // Set non-blocking mode
    if (!setNonBlocking(_serverSocket)) {
        std::cerr << "Error setting non-blocking mode" << std::endl;
        close(_serverSocket);
        return false;
    }

    // Prepare server address
    memset(&_serverAddr, 0, sizeof(_serverAddr));
    _serverAddr.sin_family = AF_INET;
    _serverAddr.sin_addr.s_addr = INADDR_ANY;
    _serverAddr.sin_port = htons(_port);

    // Bind socket to address
    if (bind(_serverSocket, (struct sockaddr*)&_serverAddr, sizeof(_serverAddr)) == -1) {
        std::cerr << "Error binding socket to port " << _port << std::endl;
        close(_serverSocket);
        return false;
    }

    // Start listening
    if (listen(_serverSocket, 10) == -1) {
        std::cerr << "Error listening on socket" << std::endl;
        close(_serverSocket);
        return false;
    }

    // Add server socket to poll list
    pollfd serverPollFd;
    serverPollFd.fd = _serverSocket;
    serverPollFd.events = POLLIN;
    _pollfds.push_back(serverPollFd);

    std::cout << "Server is listening on port " << _port << std::endl;
    return true;
}

void Server::addClient(int clientFd) {
    // Create new client and add to map
    Client* client = new Client(clientFd, this);
    _clients[clientFd] = client;
    
    // Record connection time
    _connectionTimes[clientFd] = time(NULL);
    
    // Add to poll array
    pollfd clientPollFd;
    clientPollFd.fd = clientFd;
    clientPollFd.events = POLLIN;
    _pollfds.push_back(clientPollFd);
}

void Server::checkTimeouts() {
    time_t now = time(NULL);
    std::vector<int> toRemove;
    
    for (std::map<int, time_t>::iterator it = _connectionTimes.begin(); 
         it != _connectionTimes.end(); ++it) {
        Client* client = getClient(it->first);
        if (client && !client->isAuthenticated() && (now - it->second) > 60) {
            // 60 seconds to complete registration
            toRemove.push_back(it->first);
        }
    }
    
    for (size_t i = 0; i < toRemove.size(); i++) {
        Client* client = getClient(toRemove[i]);
        if (client) {
            client->sendData("ERROR :Registration timeout");
        }
        removeClient(toRemove[i]);
    }
}

// In Server class, add a method to check and remove disconnected clients
void Server::checkAndRemoveDisconnectedClients() {
    std::vector<int> clientsToRemove;
    
    // First, identify clients that need to be removed
    for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
        if (it->second->isDisconnected()) {
            clientsToRemove.push_back(it->first);
        }
    }
    
    // Then remove them
    for (size_t i = 0; i < clientsToRemove.size(); i++) {
        removeClient(clientsToRemove[i]);
    }
}

void Server::run() {
    while (true) {
        // Add this block right before poll() to set POLLOUT when needed
        for (size_t i = 1; i < _pollfds.size(); i++) {
            Client* client = getClient(_pollfds[i].fd);
            if (client && client->hasPendingMessages()) {
                _pollfds[i].events = POLLIN | POLLOUT;  // Enable write notifications
            } else {
                _pollfds[i].events = POLLIN;  // Only interested in reads
            }
        }

        // Original poll() call remains here
        int activity = poll(&_pollfds[0], _pollfds.size(), -1);
        
        if (activity < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "Poll error: " << strerror(errno) << std::endl;
            break;
        }
        
        // Rest of your existing code remains exactly the same
        if (_pollfds[0].revents & POLLIN) {
            handleNewConnection();
        }
        
        for (size_t i = 1; i < _pollfds.size(); i++) {
            int fd = _pollfds[i].fd;
            
            if (_pollfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                Client* client = getClient(fd);
                if (client) {
                    client->setDisconnected();
                }
                continue;
            }
            
            if (_pollfds[i].revents & POLLIN) {
                handleClientData(fd);
            }
            
            if (_pollfds[i].revents & POLLOUT) {
                Client* client = getClient(fd);
                if (client) {
                    client->sendPendingData();
                }
            }
        }
        
        checkAndRemoveDisconnectedClients();
    }
}

// // Modify Server::run() to include checking for disconnected clients
// void Server::run() {
//     while (true) {
//         // Wait for activity on sockets
//         int activity = poll(&_pollfds[0], _pollfds.size(), -1);
        
//         if (activity < 0) {
//             if (errno == EINTR) {
//                 // Interrupted by signal, just continue
//                 continue;
//             }
//             std::cerr << "Poll error: " << strerror(errno) << std::endl;
//             break;
//         }
        
//         // Process server socket (new connections)
//         if (_pollfds[0].revents & POLLIN) {
//             handleNewConnection();
//         }
        
//         // Process client sockets
//         for (size_t i = 1; i < _pollfds.size(); i++) {
//             int fd = _pollfds[i].fd;
            
//             // Check for errors or hangup
//             if (_pollfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
//                 Client* client = getClient(fd);
//                 if (client) {
//                     client->setDisconnected();
//                 }
//                 continue;
//             }
            
//             // Check for data to read
//             if (_pollfds[i].revents & POLLIN) {
//                 handleClientData(fd);
//             }
            
//             // Check for ability to write (if client has pending data)
//             if (_pollfds[i].revents & POLLOUT) {
//                 Client* client = getClient(fd);
//                 if (client) {
//                     client->sendPendingData();
//                 }
//             }
//         }
        
//         // Check and cleanup disconnected clients
//         checkAndRemoveDisconnectedClients();
//     }
// }

void Server::stop() {
    // Close server socket
    if (_serverSocket != -1) {
        close(_serverSocket);
        _serverSocket = -1;
    }
}

// void Server::addClient(int clientFd) {
//     // Create new client and add to map
//     Client* client = new Client(clientFd, this);
//     _clients[clientFd] = client;
    
//     // Add to poll array
//     pollfd clientPollFd;
//     clientPollFd.fd = clientFd;
//     clientPollFd.events = POLLIN;
//     _pollfds.push_back(clientPollFd);
// }

void Server::removeClient(int clientFd) {
    std::map<int, Client*>::iterator it = _clients.find(clientFd);
    if (it != _clients.end()) {
        // Get client's channels before deletion
        const std::vector<Channel*>& channels = it->second->getChannels();
        
        // Make a copy since the vector will be modified during channel operations
        std::vector<Channel*> channelsCopy(channels.begin(), channels.end());
        
        // Notify each channel about the client leaving
        for (std::vector<Channel*>::iterator chanIt = channelsCopy.begin(); 
             chanIt != channelsCopy.end(); ++chanIt) {
            
            Channel* channel = *chanIt;
            // Broadcast quit message to channel members
            channel->broadcastMessage(":" + it->second->getNickname() + 
                                   "!" + it->second->getUsername() + 
                                   "@host QUIT :Connection closed");
            
            // Remove client from channel
            it->second->leaveChannel(channel);
            
            // If channel becomes empty, remove it
            if (channel->getClients().empty()) {
                removeChannel(channel->getName());
            }
        }
        
        // Delete client object
        delete it->second;
        _clients.erase(it);
    }
    
    // Close socket
    close(clientFd);
    
    // Remove from poll array
    for (size_t i = 0; i < _pollfds.size(); i++) {
        if (_pollfds[i].fd == clientFd) {
            _pollfds.erase(_pollfds.begin() + i);
            break;
        }
    }
}

Client* Server::getClient(int clientFd) {
    std::map<int, Client*>::iterator it = _clients.find(clientFd);
    if (it != _clients.end()) {
        return it->second;
    }
    return NULL;
}

Channel* Server::getChannel(const std::string& name) {
    std::map<std::string, Channel*>::iterator it = _channels.find(name);
    if (it != _channels.end()) {
        return it->second;
    }
    return NULL;
}

Channel* Server::createChannel(const std::string& name, Client* creator) {
    // Check if channel already exists
    if (_channels.find(name) != _channels.end()) {
        return _channels[name];
    }
    
    // Create new channel
    Channel* channel = new Channel(name, creator);
    _channels[name] = channel;
    return channel;
}

void Server::removeChannel(const std::string& name) {
    std::map<std::string, Channel*>::iterator it = _channels.find(name);
    if (it != _channels.end()) {
        delete it->second;
        _channels.erase(it);
    }
}

bool Server::checkPassword(const std::string& password) const {
    return password == _password;
}

void Server::handleNewConnection() {
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    
    // Accept new connection
    int clientFd = accept(_serverSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
    
    if (clientFd < 0) {
    // Handle EAGAIN/EWOULDBLOCK separately as they're expected for non-blocking sockets
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return; // No connections available, not an error
    }
    std::cerr << "Error accepting connection: " << strerror(errno) << std::endl;
    return;
}
    
    // Set non-blocking mode
    if (!setNonBlocking(clientFd)) {
        std::cerr << "Error setting client socket to non-blocking mode" << std::endl;
        close(clientFd);
        return;
    }
    
    std::cout << "New connection accepted" << std::endl;
    
    // Add new client
    addClient(clientFd);
}

void Server::handleClientData(int clientFd) {
    Client* client = getClient(clientFd);
    if (!client) {
        std::cerr << "Client not found" << std::endl;
        return;
    }
    
    // Let client handle incoming data
    if (!client->receiveData()) {
        // Client disconnected or error
        removeClient(clientFd);
        
        // Remove from poll array
        for (size_t i = 0; i < _pollfds.size(); i++) {
            if (_pollfds[i].fd == clientFd) {
                _pollfds.erase(_pollfds.begin() + i);
                break;
            }
        }
    }
}

bool Server::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}