#include "clientserver.h"
#include <google/protobuf/util/delimited_message_util.h>

// Some inline helper function
std::string string_to_hex(const std::string& input)
{
    static const char* const lut = "0123456789abcdef";
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
    this->BUFFSIZE   = 1460;    // MTU default 
    this->dataSize   = 1350;    // Plenty of space for header

    // Create listen socket at networkObj
    this->networkObj = new Network(localPort, this->BUFFSIZE);

    // Create client info list -> this might contain nothing at start
    this->peers      = new BTL::ClientInfo();
    this->peers->set_localport(localPort);

    this->localPort  = localPort;
    this->isServer   = isServer;

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
    int fileHandle = -1;

    // File transfer helper variables
    std::string fileName = "";
    std::string fileHash = "";
    int fileSize = 0;
    int block_count;
    std::string blockMark = "";
    std::string blockDone = "";

    if (this->isServer){
        // Server print listed files

        fds[0].fd = this->networkObj->recvfd;
        fds[0].events = POLLIN | POLLPRI;
        // fd = 0 (stdin)
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

                if (fileHandle != -1){
                    printf("Some file is pending, wait for client to response\n");
                    continue;
                }

                // Parse file name
                fileName = std::string(output_buffer, n-1);

                // Open file
                fileHandle = open(fileName.c_str(), O_RDONLY);

                if (fileHandle < 0) {
                    // Open file failed, print error
                    printf("Sockpeer::run Open file failed, error: %d\n", strerror(errno));
                    continue;
                }

                /* Advise the kernel of our access pattern.  */
                posix_fadvise(fileHandle, 0, 0, 1);  // FDADVICE_SEQUENTIAL

                // obtain file size
                struct stat sb;
                fstat(fileHandle, &sb);
                fileSize = sb.st_size;
                printf("Sockpeer::run Server sending \"%s\" to all clients\n", fileName.c_str());
                
                // Allocate memory
                char buffer[fileSize+1];
                if (fileSize != read(fileHandle, buffer, fileSize)) {
                    printf("Sockpeer::run Can't read file to calculate hash\n");

                }

                // Calculate hash - this might slow bcuz C++ :shrug:
                fileHash = md5(std::string(buffer, fileSize));
                printf("Sockpeer::run File hash: %s\n", fileHash.c_str());

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

                // Begin send fileData
                block_count = 0;

                while ((block_count * this->dataSize) < fileSize){
                    // Prepare information
                    int offset = block_count * this->dataSize;
                    int read_length = (fileSize - offset) < this->dataSize ? (fileSize - offset) : this->dataSize;
                    std::string rawData = std::string(buffer + offset, read_length);

                    // Prepare fileData object
                    BTL::FileData fileData;
                    fileData.set_filename(fileName);
                    fileData.set_offset(offset);
                    fileData.set_data(rawData);
                    dataOut = wrapMessage(BTL::MessageType::FILEDATA, &fileData);
                    std::cout << md5(rawData) << " " << rawData.length() << " " << std::endl;

                    // 1-server config
                    BTL::HostInfo peer = this->peers->peer(i % this->peers->peer_size()); 
                    this->networkObj->networkSend(peer.host(), peer.port(), dataOut);
                    printf("Sending block: %d to %s:%d\n", i, peer.host(), peer.port());
                    block_count += 1;
                }

                // // mark that all file is not done
                // for (auto x = lookup.begin(); x != lookup.end(); x++){
                //     markFile[x->first] = false;
                // }

                // // Hold for more requests
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
                        this->networkObj->networkSend(peerHost, sender->port(), dataOut);

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
                    // One file at a time 
                    if (this->isServer and fileHandle == -1){

                        std::cout << "Sockpeer::run this client already open ofstream\n";
                        continue;
                    }

                    // Only client can get this message
                    BTL::FileInfo fileInfo;
                    google::protobuf::util::ParseDelimitedFromZeroCopyStream(&fileInfo, &zstream, &clean_eof);
                    
                    fileName = fileInfo.filename();
                    fileHash = fileInfo.filehash();
                    fileSize = fileInfo.filesize();

                    // Mark helper to track written block
                    block_count = fileSize/this->dataSize + 1;
                    char buffer[block_count];
                    memset(buffer, '1', block_count);
                    blockMark = std::string(buffer, block_count);
                    memset(buffer, '0', block_count);
                    blockDone = std::string(buffer, block_count);

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
                }
                else if (peerMessageType.message() == BTL::MessageType::FILEDATA){
                    BTL::FileData fileData;
                    google::protobuf::util::ParseDelimitedFromZeroCopyStream(&fileData, &zstream, &clean_eof);

                    int block_i = fileData.offset() / this->dataSize;
                    // If this block is already set, skip it
                    if (blockMark[block_i] == '0'){
                        continue;
                    }

                    // Write rawData to buffer
                    os.seekp(fileData.offset());
                    // os << fileData.data();
                    os.write(fileData.data().c_str(), fileData.data().length());

                    // std::cout << fileData.data().length() << " " << md5(fileData.data()) << std::endl;
                    // std::cout << fileData.data().length()+fileData.offset() << " " << fileSize << std::endl;
                    std::cout << "Sockpeer::run writing " << fileData.data().length() << " bytes at block " << block_i << std::endl;
                    
                    // Bounce back to other client
                    for (auto peer: this->peers->peer()){
                        if (!peer.isserver()){
                            // Send file info to all peer that is not server
                            dataOut = wrapMessage(BTL::MessageType::FILEDATA, &fileData);
                            this->networkObj->networkSend(peer.host(), peer.port(), dataOut);  
                        }
                    }

                    // Mark block when done?
                    blockMark[block_i] = '0';

                    if (blockDone.compare(blockMark) == 0){
                        std::cout << "Sockpeer::run Received all\n";
                        os.close();
                        // Send done to server, MARK: 1-server topo
                        for (auto peer: this->peers->peer()){
                            if (peer.isserver()){
                                BTL::CommonReply reply;
                                reply.set_status(-1);
                                reply.set_localport(this->localPort);
                                dataOut = wrapMessage(BTL::MessageType::COMMONREPLY, &reply);
                                this->networkObj->networkSend(peer.host(), peer.port(), dataOut);
                            }
                        }
                    }
                }
                else if (peerMessageType.message() == BTL::MessageType::COMMONREPLY){
                    BTL::CommonReply reply;
                    google::protobuf::util::ParseDelimitedFromZeroCopyStream(&reply, &zstream, &clean_eof);

                    if (this->isServer) {
                        // When server received this message, some client is asking for some block
                        if (reply.status() == -1){
                            std::string whichOne = peerHost + ":";
                            whichOne += std::to_string(reply.localport());
                            markFile[whichOne] = true;

                            // if all file is mark true, break
                            bool isDone = true;
                            for (auto x = lookup.begin(); x != lookup.end(); x++){
                                if (markFile[x->first] == false){
                                    isDone = false;
                                    break;
                                }
                            }
                            if (isDone){
                                break;
                            }
                            continue;
                        }
                        // 
                    }
                    // When peer received this message, other peer is requesting for some block
                    if (blockMark[reply.status()] == '1' and this->isServer == false){
                        // Block is not written, skip
                        continue;
                    }
                    char buffer[this->dataSize];
                    int block_i = reply.status();
                    int read_length = (fileSize - block_i*this->dataSize) < this->dataSize ? (fileSize - block_i*this->dataSize) : this->dataSize;
                    std::ifstream tmp_is(fileName, std::ifstream::binary);
                    tmp_is.seekg(reply.status());
                    tmp_is.read(buffer, read_length);
                    tmp_is.close();
                    // Prepare data
                    BTL::FileData fileData;
                    fileData.set_filename(fileName);
                    fileData.set_offset(reply.status());
                    fileData.set_data(std::string(buffer, read_length));
                    // Send to that client
                    dataOut = wrapMessage(BTL::MessageType::FILEDATA, &fileData);
                    this->networkObj->networkSend(peerHost, reply.localport(), dataOut);
                }
                else {
                    std::cout << "Command not found\n";
                }
            }
        }
        // ask all peer for non written block
        if (os.is_open() and this->isServer == false){
            int block_i;
            for (block_i=0; block_i<block_count; block_i++){
                if (blockMark[block_i] == '1'){
                    break;
                }
            }
            if (block_i == block_count){
                // All block written? why os not close?
                os.close();
                std::cout << "Sockpeer::run::random Exception here\n";
            }
            // Ask server for block_i
            BTL::CommonReply reply;
            reply.set_localport(this->localPort);
            reply.set_status(block_i);
            dataOut = wrapMessage(BTL::MessageType::COMMONREPLY, &reply);

            for (auto peer: this->peers->peer()){
                if (time(NULL) % 2 == 0)
                this->networkObj->networkSend(peer.host(), peer.port(), dataOut);
            }
        }
    }
    return;
};

// Destructor
Sockpeer::~Sockpeer(){
    return;
};














































