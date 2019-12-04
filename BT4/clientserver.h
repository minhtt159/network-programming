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
// File System
#include <filesystem>
namespace fs = std::filesystem;
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

struct fileObject {
    // File variables
    int fileHandle;
    std::string fileName;
    std::string fileHash;
    size_t fileSize;
    char* fileBuffer;
};

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
    // 
    int downloadCount;
    int uploadCount;
    int fileCount;

protected:
	void finalize();

public:
	std::unordered_map<int, fileObject> clientObjectMap;
	// Server object for client
	BTL::HostInfo* server;
	// Client object for server
	BTL::ClientInfo* client;
	// Network object for send and recv
	Network* networkObj;
	// 
	int localPort;
	std::string localAddress;
	// Helper 
	size_t dataSize;
	// 
	bool isServer;
	// Return true if connected to network, false otherwise
	bool connected;
	// Main run loop, read sdk
	void run();
	// Contructor
	Sockpeer(int localPort, std::string remoteHost, int remotePort, bool isServer);
	// Destructor
	~Sockpeer();
};
#endif
