#include <iostream>
#include <sstream>
#include <stdio.h>
#include <cstring>
#include <ctime>
#include <poll.h>
#include <fstream>
#include <unordered_map>
#include "udp.pb.h"     	// Protobuf
#include "network.h"		// Network
#include "md5.h"			// Hash function

class Sockpeer
{
private:
	// Network object for send and recv
	Network* networkObj;
	// Client Info list
	BTL::ClientInfo* peers;
	// buffer size, should I let user decide?
	size_t BUFFSIZE;
	int dataSize;
	//
	int localPort;
	//
	std::unordered_map<std::string, bool> lookup;
	std::unordered_map<std::string, bool> markFile;

protected:
	// Serialize message
	std::string wrapMessage(BTL::MessageType::Message msgType, google::protobuf::Message* msgData);
	// Parse message
	// bool parseMessage(BTL::MessageType* internal_MessageType, std::string* BTLMessageString, std::string dataIn);
	// bool parseMessage(BTL::MessageType* internal_MessageType, google::protobuf::Message* internal_Message, std::string dataIn);

public:
	// Return true if connected to network, false otherwise
	bool connected;
	// 
	bool isServer;
	// Main run loop, read sdk
	void run();
	//
	Sockpeer(int localPort, std::string remoteHost, int remotePort, bool isServer);
	// Destructor
	~Sockpeer();
};
