#include <iostream>
#include <sstream>
#include <stdio.h>
#include <cstring>
#include <ctime>
#include <errno.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "udp.pb.h"     	// Protobuf

// void getLocalIP();

class Network
{
private:
	// Socket for sending message
	int sendfd;
	// Socket for receiving message
	int recvfd;
public:
	// Send UDP packet to peer
	bool networkSend(std::string HOST, int PORT, std::string BUFFER);
	// Recv UDP packet from peer, return received bytes & information about sender info
	bool networkRecv(std::string* BUFFER, size_t BUFFSIZE, sockaddr_in * CLIENT);
	/* 
	Contructor:
	- create listening socket at PORT
	*/
	Network(int port);
	// Destructor
	~Network();
};

class Sockpeer
{
private:
	// Network object for send and recv
	Network* networkObj;
	// Client Info list
	BTL::ClientInfo* peers;
	// buffer size
	size_t BUFFSIZE;

protected:
	// Serialize message
	std::string wrapMessage(BTL::MessageType::Message msgType, google::protobuf::Message* msgData);

public:
	// Return true if connected to network, false otherwise
	bool connected;
	// 
	bool isServer;
	// Main run loop
	void run();
	/*
	Contructor:
	if host == NULL:
		- this is a server
		- open listening socket at PORT
	if host != NULL:
		- this is a client
		- connect to HOST:PORT
	*/
	Sockpeer(std::string host, int port, bool isServer);
	// Destructor
	~Sockpeer();
};
