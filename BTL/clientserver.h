#ifndef CLIENTSERVER_H
#define CLIENTSERVER_H
// C & C++
#include <iostream>
#include <stdio.h>
// Stream & string
#include <sstream>
#include <fstream>
#include <cstring>
#include <ctime>            // Time
#include <unistd.h>
#include <cstdarg>          // va_start from debug
#include <errno.h>          // Error
#include <poll.h>           // Listen on file descriptor events
#include <sys/mman.h>       // Map pages of memory
#include <sys/stat.h>       // File stat
#include <fcntl.h>
#include <iomanip>
#include <cmath>            // Math operation
#include <unordered_map>    // Map for marker
#include "udp.pb.h"         // Protobuf
#include "network.h"        // Network
#include "md5.h"            // Hash function

// Serialize message before sending
std::string wrapMessage(BTL::MessageType::Message msgType, int localPort, google::protobuf::Message* msgData);

struct window{
    std::string host;       // host
    size_t port;            // port
    std::string data;       // data packet

    window(std::string h, size_t p, std::string d){
        this->host = h;
        this->port = p;
        this->data = d;
    }

    // bool operator<(const window &p)const {
    //     return this->index < p.index
    // }
    bool operator==(const window &p) const{
        return (this->port == p.port) and (this->host == p.host) and (this->host == p.host);
    }
};
namespace std{
    template<>
    struct hash<window>{
        std::size_t operator()(const window& k) const{
            using std::size_t;
            using std::hash;
            using std::string;
            return ((hash<string>()(k.host)
                    ^ (hash<string>()(k.data) << 1)) >> 1 )
                    ^ (hash<int>()(k.port) << 1 );
        }
    };
}


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
    int askTime;		// Mark how many time this peer receive FILECACHE message
    int dupTime;		// Mark how many time this peer receive duplicate FILEDATA message
    // File variables, just send 1 file at a time
    int fileHandle;
    char* fileBuffer;
    std::string fileName;
    std::string fileHash;
    size_t fileSize;
    // 
	void finalize();
    // CControl window variable
    int max_windows;


public:
    std::unordered_map<window, bool> cc_window;
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
