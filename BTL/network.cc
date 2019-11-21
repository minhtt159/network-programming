#include "network.h"

// Constructor create listening socket recvfd at PORT
Network::Network(int PORT, int BUFFER){
    // sysctl net.inet.udp.maxdgram
    this->BUFFSIZE = 9216 * 4; // for kernel to breath
    // Create recvfd
    if ( (this->recvfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
        std::cout<< "Network::Network socket creation failed\n"; 
        // should return here?
    }

    // Create socket address information
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));

    // Filling server information 
    servaddr.sin_family         = AF_INET;      // IPv4 
    servaddr.sin_addr.s_addr    = INADDR_ANY;   // 0.0.0.0 
    servaddr.sin_port           = htons(PORT);  // PORT

    // // Set socket timeout (1s)
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if (setsockopt(this->recvfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0 &&
        setsockopt(this->recvfd, SOL_SOCKET, SO_SNDBUF, &this->BUFFSIZE, sizeof(this->BUFFSIZE) < 0)) {
        std::cout << "Network:Network setsockopt failed\n"; 
        // should return here?
    }

    // Bind the socket with the server address
    if ( bind(this->recvfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0 ) { 
        std::cout << "Network:Network bind failed\n"; 
        // should return here?
    }
    
    std::cout << "Network::Network created listen socket at port " << PORT << std::endl;
};

// Send UDP packet (BUFFER) to peer (HOST:PORT)
size_t Network::networkSend(std::string HOST, int PORT, std::string BUFFER) {
    // Creating socket file descriptor 
    int sendfd;
    if ( (sendfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
        std::cout << "Network::networkSend socket creation failed\n"; 
        return false;
    }

    // Create socket address information
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr)); 

    // Filling server information 
    servaddr.sin_family = AF_INET; 
    servaddr.sin_port = htons(PORT); 
    servaddr.sin_addr.s_addr = inet_addr(HOST.c_str()); 

    // Convert BUFFER string to char array
    char charBUFF[BUFFER.length() + 1];
    memcpy(charBUFF, BUFFER.c_str(), BUFFER.length());

    // Send BUFFER to SERVER, no MSG
    size_t n = sendto(sendfd, charBUFF, sizeof(charBUFF), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
    close(sendfd);
    printf("%lu\n",n);
    return n;
};

// Recv UDP packet from peer, return condition, buffer & information about sender info
size_t Network::networkRecv(char* BUFFER, size_t BUFFSIZE, sockaddr_in * CLIENT){
    // Check if recvfd is open
    if (!this->recvfd){
        std::cout << "Network:networkRecv recvfd not initialized";
        return false;
    }
    if (setsockopt(this->recvfd, SOL_SOCKET, SO_RCVBUF, &this->BUFFSIZE, sizeof(this->BUFFSIZE)) < 0) {
        std::cout << "Network:networkRecv setsockopt failed\n";
        return false;
    }
    // Clear client socket address information
    socklen_t clientLength = sizeof(*CLIENT);
    memset(CLIENT, 0, clientLength);
    memset(BUFFER, 0, BUFFSIZE);
    // Recv charBUFF with maximum BUFFSIZE, should MSG_WAITALL? 
    size_t n = recvfrom(this->recvfd, BUFFER, BUFFSIZE, 0, (struct sockaddr *)CLIENT, &clientLength);
    printf("%lu\n",n);
    return n;
};

Network::~Network(){
    if (!this->recvfd){
        close(this->recvfd);
    }
    return;
};