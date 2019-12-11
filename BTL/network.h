#ifndef NETWORK_H
#define NETWORK_H

#include <iostream>
#include <stdio.h>
#include <cstring>
#include <errno.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// Send UDP packet to peer
size_t networkSend(std::string HOST, int PORT, std::string BUFFER);

class Network
{
private:
	// Buffer size for send & recv
	socklen_t BUFFSIZE;
public:
	// Socket for receiving message
	int recvfd;
	// Recv UDP packet from peer, return (length of buffer, buffer & information about sender info)
	size_t networkRecv(char* BUFFER, size_t BUFFSIZE, sockaddr_in * CLIENT);
	// Constructor create listening socket recvfd at PORT
	Network(int port, int buffsize);
	// Destructor
	~Network();
};

#endif /* !NETWORK_H */