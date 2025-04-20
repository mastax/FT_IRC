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