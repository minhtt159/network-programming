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
    // Create listen socket at networkObj
    this->networkObj = new Network(port);
    // Create client info list -> this might contain nothing at start
    this->peers      = new BTL::ClientInfo();
    // BUFFSIZE
    this->BUFFSIZE = 10*1024; // 10KB for testing 
    // 
    this->isServer = isServer;
    // Helper variables
    size_t n;

    if (!isServer) {
        // Add server to peers list
        BTL::HostInfo* server = this->peers->add_peer();
        server->set_host(host);
        server->set_port(port);
        server->set_isserver(true);

        // Try to send server for peers
        std::string dataOut = "lewlew";
        char* pkt = new char[dataOut.length()+1];
        std::strcpy(pkt, dataOut.c_str());
        while (true) {
            n = this->networkObj->networkSend(server->host(), server->port(), pkt);
            if ( n == dataOut.length() ){
                break;
            }
        }

        // Try to recv server for peers
        char* serverReply = new char[BUFFSIZE];
        sockaddr_in servaddr;
        n = this->networkObj->networkRecv(serverReply, this->BUFFSIZE, (sockaddr_in *) &servaddr);
        if (n < 0 or strlen(serverReply) == 0){
            std::cout << "No reply from server\n";
            this->connected = false;
            return;
        }
        printf("Read something from server");
        // Parse readDelimitedFrom
        // Update peer
    }
    this->connected = true;
    return;
};

void Sockpeer::run(){
    // Helper variable
    size_t n;
    if (this->isServer){
        // loop for reading message
        while (true) {
            char serverReply[this->BUFFSIZE];
            struct sockaddr_in client_address;
            n = this->networkObj->networkRecv(serverReply, this->BUFFSIZE, &client_address);
            std::string serverHost = inet_ntoa(client_address.sin_addr);
            std::cout << serverReply << " " << serverHost << std::endl;
        }
    }
    else {
        // ask server 
    }
    return;
};

// Destructor
Sockpeer::~Sockpeer(){
    return;
};

/*--------------------------------------------------*/

/* 
Contructor:
- create listening socket at PORT
*/
Network::Network(int PORT){
    // Create recvfd
    if ( (this->recvfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
        std::cout<< "Network::Network socket creation failed\n"; 
        // exit(EXIT_FAILURE); 
    }

    // Create socket address information
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    // Filling server information 
    servaddr.sin_family         = AF_INET;      // IPv4 
    servaddr.sin_addr.s_addr    = INADDR_ANY;   // 0.0.0.0 
    servaddr.sin_port           = htons(PORT);  // PORT

    // Bind the socket with the server address
    if ( bind(this->recvfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0 ) { 
        std::cout << "Network:Network bind failed\n"; 
        // exit(EXIT_FAILURE);
    }
};

// Send BUFFER to HOST:PORT -> return number of characters sent
size_t Network::networkSend(std::string HOST, int PORT, char* BUFFER) {
    // Creating socket file descriptor 
    if ( (sendfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
        std::cout << "Network::networkSend socket creation failed\n"; 
        // exit(EXIT_FAILURE);
        return -1; 
    }

    // Create socket address information
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr)); 

    // Filling server information 
    servaddr.sin_family = AF_INET; 
    servaddr.sin_port = htons(PORT); 
    servaddr.sin_addr.s_addr = inet_addr(HOST.c_str()); 

    // Send BUFFER to SERVER, no MSG
    return send(sendfd, BUFFER, strlen(BUFFER), 0);
};

// Read bytes from listen socket
size_t Network::networkRecv(char* BUFFER, size_t BUFFSIZE, sockaddr_in * CLIENT){
    if (!this->recvfd){
        std::cout << "Network:networkRecv recvfd not initialized";
        return -1;
    }
    // Clear client socket address information
    socklen_t clientLength = sizeof(*CLIENT);
    memset(CLIENT, 0, clientLength);
    // Recv with MSG_WAITALL
    return recvfrom(this->recvfd, BUFFER, BUFFSIZE, MSG_WAITALL, (struct sockaddr *)CLIENT, &clientLength);
};

Network::~Network(){
    return;
};