
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
    bool _isOperator;           // Whether client is a server operator
    bool _disconnected;
    // bool _passwordValidated;
    std::vector<Channel*> _channels;  // Channels the client has joined
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
    bool hasPendingMessages() const { return !_outgoingMessages.empty(); }

    void completeRegistration();

private:
    // Process received data
    void processData();
    // Handle different IRC commands
    void handleCommand(const std::string& command, const std::vector<std::string>& params);
    void parseAndHandleCommand(const std::string& line);

};

#endif // CLIENT_HPP