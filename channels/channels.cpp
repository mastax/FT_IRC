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