#include <iostream>
#include <stdio.h>
#include <errno.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

class Network
{
private:
	// Buffer size for send & recv
	socklen_t BUFFSIZE;
public:
	// Socket for receiving message
	int recvfd;
	// Send UDP packet to peer
	size_t networkSend(std::string HOST, int PORT, std::string BUFFER);
	// Recv UDP packet from peer, return condition, buffer & information about sender info
	size_t networkRecv(char* BUFFER, size_t BUFFSIZE, sockaddr_in * CLIENT);
	// Constructor create listening socket recvfd at PORT
	Network(int port, int buffize);
	// Destructor
	~Network();
};