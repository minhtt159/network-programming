#include <stdio.h>
#include <cstring>
#include <errno.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "udp.pb.h"     	// Protobuf

void getLocalIP();

class Sockpeer
{
private:
	Network networkObj;
public:
	void run();
	Sockpeer();
	~Sockpeer();
	
};

class Network
{
private:
	char* host;
	int port;
	int sendfd;
	int recvfd;
public:
	Network();
	~Network();
	send();
	char* recv();
};