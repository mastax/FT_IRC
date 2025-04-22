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

public:
    Server(unsigned int port, const std::string& password);
    ~Server();

    // Prevent copying
    Server(const Server& other) = delete;
    Server& operator=(const Server& other) = delete;

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

private:
    // Helper functions
    void handleNewConnection();
    void handleClientData(int clientFd);
    bool setNonBlocking(int fd);
};

#endif // SERVER_HPP


#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include <vector>
#include <map>

class Server;
class Channel;

class Client {
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

private:
    // Process received data
    void processData();
    // Handle different IRC commands
    void handleCommand(const std::string& command, const std::vector<std::string>& params);
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
        std::cerr << "Error creating socket" << std::endl;
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

void Server::run() {
    while (true) {
        // Wait for activity on one of the sockets (timeout -1 means wait indefinitely)
        int activity = poll(&_pollfds[0], _pollfds.size(), -1);
        
        if (activity < 0) {
            std::cerr << "Poll error" << std::endl;
            break;
        }
        
        // Check if there's activity on the server socket (new connection)
        if (_pollfds[0].revents & POLLIN) {
            handleNewConnection();
        }
        
        // Check client sockets for activity
        for (size_t i = 1; i < _pollfds.size(); i++) {
            if (_pollfds[i].revents & POLLIN) {
                // Data available to read
                handleClientData(_pollfds[i].fd);
            }
            
            if (_pollfds[i].revents & POLLHUP) {
                // Client disconnected
                std::cout << "Client disconnected" << std::endl;
                removeClient(_pollfds[i].fd);

                // Remove from poll array
                _pollfds.erase(_pollfds.begin() + i);
                i--; // Adjust index after removal
            }
        }
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
    // Find client in map
    std::map<int, Client*>::iterator it = _clients.find(clientFd);
    if (it != _clients.end()) {
        delete it->second;
        _clients.erase(it);
    }
    
    // Close socket
    close(clientFd);
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
        std::cerr << "Error accepting connection" << std::endl;
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
// #include "Channel.hpp"
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
    : _fd(fd), _server(server), _authenticated(false), _isOperator(false) {
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
    char buffer[1024];
    ssize_t bytesRead = recv(_fd, buffer, sizeof(buffer) - 1, 0);
    
    if (bytesRead <= 0) {
        if (bytesRead == 0) {
            // Connection closed
            std::cout << "Client disconnected" << std::endl;
        } else {
            // Error
            std::cerr << "Error receiving data: " << strerror(errno) << std::endl;
        }
        return false;
    }
    
    buffer[bytesRead] = '\0';
    _buffer += buffer;
    
    // Process commands that end with \r\n
    size_t pos;
    while ((pos = _buffer.find("\r\n")) != std::string::npos) {
        std::string command = _buffer.substr(0, pos);
        _buffer.erase(0, pos + 2); // Remove command and \r\n from buffer
        
        std::cout << "Received command: " << command << std::endl;
        processData();
    }
    
    return true;
}

void Client::sendData(const std::string& message) {
    std::string fullMessage = message + "\r\n";
    ssize_t bytesSent = send(_fd, fullMessage.c_str(), fullMessage.size(), 0);
    
    if (bytesSent < 0) {
        std::cerr << "Error sending data to client" << std::endl;
    }
}

void Client::processData() {
    // Example of processing IRC commands
    // This is simplified - you'd need to implement RFC 1459 properly
    
    size_t pos = _buffer.find("\r\n");
    if (pos == std::string::npos) {
        return; // No complete command yet
    }
    
    std::string line = _buffer.substr(0, pos);
    _buffer.erase(0, pos + 2); // Remove processed command
    
    // Skip empty lines
    if (line.empty()) {
        return;
    }
    
    // Parse command
    std::string command;
    std::vector<std::string> params;
    
    if (line[0] == ':') {
        // Command with prefix (we'll ignore the prefix for now)
        size_t spacePos = line.find(' ');
        if (spacePos == std::string::npos) {
            return; // Invalid format
        }
        
        line = line.substr(spacePos + 1);
    }
    
    // Extract command
    size_t spacePos = line.find(' ');
    if (spacePos == std::string::npos) {
        command = line;
    } else {
        command = line.substr(0, spacePos);
        
        // Extract parameters
        std::string paramsStr = line.substr(spacePos + 1);
        
        // Handle trailing parameter (starts with :)
        size_t colonPos = paramsStr.find(" :");
        if (colonPos != std::string::npos) {
            std::string normalParams = paramsStr.substr(0, colonPos);
            std::string trailingParam = paramsStr.substr(colonPos + 2);
            
            // Split normal parameters
            if (!normalParams.empty()) {
                std::vector<std::string> normalParamList = split(normalParams, ' ');
                params.insert(params.end(), normalParamList.begin(), normalParamList.end());
            }
            
            // Add trailing parameter
            params.push_back(trailingParam);
        } else {
            // Split all parameters
            params = split(paramsStr, ' ');
        }
    }
    
    // Convert command to uppercase for case-insensitive comparison
    std::transform(command.begin(), command.end(), command.begin(), ::toupper);
    
    // Handle the command
    handleCommand(command, params);
}

void Client::handleCommand(const std::string& command, const std::vector<std::string>& params) {
    // This is where you'd implement handling of different IRC commands
    // For now, just print the command and parameters
    
    std::cout << "Command: " << command << std::endl;
    std::cout << "Parameters: ";
    for (size_t i = 0; i < params.size(); i++) {
        std::cout << "[" << params[i] << "] ";
    }
    std::cout << std::endl;
    
    // Basic command handling examples:
    if (command == "PASS") {
        // Password authentication
        if (params.size() < 1) {
            sendData("461 " + _nickname + " PASS :Not enough parameters");
            return;
        }
        
        if (_server->checkPassword(params[0])) {
            // Password accepted, but client is not fully registered yet
            // They need to provide NICK and USER as well
        } else {
            sendData("464 :Password incorrect");
        }
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
        if (!_nickname.empty() && !_username.empty()) {
            _authenticated = true;
            
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
