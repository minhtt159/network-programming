// C & C++
#include <iostream>
#include <stdio.h>
// Stream & string
#include <sstream>
#include <fstream>
#include <cstring>
// Time
#include <ctime>
// Error
#include <errno.h>
// Listen on file descriptor events
#include <poll.h>
// Map pages of memory
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <unordered_map>	// Map for marker
#include "udp.pb.h"     	// Protobuf
#include "network.h"		// Network
#include "md5.h"			// Hash function
// #include "thread_helper.h"	// Multi thread


class Sockpeer
{
private:
	// Network object for send and recv
	Network* networkObj;
	// Client Info list
	BTL::ClientInfo* peers;
	// buffer size, should I let user decide?
	size_t BUFFSIZE;
	size_t dataSize;
	// time
	time_t startTime;
	// localPort for networkObj->send
	int localPort;
	// peer lookup map
	std::unordered_map<std::string, bool> lookup;
	// mark if peer is done
	std::unordered_map<std::string, bool> markFile;

protected:
	// Serialize message
	std::string wrapMessage(BTL::MessageType::Message msgType, google::protobuf::Message* msgData);
	// Parse message -> should parse message by hand
	// bool parseMessage(BTL::MessageType* internal_MessageType, std::string* BTLMessageString, std::string dataIn);
	// bool parseMessage(BTL::MessageType* internal_MessageType, google::protobuf::Message* internal_Message, std::string dataIn);

public:
	// Return true if connected to network, false otherwise
	bool connected;
	// Return true if this peer is server
	bool isServer;
	// Main run loop, read sdk
	void run();
	// Contructor
	Sockpeer(int localPort, std::string remoteHost, int remotePort, bool isServer);
	// Destructor
	~Sockpeer();
};
