#ifndef SERVER_HPP
#define SERVER_HPP

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>

class Client;
class Channel;

class Server {
private:
    int                     _serverSocket;      // Server socket fd
    struct sockaddr_in      _serverAddr;        // Server address
    unsigned int            _port;              // Server port
    std::string             _password;          // Connection password
    std::vector<pollfd>     _pollfds;           // Collection of file descriptors for poll()
    std::map<int, Client*>  _clients;           // Connected clients (fd -> Client)
    std::map<std::string, Channel*> _channels;  // Available channels (name -> Channel)
     bool _disconnected;

public:
    Server(unsigned int port, const std::string& password);
    ~Server();

    void checkAndRemoveDisconnectedClients();
    // Prevent copying
    Server(const Server& other);
    Server& operator=(const Server& other);

    // Server operations
    bool setup();
    void run();
    void stop();

    // Client operations
    void addClient(int clientFd);
    void removeClient(int clientFd);
    Client* getClient(int clientFd);
    
    // Channel operations
    Channel* getChannel(const std::string& name);
    Channel* createChannel(const std::string& name, Client* creator);
    void removeChannel(const std::string& name);

    // Password verification
    bool checkPassword(const std::string& password) const;
    void handleNewConnection();
    void handleClientData(int clientFd);
    bool setNonBlocking(int fd);
    bool isDisconnected() const {
        return _disconnected;
    }
    
    void setDisconnected() {
        _disconnected = true;
    }
    // Helper functions
};

#endif // SERVER_HPP


#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include <vector>
#include <map>
#include <queue>

class Server;
class Channel;
// Add in Client class declaration, at the beginning of the class:

class Client {
// friend class Server; // Add this line to allow Server to access private members
private:
    int _fd;                    // Client socket file descriptor
    Server* _server;            // Reference to server
    std::string _nickname;      // Client nickname
    std::string _username;      // Client username
    std::string _hostname;      // Client hostname
    std::string _buffer;        // Buffer for incoming data
    bool _authenticated;        // Whether client is authenticated
    std::vector<Channel*> _channels;  // Channels the client has joined
    bool _isOperator;           // Whether client is a server operator
    bool _disconnected;
    std::queue<std::string> _outgoingMessages; // For messages waiting to be sent

public:
    Client(int fd, Server* server);
    ~Client();

    // Getters
    int getFd() const;
    const std::string& getNickname() const;
    const std::string& getUsername() const;
    const std::string& getHostname() const;
    bool isAuthenticated() const;
    bool isOperator() const;

    // Setters
    void setNickname(const std::string& nickname);
    void setUsername(const std::string& username);
    void setHostname(const std::string& hostname);
    void setAuthenticated(bool authenticated);
    void setOperator(bool isOperator);

    // Channel operations
    void joinChannel(Channel* channel);
    void leaveChannel(Channel* channel);
    bool isInChannel(Channel* channel) const;
    const std::vector<Channel*>& getChannels() const;

    // Data handling
    bool receiveData();
    void sendData(const std::string& message);
    void sendPendingData();
    bool isDisconnected() const {
        return _disconnected;
    }
    
    void setDisconnected() {
        _disconnected = true;
    }

private:
    // Process received data
    void processData();
    // Handle different IRC commands
    void handleCommand(const std::string& command, const std::vector<std::string>& params);
    void parseAndHandleCommand(const std::string& line);

};

#endif // CLIENT_HPP


#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <string>
#include <vector>
#include <set>

class Client;

class Channel {
private:
    std::string _name;                // Channel name
    std::string _topic;               // Channel topic
    std::string _password;            // Channel password (for mode +k)
    std::vector<Client*> _clients;    // Clients in the channel
    std::set<Client*> _operators;     // Channel operators
    unsigned int _userLimit;          // User limit (for mode +l)
    bool _inviteOnly;                 // Invite-only flag (mode +i)
    bool _topicRestricted;            // Topic restricted to operators (mode +t)
    std::set<Client*> _invitedUsers;  // Users invited to the channel

public:
    Channel(const std::string& name, Client* creator);
    ~Channel();

    // Getters
    const std::string& getName() const;
    const std::string& getTopic() const;
    const std::string& getPassword() const;
    unsigned int getUserLimit() const;
    bool isInviteOnly() const;
    bool isTopicRestricted() const;

    // Client operations
    void addClient(Client* client);
    void removeClient(Client* client);
    bool hasClient(Client* client) const;
    const std::vector<Client*>& getClients() const;

    // Operator operations
    void addOperator(Client* client);
    void removeOperator(Client* client);
    bool isOperator(Client* client) const;

    // Invite operations
    void addInvite(Client* client);
    bool isInvited(Client* client) const;

    // Mode operations
    void setTopic(const std::string& topic);
    void setPassword(const std::string& password);
    void setUserLimit(unsigned int limit);
    void setInviteOnly(bool inviteOnly);
    void setTopicRestricted(bool restricted);

    // Message operations
    void broadcastMessage(const std::string& message);
    void broadcastMessage(const std::string& message, Client* except);
    
    // Utility functions
    std::string getNamesList() const;
    std::string getModeString() const;
};

#endif // CHANNEL_HPP

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

// Add to Client class:
void Client::sendPendingData() {
    while (!_outgoingMessages.empty()) {
        std::string& message = _outgoingMessages.front();
        
        ssize_t bytesSent = send(_fd, message.c_str(), message.size(), 0);
        
        if (bytesSent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Would block, try again later
                return;
            }
            // Error sending data
            std::cerr << "Error sending data: " << strerror(errno) << std::endl;
            setDisconnected();
            return;
        }
        else if (static_cast<size_t>(bytesSent) < message.size()) {
            // Partial send - keep remaining data
            message = message.substr(bytesSent);
            return;
        }
        else {
            // Full message sent
            _outgoingMessages.pop();
        }
    }
}

// Modify Server::run() to include checking for disconnected clients
void Server::run() {
    while (true) {
        // Wait for activity on sockets
        int activity = poll(&_pollfds[0], _pollfds.size(), -1);
        
        if (activity < 0) {
            if (errno == EINTR) {
                // Interrupted by signal, just continue
                continue;
            }
            std::cerr << "Poll error: " << strerror(errno) << std::endl;
            break;
        }
        
        // Process server socket (new connections)
        if (_pollfds[0].revents & POLLIN) {
            handleNewConnection();
        }
        
        // Process client sockets
        for (size_t i = 1; i < _pollfds.size(); i++) {
            int fd = _pollfds[i].fd;
            
            // Check for errors or hangup
            if (_pollfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                Client* client = getClient(fd);
                if (client) {
                    client->setDisconnected();
                }
                continue;
            }
            
            // Check for data to read
            if (_pollfds[i].revents & POLLIN) {
                handleClientData(fd);
            }
            
            // Check for ability to write (if client has pending data)
            if (_pollfds[i].revents & POLLOUT) {
                Client* client = getClient(fd);
                if (client) {
                    client->sendPendingData();
                }
            }
        }
        
        // Check and cleanup disconnected clients
        checkAndRemoveDisconnectedClients();
    }
}

void Server::stop() {
    // Close server socket
    if (_serverSocket != -1) {
        close(_serverSocket);
        _serverSocket = -1;
    }
}

void Server::addClient(int clientFd) {
    // Create new client and add to map
    Client* client = new Client(clientFd, this);
    _clients[clientFd] = client;
    
    // Add to poll array
    pollfd clientPollFd;
    clientPollFd.fd = clientFd;
    clientPollFd.events = POLLIN;
    _pollfds.push_back(clientPollFd);
}

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


#include "client.hpp"
#include "../server/server.hpp"
#include "../channels/channels.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <cstring>


// Utility function to split a string by delimiters
std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

Client::Client(int fd, Server* server)
        : _fd(fd), _server(server), _authenticated(false), _isOperator(false), _disconnected(false) {
    }
    

Client::~Client() {
    // Leave all channels
    std::vector<Channel*> channelsCopy = _channels;
    for (std::vector<Channel*>::iterator it = channelsCopy.begin(); it != channelsCopy.end(); ++it) {
        leaveChannel(*it);
    }
}

int Client::getFd() const {
    return _fd;
}

const std::string& Client::getNickname() const {
    return _nickname;
}

const std::string& Client::getUsername() const {
    return _username;
}

const std::string& Client::getHostname() const {
    return _hostname;
}

bool Client::isAuthenticated() const {
    return _authenticated;
}

bool Client::isOperator() const {
    return _isOperator;
}

void Client::setNickname(const std::string& nickname) {
    _nickname = nickname;
}

void Client::setUsername(const std::string& username) {
    _username = username;
}

void Client::setHostname(const std::string& hostname) {
    _hostname = hostname;
}

void Client::setAuthenticated(bool authenticated) {
    _authenticated = authenticated;
}

void Client::setOperator(bool isOperator) {
    _isOperator = isOperator;
}

void Client::joinChannel(Channel* channel) {
    if (!isInChannel(channel)) {
        _channels.push_back(channel);
        channel->addClient(this);
    }
}

void Client::leaveChannel(Channel* channel) {
    std::vector<Channel*>::iterator it = std::find(_channels.begin(), _channels.end(), channel);
    if (it != _channels.end()) {
        _channels.erase(it);
        channel->removeClient(this);
    }
}

bool Client::isInChannel(Channel* channel) const {
    return std::find(_channels.begin(), _channels.end(), channel) != _channels.end();
}

const std::vector<Channel*>& Client::getChannels() const {
    return _channels;
}

bool Client::receiveData() {
    char buffer[4096]; // Larger buffer
    ssize_t bytesRead = recv(_fd, buffer, sizeof(buffer) - 1, 0);
    
    if (bytesRead <= 0) {
        if (bytesRead == 0) {
            // Connection closed
            std::cout << "Client " << _nickname << " disconnected" << std::endl;
        } else {
            // Error reading
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true; // Not an error, just no data available
            }
            std::cerr << "Error receiving data: " << strerror(errno) << std::endl;
        }
        return false;
    }
    
    buffer[bytesRead] = '\0';
    _buffer += buffer;
    
    // In receiveData():
    std::cout << "Received data: " << buffer << std::endl;
    
    // Process complete commands
    processData();
    
    return true;
}

void Client::sendData(const std::string& message) {
    std::string fullMessage = message + "\r\n";
    // In Client::sendData()
    ssize_t bytesSent = send(_fd, fullMessage.c_str(), fullMessage.size(), 0);
    // std::cout << "Sending: " << fullMessage << " (Bytes: " << bytesSent << ")" << std::endl;

    if (bytesSent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Would block, need to buffer this message for later sending
        // Add to outgoing message queue (implement this)
        // To this:
        _outgoingMessages.push(fullMessage);
        return;
    }
    std::cerr << "Error sending data to client: " << strerror(errno) << std::endl;
    // Consider flagging client for disconnection
    return;
    }
    else if (static_cast<size_t>(bytesSent) < fullMessage.size()) {
    // Partial send - buffer remaining data
    _outgoingMessages.push(fullMessage.substr(bytesSent));
    // In parseAndHandleCommand() after extracting the command:
}
}

void Client::parseAndHandleCommand(const std::string& line) {
    // Parse command
    std::string prefix = "";
    std::string command;
    std::vector<std::string> params;
    
    // Extract prefix if present
    size_t start = 0;
    if (line[0] == ':') {
        size_t prefixEnd = line.find(' ');
        if (prefixEnd == std::string::npos) {
            return; // Invalid format
        }
        prefix = line.substr(1, prefixEnd - 1);
        start = prefixEnd + 1;
        while (start < line.size() && line[start] == ' ') {
            start++;
        }
    }
    
    // Extract command
    size_t cmdEnd = line.find(' ', start);
    if (cmdEnd == std::string::npos) {
        command = line.substr(start);
    } else {
        command = line.substr(start, cmdEnd - start);
        // std::cout << "Processing command: " << command << std::endl;
        
        // Extract parameters
        size_t paramStart = cmdEnd + 1;
        while (paramStart < line.size() && line[paramStart] == ' ') {
            paramStart++;
        }
        
        // Handle trailing parameter (starts with :)
        size_t trailingStart = line.find(" :", paramStart);
        if (trailingStart != std::string::npos) {
            // Extract space-separated parameters before the trailing parameter
            std::string normalParams = line.substr(paramStart, trailingStart - paramStart);
            if (!normalParams.empty()) {
                std::istringstream iss(normalParams);
                std::string param;
                while (iss >> param) {
                    params.push_back(param);
                }
            }
            
            // Add trailing parameter
            params.push_back(line.substr(trailingStart + 2));
        } else {
            // Extract all space-separated parameters
            std::istringstream iss(line.substr(paramStart));
            std::string param;
            while (iss >> param) {
                params.push_back(param);
            }
        }
        
        // In parseAndHandleCommand():
    
    }
    
    // Convert command to uppercase for case-insensitive comparison
    std::transform(command.begin(), command.end(), command.begin(), ::toupper);
    
    // Handle the command
    handleCommand(command, params);
}

void Client::processData() {
    size_t pos;
    // Process as many complete commands as possible
    while ((pos = _buffer.find("\r\n")) != std::string::npos) {
        std::string line = _buffer.substr(0, pos);
        _buffer.erase(0, pos + 2); // Remove processed command
        
        // Skip empty lines
        if (line.empty()) {
            continue;
        }
        
        // Parse and handle command
        parseAndHandleCommand(line);
    }
    
    // Handle buffer overflow protection - if buffer gets too large without
    // finding a \r\n, the client might be sending malformed data
    if (_buffer.size() > 8192) { // 8KB limit
        sendData("ERROR :Client exceeded buffer size limit");
        _buffer.clear();
    }
}

void Client::handleCommand(const std::string& command, const std::vector<std::string>& params) {
    // This is where you'd implement handling of different IRC commands
    // For now, just print the command and parameters
    bool _passwordValidated = false;
    std::cout << "Command: " << command << std::endl;
    std::cout << "Parameters: ";
    for (size_t i = 0; i < params.size(); i++) {
        std::cout << "[" << params[i] << "] ";
    }
    std::cout << std::endl;
    
    // Basic command handling examples:
    if (command == "PASS") {
        // Password authentication
        if (_server->checkPassword(params[0])) {
            _passwordValidated = true;
            std::cout << "hello\n";
            // They need to provide NICK and USER as well
        } else {
            sendData("464 :Password incorrect");
        }
        if (params.size() < 1) {
            sendData("461 " + _nickname + " PASS :Not enough parameters");
            return;
        }
        
            // Password accepted, but client is not fully registered yet
    }
    // Add this to Client::handleCommand
    else if (command == "TOPIC") {
        if (params.size() < 1) {
            sendData("461 " + _nickname + " TOPIC :Not enough parameters");
        return;
    }
    
    std::string channelName = params[0];
    Channel* channel = _server->getChannel(channelName);
    
    if (!channel) {
        sendData("403 " + _nickname + " " + channelName + " :No such channel");
        return;
    }
    
    // Check if client is in the channel
    if (!isInChannel(channel)) {
        sendData("442 " + _nickname + " " + channelName + " :You're not on that channel");
        return;
    }
    
    // If no second parameter, return the topic
    if (params.size() == 1) {
        const std::string& topic = channel->getTopic();
        if (!topic.empty()) {
            sendData("332 " + _nickname + " " + channelName + " :" + topic);
        } else {
            sendData("331 " + _nickname + " " + channelName + " :No topic is set");
        }
        return;
    }
    
    // Changing the topic - check if allowed
    if (channel->isTopicRestricted() && !channel->isOperator(this)) {
        sendData("482 " + _nickname + " " + channelName + " :You're not channel operator");
        return;
    }
    
    // Set the new topic
    channel->setTopic(params[1]);
    
    // Notify all clients in the channel
    channel->broadcastMessage(":" + _nickname + "!" + _username + "@host TOPIC " + channelName + " :" + params[1]);
}
    else if (command == "NICK") {
        // Set nickname
        if (params.size() < 1) {
            sendData("431 :No nickname given");
            return;
        }
        
        std::string newNick = params[0];
        // Check if nickname is valid and not already taken
        // For simplicity, we're not implementing these checks here
        
        _nickname = newNick;
        sendData(":" + _nickname + " NICK :" + _nickname);
    }
    else if (command == "USER") {
        // Set username and realname
        if (params.size() < 4) {
            sendData("461 " + _nickname + " USER :Not enough parameters");
            return;
        }
        
        _username = params[0];
        // params[1] and params[2] are mode and unused
        // params[3] is the real name
        

        // If we have both nickname and username, complete registration
        if (!_nickname.empty() && !_username.empty() && _passwordValidated) {
            _authenticated = true;
            std::cout << "HELLO\n";
            // Send welcome messages
            sendData("001 " + _nickname + " :Welcome to the Internet Relay Network " + _nickname + "!" + _username + "@host");
            sendData("002 " + _nickname + " :Your host is ft_irc, running version 1.0");
            sendData("003 " + _nickname + " :This server was created today");
            sendData("004 " + _nickname + " ft_irc 1.0 o o");
        }
    }
    else if (command == "JOIN") {
        // Join a channel
        if (params.size() < 1) {
            sendData("461 " + _nickname + " JOIN :Not enough parameters");
            return;
        }
        
        std::string channelName = params[0];
        
        // Check if channel name is valid
        if (channelName[0] != '#') {
            sendData("403 " + _nickname + " " + channelName + " :No such channel");
            return;
        }
        
        // Get or create channel
        Channel* channel = _server->getChannel(channelName);
        if (!channel) {
            channel = _server->createChannel(channelName, this);
        }
        
        // Join channel
        joinChannel(channel);
        
        // Notify clients in channel
        channel->broadcastMessage(":" + _nickname + "!" + _username + "@host JOIN " + channelName);
        
        // Send channel topic
        const std::string& topic = channel->getTopic();
        if (!topic.empty()) {
            sendData("332 " + _nickname + " " + channelName + " :" + topic);
        } else {
            sendData("331 " + _nickname + " " + channelName + " :No topic is set");
        }
        
        // Send names list
        sendData("353 " + _nickname + " = " + channelName + " :" + channel->getNamesList());
        sendData("366 " + _nickname + " " + channelName + " :End of /NAMES list");
    }
    // // In handleCommand() for each command handler:
    // std::cout << "Handling PASS command" << std::endl;
    // Add more command handlers here (PRIVMSG, PART, QUIT, etc.)

}
#include "channels.hpp"
#include "../client/client.hpp"
#include <algorithm>
#include <sstream>

Channel::Channel(const std::string& name, Client* creator)
    : _name(name), _userLimit(0), _inviteOnly(false), _topicRestricted(true) {
    // Add creator as first client and operator
    addClient(creator);
    addOperator(creator);
}

Channel::~Channel() {
    // Remove all clients from the channel
    std::vector<Client*> clientsCopy = _clients;
    for (std::vector<Client*>::iterator it = clientsCopy.begin(); it != clientsCopy.end(); ++it) {
        (*it)->leaveChannel(this);
    }
    _clients.clear();
    _operators.clear();
    _invitedUsers.clear();
}

const std::string& Channel::getName() const {
    return _name;
}

const std::string& Channel::getTopic() const {
    return _topic;
}

const std::string& Channel::getPassword() const {
    return _password;
}

unsigned int Channel::getUserLimit() const {
    return _userLimit;
}

bool Channel::isInviteOnly() const {
    return _inviteOnly;
}

bool Channel::isTopicRestricted() const {
    return _topicRestricted;
}

void Channel::addClient(Client* client) {
    if (!hasClient(client)) {
        _clients.push_back(client);
    }
}

void Channel::removeClient(Client* client) {
    std::vector<Client*>::iterator it = std::find(_clients.begin(), _clients.end(), client);
    if (it != _clients.end()) {
        _clients.erase(it);
    }
    
    // Also remove from operators if they were one
    removeOperator(client);
    
    // If channel is empty, it should be deleted
    // This will be handled by the Server class
}

bool Channel::hasClient(Client* client) const {
    return std::find(_clients.begin(), _clients.end(), client) != _clients.end();
}

const std::vector<Client*>& Channel::getClients() const {
    return _clients;
}

void Channel::addOperator(Client* client) {
    // Only add as operator if they are in the channel
    if (hasClient(client)) {
        _operators.insert(client);
    }
}

void Channel::removeOperator(Client* client) {
    _operators.erase(client);
}

bool Channel::isOperator(Client* client) const {
    return _operators.find(client) != _operators.end();
}

void Channel::addInvite(Client* client) {
    _invitedUsers.insert(client);
}

bool Channel::isInvited(Client* client) const {
    return _invitedUsers.find(client) != _invitedUsers.end();
}

void Channel::setTopic(const std::string& topic) {
    _topic = topic;
}

void Channel::setPassword(const std::string& password) {
    _password = password;
}

void Channel::setUserLimit(unsigned int limit) {
    _userLimit = limit;
}

void Channel::setInviteOnly(bool inviteOnly) {
    _inviteOnly = inviteOnly;
}

void Channel::setTopicRestricted(bool restricted) {
    _topicRestricted = restricted;
}

void Channel::broadcastMessage(const std::string& message) {
    for (std::vector<Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
        (*it)->sendData(message);
    }
}

void Channel::broadcastMessage(const std::string& message, Client* except) {
    for (std::vector<Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
        if (*it != except) {
            (*it)->sendData(message);
        }
    }
}

std::string Channel::getNamesList() const {
    std::stringstream ss;
    
    for (std::vector<Client*>::const_iterator it = _clients.begin(); it != _clients.end(); ++it) {
        // Add @ for operators
        if (isOperator(*it)) {
            ss << "@";
        }
        
        ss << (*it)->getNickname() << " ";
    }
    
    return ss.str();
}

std::string Channel::getModeString() const {
    std::string modes = "+";
    std::string params = "";
    
    if (_inviteOnly) {
        modes += "i";
    }
    
    if (_topicRestricted) {
        modes += "t";
    }
    
    if (!_password.empty()) {
        modes += "k";
        params += " " + _password;
    }
    
    if (_userLimit > 0) {
        modes += "l";
        std::stringstream ss;
        ss << _userLimit;
        params += " " + ss.str();
    }
    
    return modes + params;
}
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