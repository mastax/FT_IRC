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