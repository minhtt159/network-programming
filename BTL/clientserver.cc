#include "clientserver.h"
#include <google/protobuf/util/delimited_message_util.h>

// Some inline helper function
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

// Protected method
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

Sockpeer::Sockpeer(int localPort, std::string remoteHost, int remotePort, bool isServer){
    // Create listen socket at networkObj
    this->networkObj = new Network(localPort);

    // Create client info list -> this might contain nothing at start
    this->peers      = new BTL::ClientInfo();
    this->peers->set_localport(localPort);

    // BUFFSIZE, should I let user decide?
    this->BUFFSIZE  = 10*1024; // 10KB for testing 
    this->localPort = localPort;
    this->isServer  = isServer;

    if (!isServer) {
        // Add server to peers list
        BTL::HostInfo* server = this->peers->add_peer();
        server->set_host(remoteHost);
        server->set_port(remotePort);
        server->set_isserver(true);

        // Add server to lookup
        std::string marker = remoteHost + ":";
        marker += std::to_string(remotePort);
        this->lookup[marker] = true;
                
        char charReply[this->BUFFSIZE];
        std::string serverReply;
        struct sockaddr_in servaddr;

        int i = 10;
        int n;
        while (i--){
            memset(&charReply, 0, sizeof(charReply));
            memset(&servaddr, 0, sizeof(servaddr));
            // 10 attemps to call server
            this->peers->set_remotehost(remoteHost);
            this->peers->set_localport(localPort);
            std::string dataOut = wrapMessage(BTL::MessageType::CLIENTINFO, this->peers);

            if (this->networkObj->networkSend(remoteHost, server->port(), dataOut) != true){
                std::cout << "Sockpeer::Sockpeer send fail\n";
                continue;
            }

            n = this->networkObj->networkRecv(charReply, this->BUFFSIZE, &servaddr);
            if (n != -1){
                break;
            }
            else {
                std::cout << "Sockpeer::Sockpeer recv error, attempt " << i << ", try again\n";
            }
        }
        // After 10 attemps, server no response
        if (n == -1){
            std::cout << "No reply from server\n";
            this->connected = false;
            return;
        }

        serverReply = std::string(charReply, n);
        printf("Sockpeer::Sockpeer remote: %s:%u\n", server->host().c_str(),server->port());

        // Parse message from server
        std::string serverHost = inet_ntoa(servaddr.sin_addr);
        BTL::MessageType serverMessageType;
        BTL::ClientInfo serverClientInfo;
        std::stringstream stream = std::stringstream(serverReply, n);
        google::protobuf::io::IstreamInputStream zstream(&stream);
        bool clean_eof = true;
        google::protobuf::util::ParseDelimitedFromZeroCopyStream(&serverMessageType, &zstream, &clean_eof);
        clean_eof = true;
        google::protobuf::util::ParseDelimitedFromZeroCopyStream(&serverClientInfo, &zstream, &clean_eof);

        // Blacklist self from peer ClientInfo list
        marker = serverClientInfo.remotehost() + ":";
        marker += std::to_string(this->localPort);
        this->lookup[marker] = true;

        for (auto peer : serverClientInfo.peer()) {
            // BTL::HostInfo peer = serverClientInfo.peer(i);
            std::string peerString = peer.host() + ":";
            peerString += std::to_string(peer.port());
            if (this->lookup.find(peerString) == this->lookup.end()){
                // add to this->peers and lookup map
                BTL::HostInfo* a = this->peers->add_peer();
                a->set_host(peer.host());
                a->set_port(peer.port());
                a->set_isserver(peer.isserver());
                this->lookup[peerString] = true;
            }
        }
        // std::cout << "---------\n";
        // for (auto &x : this->lookup) {
        //     std::cout << x.first << std::endl;
        // }
        // std::cout << "---------\n";
        std::cout << "Sockpeer::Sockpeer this client now contains " << this->peers->peer_size() << " peer(s)\n";
        for (auto i: this->peers->peer()){
            printf("%s:%d\n", i.host().c_str(), i.port());
        }
    }
    else {
        // std::cout << "Sockpeer::Sockpeer created listen socket at port " << localPort << std::endl;
    }
    this->connected = true;
    return;
};

void Sockpeer::run(){
    // Helper variable
    size_t n;
    char output_buffer[1024];
    // Create 2 poll for stdin and socket
    struct pollfd fds[2];
    if (this->isServer){
        // Server

        /* Descriptor zero is stdin */
        fds[0].fd = this->networkObj->recvfd;
        fds[0].events = POLLIN | POLLPRI;

        fds[1].fd = 0;
        fds[1].events = POLLIN | POLLPRI;
    }
    else {
        // Client
        fds[0].fd = this->networkObj->recvfd;
        fds[0].events = POLLIN | POLLPRI;
    }
    // Normally we'd check an exit condition, but for this example we loop endlessly.
    while (true) {
        int ret = 0;
        if (this->isServer){
            ret = poll(fds, 2, -1);    
        }
        else {
            ret = poll(fds, 1, -1);
        }
        
        if (ret < 0) {
            printf("Error - poll returned error: %s\n", strerror(errno));
            break;
        }
        if (ret > 0) {
            /* Regardless of requested events, poll() can always return these */
            // if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            //     printf("Error - poll indicated socket error\n");
            //     break;
            // }
            // if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            //     printf("Error - poll indicated stdin error\n");
            //     break;
            // }

            /* Check if data to read from stdin */
            if (this->isServer && fds[1].revents & (POLLIN | POLLPRI)) {
                n = read(0, output_buffer, sizeof(output_buffer));
                if (n < 0) {
                    printf("Error - stdin error: %s\n", strerror(errno));
                    break;
                }

                output_buffer[n] = '\x00';
                // BTL::FileInfo requestedFile;
                // requestedFile.set_filename(output_buffer);
                std::cout << output_buffer << std::endl;
                // Open file

                // Calculate hash

            }

            /* Check if data to read from socket */
            if (fds[0].revents & (POLLIN | POLLPRI)) {
                std::cout << "wow, new packet\n";
                // Recv packet
                char charData[this->BUFFSIZE];
                struct sockaddr_in peerAddress;

                n = this->networkObj->networkRecv(charData, this->BUFFSIZE, &peerAddress);
                if (n == -1){
                    continue;
                }

                std::string peerData = std::string(charData, n);
                std::string peerHost = inet_ntoa(peerAddress.sin_addr);

                // Parse message
                BTL::MessageType peerMessageType;
                std::stringstream stream = std::stringstream(peerData);
                google::protobuf::io::IstreamInputStream zstream(&stream);
                bool clean_eof = true;
                google::protobuf::util::ParseDelimitedFromZeroCopyStream(&peerMessageType, &zstream, &clean_eof);
                clean_eof = true;
                
                if (peerMessageType.message() == BTL::MessageType::HOSTINFO) {
                    // Some peer is updated to network, try to update if peer is not already
                    BTL::HostInfo peer;
                    google::protobuf::util::ParseDelimitedFromZeroCopyStream(&peer, &zstream, &clean_eof);
                    
                    // Add peer to network
                    std::string peerString = peer.host() + ":";
                    peerString += std::to_string(peer.port());
                    if (this->lookup.find(peerString) == this->lookup.end()){
                        // Peer not found, add new
                        BTL::HostInfo* sender = this->peers->add_peer();
                        sender->set_host(peer.host());
                        sender->set_port(peer.port());
                        sender->set_isserver(peer.isserver());
                        this->lookup[peerString] = true;
                    }

                    // Show client list
                    std::cout << "Sockpeer::run this client now contains " << this->peers->peer_size() << " peer(s)\n";
                    for (auto i: this->peers->peer()){
                        printf("%s:%d\n", i.host().c_str(), i.port());
                    }
                }
                else if (peerMessageType.message() == BTL::MessageType::CLIENTINFO) {
                    // ClientInfo message for exchange node
                    BTL::ClientInfo peerClientInfo;
                    google::protobuf::util::ParseDelimitedFromZeroCopyStream(&peerClientInfo, &zstream, &clean_eof);
                    
                    // Blacklist self from peer ClientInfo list
                    std::string marker = peerClientInfo.remotehost() + ":";
                    marker += std::to_string(this->localPort);
                    this->lookup[marker] = true;

                    // Add peer to network
                    std::string peerString = peerHost + ":";
                    peerString += std::to_string(peerClientInfo.localport());
                    if (this->lookup.find(peerString) == this->lookup.end()){
                        // Peer not found, add new
                        BTL::HostInfo* sender = this->peers->add_peer();
                        sender->set_host(peerHost);
                        sender->set_port(peerClientInfo.localport());
                        sender->set_isserver(false);
                        this->lookup[peerString] = true;

                        // If new client update to network, send this->peers to the client
                        this->peers->set_remotehost(peerHost);
                        this->peers->set_localport(this->localPort);
                        std::string dataOut = wrapMessage(BTL::MessageType::CLIENTINFO, this->peers);
                        this->networkObj->networkSend(peerHost, peerClientInfo.localport(), dataOut);

                        // Send peer HostInfo to all of my other client
                        for (auto mypeer : this->peers->peer()){
                            // skip the new updated client
                            marker = mypeer.host() + ":";
                            marker += std::to_string(mypeer.port());
                            if ( marker == peerString ) {
                                continue;
                            }
                            this->peers->set_remotehost(mypeer.host());
                            this->peers->set_localport(this->localPort);
                            std::string dataOut = wrapMessage(BTL::MessageType::HOSTINFO, sender);
                            this->networkObj->networkSend(mypeer.host(), mypeer.port(), dataOut);
                        }
                    }
                    
                    // Add peer's client list to my client list
                    for (auto peer : peerClientInfo.peer()){
                        peerString = peer.host() + ":";
                        peerString += std::to_string(peer.port());

                        if (this->lookup.find(peerString) == this->lookup.end()){
                            // Peer not found, add new
                            BTL::HostInfo* a = this->peers->add_peer();
                            a->set_host(peer.host());
                            a->set_port(peer.port());
                            a->set_isserver(peer.isserver());
                            this->lookup[peerString] = true; 
                        }
                    }
                    // std::cout << "---------\n";
                    // for (auto &x : this->lookup) {
                    //     std::cout << x.first << std::endl;
                    // }
                    // std::cout << "---------\n";
                    // Show client list
                    std::cout << "Sockpeer::run this client now contains " << this->peers->peer_size() << " peer(s)\n";
                    for (auto i: this->peers->peer()){
                        printf("%s:%d\n", i.host().c_str(), i.port());
                    }
                }
                else {
                    std::cout << "Command not found\n";
                }
            }
        }
    }
    return;
};

// Destructor
Sockpeer::~Sockpeer(){
    return;
};














































