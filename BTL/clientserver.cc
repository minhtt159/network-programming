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
    // BUFFSIZE, should I let user decide?
    this->BUFFSIZE  = 8*1024; // 8KB for testing 

    // Create listen socket at networkObj
    this->networkObj = new Network(localPort, this->BUFFSIZE);

    // Create client info list -> this might contain nothing at start
    this->peers      = new BTL::ClientInfo();
    this->peers->set_localport(localPort);

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

            if (this->networkObj->networkSend(remoteHost, server->port(), dataOut) < 0){
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

        // Show client list
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
    char output_buffer[this->BUFFSIZE];
    std::string dataOut;

    // Create 2 poll for socket and stdin
    struct pollfd fds[2];
    // Stream for reading / writing files
    std::ifstream is;
    std::ofstream os;

    std::string fileName = "";
    std::string fileHash = "";
    int fileSize = 0;
    int block_count = 0;
    std::string fileRawData = "";

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
            // Regardless of requested events, poll() can always return these
            if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                printf("Error - poll indicated socket error\n");
                break;
            }
            // Client don't care about fds[1]
            if (this->isServer and fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                printf("Error - poll indicated stdin error\n");
                break;
            }

            // Check if data to read from stdin, client don't care about fds[1]
            if (this->isServer and fds[1].revents & (POLLIN | POLLPRI)) {
                // Read input and clear event
                n = read(0, output_buffer, sizeof(output_buffer));
                if (n < 0) {
                    printf("Error - stdin error: %s\n", strerror(errno));
                    break;
                }

                if (is.is_open()){
                    std::cout << "Some file is pending, wait for client to response\n";
                    continue;
                }

                fileName = std::string(output_buffer, n-1);
                // Open file
                is.open(fileName, std::ifstream::binary);

                if (!is) {
                    std::cout << "Sockpeer::run File not found\n";
                }
                // Get length of file
                is.seekg(0, is.end);
                fileSize = is.tellg();
                is.seekg(0, is.beg);
                // Allocate memory
                char* buffer = new char[fileSize];
                // Read data as a block
                is.read(buffer, fileSize);
                std::string fileData = std::string(buffer, fileSize);

                std::cout << "Sending " << fileName << " to all client\n";
                // Calculate hash
                fileHash = md5(fileData);
                std::cout << "File hash: " << fileHash << std::endl;

                // Send fileInfo to all client
                BTL::FileInfo requestedFile;
                requestedFile.set_filename(fileName);
                requestedFile.set_filehash(fileHash);
                requestedFile.set_filesize(fileSize);
                for (auto peer: this->peers->peer()){
                    if (!peer.isserver()){
                        // Send file info to all peer that is not server
                        dataOut = wrapMessage(BTL::MessageType::FILEINFO, &requestedFile);
                        this->networkObj->networkSend(peer.host(), peer.port(), dataOut);  
                    }
                }
                // Client peer must send requested file info to each other in case packet drop
                if (buffer) {
                    delete [] buffer;
                }
                // Begin send fileData
                buffer = new char[this->BUFFSIZE];
                block_count = fileSize/this->BUFFSIZE + 1;
                int offset, read_length;
                for (int i=0; i<block_count; i++){
                    // Read data to buffer
                    offset = this->BUFFSIZE * i;
                    read_length = (fileSize - offset) < this->BUFFSIZE ? (fileSize - offset) : this->BUFFSIZE;
                    if (read_length == 0) {
                        continue;
                    }
                    is.seekg(offset);
                    memset(buffer, 0, this->BUFFSIZE);
                    is.read(buffer, read_length);
                    // Prepare fileData object
                    BTL::FileData fileData;
                    fileData.set_filename(fileName);
                    fileData.set_offset(offset);
                    std::string rawData = std::string(buffer, read_length);
                    fileData.set_data(rawData);
                    dataOut = wrapMessage(BTL::MessageType::FILEDATA, &fileData);
                    std::cout << md5(rawData) << " " << rawData.length() << " " << std::endl;
                    // 1-server config
                    BTL::HostInfo peer = this->peers->peer(i % this->peers->peer_size()); 
                    this->networkObj->networkSend(peer.host(), peer.port(), dataOut);
                    std::cout << "Sending block: " << i << " to " << peer.host() << ":" << peer.port() << std::endl;
                }
                // Hold for more requests
                std::cout << "Initial send done\n";
            }

            /* Check if data to read from socket */
            if (fds[0].revents & (POLLIN | POLLPRI)) {
                // Recv packet
                char charData[this->BUFFSIZE];
                struct sockaddr_in peerAddress;

                n = this->networkObj->networkRecv(charData, this->BUFFSIZE, &peerAddress);
                if (n == -1){
                    // Something wrong with the data, ask to send again?
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
                    // Some peer is updated to network and (usually) the server send this
                    // try to update if peer is not already added
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
                    
                    // Add peer's client list to my client list, skip if already added
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

                    // Show client list
                    std::cout << "Sockpeer::run this client now contains " << this->peers->peer_size() << " peer(s)\n";
                    for (auto i: this->peers->peer()){
                        printf("%s:%d\n", i.host().c_str(), i.port());
                    }
                }
                else if (peerMessageType.message() == BTL::MessageType::FILEINFO){
                    // One file at a time?
                    if (os.is_open()){
                        std::cout << "Sockpeer::run this client already open ofstream\n";
                    }

                    // Only client can get this message
                    BTL::FileInfo fileInfo;
                    google::protobuf::util::ParseDelimitedFromZeroCopyStream(&fileInfo, &zstream, &clean_eof);
                    
                    fileName = fileInfo.filename();
                    fileHash = fileInfo.filehash();
                    fileSize = fileInfo.filesize();
                    block_count = fileSize / this->BUFFSIZE + 1;

                    os.open(fileName, std::ofstream::binary);
                    std::cout << "Sockpeer::run open " << fileName << std::endl;
                    // Mark time begin

                    // Bounce this message to other client
                    for (auto peer: this->peers->peer()){
                        if (!peer.isserver()){
                            // Send file info to all peer that is not server
                            dataOut = wrapMessage(BTL::MessageType::FILEINFO, &fileInfo);
                            this->networkObj->networkSend(peer.host(), peer.port(), dataOut);  
                        }
                    }

                    // Client start to write to fileRawData;
                    fileRawData = std::string("", fileSize);
                }
                else if (peerMessageType.message() == BTL::MessageType::FILEDATA){
                    BTL::FileData fileData;
                    google::protobuf::util::ParseDelimitedFromZeroCopyStream(&fileData, &zstream, &clean_eof);

                    // Write rawData to buffer
                    os.seekp(fileData.offset());
                    os << fileData.data();
                    // os.write(fileData.data().c_str(), fileData.data().length());
                    std::cout << fileData.data().length() << " " << md5(fileData.data()) << std::endl;
                    std::cout << "Sockpeer::run writing " << fileData.data().length() << " bytes at offset " << fileData.offset() << std::endl;
                    // Mark block when done?
                    // int block = fileData.offset() / this->BUFFSIZE;
                    // block_count ^= (1 << block);
                    // std::cout << "Getting block: " << block << " " <<std::bitset<8>(block_count).to_string() << std::endl;
                    if (fileData.offset() + fileData.data().length() == fileSize){
                        std::cout << "Send done";
                        os.close();
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














































