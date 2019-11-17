#include "clientserver.h"
#include <google/protobuf/util/delimited_message_util.h>

// // https://stackoverflow.com/questions/212528/get-the-ip-address-of-the-machine
// void getLocalIP(){
//     struct ifaddrs * ifAddrStruct = NULL;
//     struct ifaddrs * ifa = NULL;
//     void * tmpAddrPtr = NULL;
//     getifaddrs(&ifAddrStruct);

//     for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
//         if (!ifa->ifa_addr) {
//             continue;
//         }
//         if (ifa->ifa_addr->sa_family == AF_INET) { // check it is IP4
//             tmpAddrPtr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
//             char addressBuffer[INET_ADDRSTRLEN];
//             inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
//             printf("%s IP Address %s\n", ifa->ifa_name, addressBuffer); 
//         }
//         else if (ifa->ifa_addr->sa_family == AF_INET6) { // check it is IP6
//             tmpAddrPtr = &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
//             char addressBuffer[INET6_ADDRSTRLEN];
//             inet_ntop(AF_INET6, tmpAddrPtr, addressBuffer, INET6_ADDRSTRLEN);
//             printf("%s IP Address %s\n", ifa->ifa_name, addressBuffer); 
//         } 
//     }
//     if (ifAddrStruct != NULL) freeifaddrs(ifAddrStruct);
//     return;
// }

std::string string_to_hex(const std::string& input)
{
    static const char* const lut = "0123456789ABCDEF";
    size_t len = input.length();

    std::string output;
    output.reserve(2 * len);
    for (size_t i = 0; i < len; ++i)
    {
        const unsigned char c = input[i];
        output.push_back(lut[c >> 4]);
        output.push_back(lut[c & 15]);
    }
    return output;
}

std::string Sockpeer::wrapMessage(BTL::MessageType::Message msgType, google::protobuf::Message* msgData){
    // Open a stream
    std::stringstream stream;
    // Prepare MessageType
    BTL::MessageType* message = new BTL::MessageType();
    message->set_message(msgType);
    message->set_timestamp(time(NULL));
    // Write MessageType to stream
    google::protobuf::util::SerializeDelimitedToOstream(*message, &stream);
    // Write Message to stream
    google::protobuf::util::SerializeDelimitedToOstream(*msgData, &stream);
    // return stream
    return stream.str();
};

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

    if (!isServer) {
        // Add server to peers list
        BTL::HostInfo* server = this->peers->add_peer();
        server->set_host(host);
        server->set_port(port);
        server->set_isserver(true);

        // Try to send server info to itself
        std::string dataOut = wrapMessage(BTL::MessageType::HOSTINFO, server);
        this->networkObj->networkSend(server->host(), server->port(), dataOut); 

        // Try to recv this->HOSTINFO from server
        std::string serverReply;
        struct sockaddr_in servaddr;
        this->networkObj->networkRecv(&serverReply, this->BUFFSIZE, &servaddr);
        if (serverReply.length() == 0){
            std::cout << "No reply from server\n";
            this->connected = false;
            return;
        }
        printf("Read something from server");
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
            // Recv packet
            std::string serverReply;
            struct sockaddr_in client_address;
            n = this->networkObj->networkRecv(&serverReply, this->BUFFSIZE, &client_address);
            // Read client info
            std::string clientHost = inet_ntoa(client_address.sin_addr);
            int clientPort = ntohs(client_address.sin_port);

            // Parse message
            BTL::MessageType * clientMsg = new BTL::MessageType();
            bool clean_eof = true;
            std::stringstream stream = std::stringstream(serverReply);
            google::protobuf::io::IstreamInputStream zstream(&stream);
            google::protobuf::util::ParseDelimitedFromZeroCopyStream(clientMsg, &zstream, &clean_eof);
            // std::string messageTypeData = serverReply[1:1+n];
            // clientMsg->ParseFromString(messageTypeData);
            std::cout << clientMsg->message() << " " << clientMsg->timestamp() << std::endl;
            if (clientMsg->message() == BTL::MessageType::CLIENTINFO) {
                clean_eof = true;
                BTL::ClientInfo* client = new BTL::ClientInfo();
                google::protobuf::util::ParseDelimitedFromZeroCopyStream(client, &zstream, &clean_eof);
                std::cout << client << std::endl;
            }
            else if (clientMsg->message() == BTL::MessageType::HOSTINFO) {
                // Parse HostInfo
                clean_eof = true;
                BTL::HostInfo* server = new BTL::HostInfo();
                google::protobuf::util::ParseDelimitedFromZeroCopyStream(server, &zstream, &clean_eof);
                std::cout << server << std::endl;
                // Return client HostInfo
                BTL::HostInfo* client = new BTL::HostInfo();
                client->set_host(clientHost);
                client->set_port(clientPort);
                client->set_isserver(false);
                std::string dataOut = wrapMessage(BTL::MessageType::HOSTINFO, client);
                this->networkObj->networkSend(client->host(), client->port(), dataOut); 
                // Done, no need to reply ?
            }
            else {
                std::cout << "Command not found\n"; 
            }
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
bool Network::networkSend(std::string HOST, int PORT, std::string BUFFER) {
    // Creating socket file descriptor 
    if ( (this->sendfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
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

    char charBUFF[BUFFER.length() + 1];
    memcpy(charBUFF, BUFFER.c_str(), BUFFER.length());
    // Send BUFFER to SERVER, no MSG
    size_t n = sendto(sendfd, charBUFF, sizeof(charBUFF), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
    close(this->sendfd);
    return n == sizeof(charBUFF);
};

// Read bytes from listen socket
bool Network::networkRecv(std::string* BUFFER, size_t BUFFSIZE, sockaddr_in * CLIENT){
    if (!this->recvfd){
        std::cout << "Network:networkRecv recvfd not initialized";
        return false;
    }
    // Clear client socket address information
    socklen_t clientLength = sizeof(*CLIENT);
    memset(CLIENT, 0, clientLength);
    // char charBUFF[BUFFSIZE];
    size_t n = recvfrom(this->recvfd, BUFFER, BUFFSIZE, 0, (struct sockaddr *)CLIENT, &clientLength);
    // *BUFFSIZE = charBUFF;
    return true;
};

Network::~Network(){
    return;
};