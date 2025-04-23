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

void Client::sendPendingData() {
    while (!_outgoingMessages.empty()) {
        std::string& message = _outgoingMessages.front();
        
        ssize_t bytesSent = send(_fd, message.c_str(), message.size(), -1);////////////////////////
        
        if (bytesSent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;  // Try again later
            }
            // Error case
            setDisconnected();
            return;
        }
        else if (static_cast<size_t>(bytesSent) < message.size()) {
            // Keep remainder in queue
            message = message.substr(bytesSent);
            return;
        }
        else {
            // Message fully sent
            _outgoingMessages.pop();
        }
    }
}

void Client::sendData(const std::string& message) {
    if (_disconnected) {
        return;  // Don't try to send to disconnected clients
    }

    std::string fullMessage = message + "\r\n";
    
    // Try immediate send if no queued messages
    if (_outgoingMessages.empty()) {
        ssize_t bytesSent = send(_fd, fullMessage.c_str(), fullMessage.size(), -1);
        
        if (bytesSent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket buffer full, queue for later
                _outgoingMessages.push(fullMessage);
                return;
            }
            // Real error occurred
            std::cerr << "Error sending to client " << _nickname << ": " << strerror(errno) << std::endl;
            setDisconnected();
            return;
        }
        else if (static_cast<size_t>(bytesSent) < fullMessage.size()) {
            // Partial send, queue remainder
            _outgoingMessages.push(fullMessage.substr(bytesSent));
            return;
        }
        // Full message sent successfully
        return;
    }
    
    // Already have queued messages, just add to queue
    _outgoingMessages.push(fullMessage);
}

// void Client::sendData(const std::string& message) {
//     std::string fullMessage = message + "\r\n";
//     // In Client::sendData()
//     ssize_t bytesSent = send(_fd, fullMessage.c_str(), fullMessage.size(), 0);
//     // std::cout << "Sending: " << fullMessage << " (Bytes: " << bytesSent << ")" << std::endl;

//     if (bytesSent < 0) {
//         if (errno == EAGAIN || errno == EWOULDBLOCK) {
//         // Would block, need to buffer this message for later sending
//         // Add to outgoing message queue (implement this)
//         // To this:
//         _outgoingMessages.push(fullMessage);
//         return;
//     }
//     std::cerr << "Error sending data to client: " << strerror(errno) << std::endl;
//     // Consider flagging client for disconnection
//     return;
//     }
//     else if (static_cast<size_t>(bytesSent) < fullMessage.size()) {
//     // Partial send - buffer remaining data
//     _outgoingMessages.push(fullMessage.substr(bytesSent));
//     // In parseAndHandleCommand() after extracting the command:
// }
// }

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

void Client::completeRegistration() {
    _authenticated = true;
    
    // Send welcome messages
    sendData("001 " + _nickname + " :Welcome to the Internet Relay Network " + _nickname + "!" + _username + "@host");
    sendData("002 " + _nickname + " :Your host is ft_irc, running version 1.0");
    sendData("003 " + _nickname + " :This server was created today");
    sendData("004 " + _nickname + " ft_irc 1.0 o o");
    
    // MOTD would go here
    sendData("422 " + _nickname + " :MOTD File is missing");
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
    
    // First check for commands that require authentication
    if (command == "JOIN" || command == "PRIVMSG" || command == "PART" || 
        command == "MODE" || command == "TOPIC" || command == "INVITE") {
        if (!_authenticated) {
            sendData("451 :You have not registered");
            return;
        }
    }
    // Basic command handling examples:
    if (command == "PASS") {
    if (_authenticated) {
        sendData("462 :You may not reregister");
        return;
    }
    
    if (params.size() < 1) {
        sendData("461 PASS :Not enough parameters");
        return;
    }
    
    if (_server->checkPassword(params[0])) {
        _passwordValidated = true;
    } else {
        sendData("464 :Password incorrect");
        setDisconnected();
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
    if (params.size() < 1) {
        sendData("431 :No nickname given");
        return;
    }
    
    std::string newNick = params[0];
    
    // Validate nickname format
    if (newNick.empty() || newNick.size() > 9 || 
        newNick.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789[]\\`_^{|}") != std::string::npos) {
        sendData("432 " + newNick + " :Erroneous nickname");
        return;
    }
    
    // TODO: Check if nickname is already in use
    
    _nickname = newNick;
    
    // If we have USER command and password, complete registration
    if (!_username.empty() && _passwordValidated && !_authenticated) {
        completeRegistration();
    }
}
    // else if (command == "NICK") {
    //     // Set nickname
    //     if (params.size() < 1) {
    //         sendData("431 :No nickname given");
    //         return;
    //     }
        
    //     std::string newNick = params[0];
    //     // Check if nickname is valid and not already taken
    //     // For simplicity, we're not implementing these checks here
        
    //     _nickname = newNick;
    //     sendData(":" + _nickname + " NICK :" + _nickname);
    // }
    else if (command == "USER") {
    if (_authenticated) {
        sendData("462 :You may not reregister");
        return;
    }
    
    if (!_passwordValidated) {
        sendData("464 :Password required");
        return;
    }
    
    if (params.size() < 4) {
        sendData("461 USER :Not enough parameters");
        return;
    }
    
    _username = params[0];
    // params[1] and params[2] are mode and unused
    // params[3] is the real name
    
    // Complete registration if we have nickname
    if (!_nickname.empty()) {
        completeRegistration();
    }
}
    // else if (command == "USER") {
    //     // Set username and realname
    //     if (params.size() < 4) {
    //         sendData("461 " + _nickname + " USER :Not enough parameters");
    //         return;
    //     }
        
    //     _username = params[0];
    //     // params[1] and params[2] are mode and unused
    //     // params[3] is the real name
        

    //     // If we have both nickname and username, complete registration
    //     if (!_nickname.empty() && !_username.empty() && _passwordValidated) {
    //         _authenticated = true;
    //         std::cout << "HELLO\n";
    //         // Send welcome messages
    //         sendData("001 " + _nickname + " :Welcome to the Internet Relay Network " + _nickname + "!" + _username + "@host");
    //         sendData("002 " + _nickname + " :Your host is ft_irc, running version 1.0");
    //         sendData("003 " + _nickname + " :This server was created today");
    //         sendData("004 " + _nickname + " ft_irc 1.0 o o");
    //     }
    // }
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