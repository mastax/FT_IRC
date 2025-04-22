
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