#ifndef CLIENTSERVER_H
#define CLIENTSERVER_H
// C & C++
#include <iostream>
#include <stdio.h>
// Stream & string
#include <sstream>
#include <fstream>
#include <cstring>
// Time
#include <ctime>
#include <unistd.h>
// Error
#include <errno.h>
// Listen on file descriptor events
#include <poll.h>
// Map pages of memory
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <iomanip>
#include <cmath>
#include <unordered_map>	// Map for marker
#include "udp.pb.h"     	// Protobuf
#include "network.h"		// Network
#include "md5.h"			// Hash function

// Serialize message
std::string wrapMessage(BTL::MessageType::Message msgType, int localPort, google::protobuf::Message* msgData);

class Sockpeer
{
private:
	// buffer size, should I let user decide?
	size_t BUFFSIZE;
	// time
	time_t startTime;
	// peer lookup map
	std::unordered_map<std::string, bool> lookup;
	//
    int askTime;
    int dupTime;
    // File variables
    int fileHandle;
    char* fileBuffer;
    std::string fileName;
    std::string fileHash;
    size_t fileSize;

protected:
	void finalize();

public:
	// Network object for send and recv
	Network* networkObj;
	// Client Info list
	BTL::ClientInfo* tracker;
	// localPort for networkObj->send
	int localPort;
	// Helper 
	size_t dataSize;
	// Return true if connected to network, false otherwise
	bool connected;
	// Return true if this peer is server
	bool isSeeder;
	// Main run loop, read sdk
	void run();
	// Contructor
	Sockpeer(int localPort, std::string remoteHost, int remotePort, bool isSeeder);
	// Destructor
	~Sockpeer();
};
#endif
