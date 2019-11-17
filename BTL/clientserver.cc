#include "clientserver.h"

// https://stackoverflow.com/questions/212528/get-the-ip-address-of-the-machine
void getLocalIP(){
    struct ifaddrs * ifAddrStruct = NULL;
    struct ifaddrs * ifa = NULL;
    void * tmpAddrPtr = NULL;
    getifaddrs(&ifAddrStruct);

    for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) {
            continue;
        }
        if (ifa->ifa_addr->sa_family == AF_INET) { // check it is IP4
            tmpAddrPtr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            char addressBuffer[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
            printf("%s IP Address %s\n", ifa->ifa_name, addressBuffer); 
        }
        else if (ifa->ifa_addr->sa_family == AF_INET6) { // check it is IP6
            tmpAddrPtr = &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
            char addressBuffer[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, tmpAddrPtr, addressBuffer, INET6_ADDRSTRLEN);
            printf("%s IP Address %s\n", ifa->ifa_name, addressBuffer); 
        } 
    }
    if (ifAddrStruct != NULL) freeifaddrs(ifAddrStruct);
    return;
}

// Constructor
Sockpeer::Sockpeer(std::string host, int port, bool isServer){
    // Create networkObj

    // 
    BTL::HostInfo 

    if (isServer){
        // If server, always listen from socket
        
    }
    else {
        // If client, try to ask for server

    }
};

Sockpeer::run(){

};

// Destructor
Sockpeer::~Sockpeer(){

};

/*
Constructor
- Create sockets:
    recv: bind    to localhost : port
    send: connect to host      : port
*/
Network::Network(){
    
};
Network::~Network(){
    
};

// Read bytes from listen socket
char* Network::recv(){

}