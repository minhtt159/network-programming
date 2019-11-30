#include "clientserver.h"
#include <google/protobuf/util/delimited_message_util.h>

int DEBUG = 0;

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

std::string wrapMessage(BTL::MessageType::Message msgType, int localPort, google::protobuf::Message* msgData){
    // Open a stream
    std::stringstream stream;
    // Prepare MessageType
    BTL::MessageType* message = new BTL::MessageType();
    message->set_message(msgType);
    message->set_localport(localPort);
    // Write MessageType to stream
    google::protobuf::util::SerializeDelimitedToOstream(*message, &stream);
    // Write Message to stream
    google::protobuf::util::SerializeDelimitedToOstream(*msgData, &stream);
    // return stream
    return stream.str();
};

Sockpeer::Sockpeer(int localPort, std::string remoteHost, int remotePort, bool isSeeder){
    // BUFFSIZE, should I let user decide?
    this->BUFFSIZE   = 1460;    // MTU default 
    this->dataSize   = 1350;    // Plenty of space for header

    // Create listen socket at networkObj
    this->networkObj = new Network(localPort, this->BUFFSIZE);

    // Create client info list -> this might contain nothing at start
    this->tracker      = new BTL::ClientInfo();
    this->tracker->set_localport(localPort);

    this->localPort  = localPort;
    this->isSeeder  = isSeeder;

    if (!isSeeder) {
        // Add server to tracker list
        BTL::HostInfo* server = this->tracker->add_peers();
        server->set_host(remoteHost);
        server->set_port(remotePort);
        server->set_isseeder(true);

        // Add server to lookup
        std::string marker = remoteHost + ":";
        marker += std::to_string(remotePort);
        this->lookup[marker] = true;
                
        char charReply[this->BUFFSIZE];
        std::string serverReply;
        struct sockaddr_in servaddr;

        int i = 0;
        int n;
        // Prepare data
        this->tracker->set_remotehost(remoteHost);
        this->tracker->set_localport(localPort);
        this->tracker->set_isseeder(false);
        std::string dataOut = wrapMessage(BTL::MessageType::CLIENTINFO, localPort, this->tracker);
        while (i < 10){
            // 10 attemps to call server
            memset(&charReply, 0, sizeof(charReply));
            memset(&servaddr, 0, sizeof(servaddr));
            n = networkSend(remoteHost, server->port(), dataOut);
            if (n < 0){
                if (DEBUG) printf("dataOut: %lu, n: %d\n", dataOut.size(), n);
                continue;
            }
            n = this->networkObj->networkRecv(charReply, this->BUFFSIZE, &servaddr);
            if (n != -1){
                break;
            }
            else {
                if (DEBUG) std::cout << "\rSockpeer::Sockpeer recv error, attempt " << i << ", try again" << std::flush;
            }
        }
        // After 10 attemps, server no response
        if (n == -1){
            std::cout << "\nNo reply from server\n";
            this->connected = false;
            return;
        }
        // Parse message from server
        serverReply = std::string(charReply, n);
        std::string serverHost = inet_ntoa(servaddr.sin_addr);

        BTL::MessageType serverMessageType;
        BTL::ClientInfo serverClientInfo;
        std::stringstream stream = std::stringstream(serverReply);
        google::protobuf::io::IstreamInputStream zstream(&stream);
        bool clean_eof = true;
        google::protobuf::util::ParseDelimitedFromZeroCopyStream(&serverMessageType, &zstream, &clean_eof);
        clean_eof = true;
        google::protobuf::util::ParseDelimitedFromZeroCopyStream(&serverClientInfo, &zstream, &clean_eof);

        // Blacklist self from peer ClientInfo list
        marker = serverClientInfo.remotehost() + ":";
        marker += std::to_string(this->localPort);
        this->lookup[marker] = true;

        // Add peers
        for (auto peer: serverClientInfo.peers()) {
            marker = peer.host() + ":";
            marker += std::to_string(peer.port());
            if (this->lookup.find(marker) == this->lookup.end()){
                BTL::HostInfo* a = this->tracker->add_peers();
                a->set_host(peer.host());
                a->set_port(peer.port());
                a->set_isseeder(peer.isseeder());
                this->lookup[marker] = true;
            }
        }

        // Show client list
        if (DEBUG) {
            std::cout << "Sockpeer::Sockpeer this client now contains " << this->tracker->peers_size() << " peer(s)\n";
            for (auto i: this->tracker->peers()){
                printf("%s:%d - %d\n", i.host().c_str(), i.port(), i.isseeder());
            }
        }
    }
    else {
        this->tracker->set_isseeder(true);
        // std::cout << "Sockpeer::Sockpeer created listen socket at port " << localPort << std::endl;
    }
    this->connected = true;
    return;
};

void Sockpeer::run(){
    static std::string blockMark = "1";
    // static std::string blockDone = "0";
    static char* fileBuffer;

    // Lambda function to update fileData
    // auto parseFunction = [](BTL::ClientInfo *lTracker, BTL::FileData lFileData, std::string lPeerHost, int lPeerPort){
    //     memcpy(fileBuffer + lFileData.offset(), lFileData.data().c_str(), lFileData.data().length());

    //     // Bounce back to other client
    //     for (auto peer: lTracker->peers()){
    //         if (peer.isseeder()){
    //             continue;
    //         }
    //         if (peer.host() == lPeerHost and peer.port() == lPeerPort){
    //             continue;
    //         }
    //         // Send file info to all peer that is not server and not the one who sent this message
    //         std::string dataOut = wrapMessage(BTL::MessageType::FILEDATA, lPeerPort, &lFileData);
    //         networkSend(peer.host(), peer.port(), dataOut);  
    //     }
    // };
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
    int last_unknown_block = 0;
    int remain_block = 0;
    std::string marker;
    bool doneWork;
    int askTime = 0;
    int timeout = 0;
    int timeout_hit = 0;

    // Multiplexing fd
    fds[0].fd = this->networkObj->recvfd;
    fds[0].events = POLLIN | POLLPRI;
    if (this->isSeeder){
        // Server print listed files

        // fd = 0 (stdin)
        fds[1].fd = 0;
        fds[1].events = POLLIN | POLLPRI;
    }
    
    // Normally we'd check an exit condition, but for this example we loop endlessly.
    while (true) {
        doneWork = false;
        int ret = 0;
        if (this->isSeeder){
            ret = poll(fds, 2, timeout);    
        }
        else {
            ret = poll(fds, 1, timeout);
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
            if (this->isSeeder and fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                printf("Error - poll indicated stdin error\n");
                break;
            }

            // Check if data to read from stdin, client don't care about fds[1]
            if (this->isSeeder and fds[1].revents & (POLLIN | POLLPRI)) {
                // Read input and clear event
                n = read(0, output_buffer, sizeof(output_buffer));
                if (n <= 0) {
                    printf("Error - stdin error: %s\n", strerror(errno));
                    break;
                }

                if (fileHandle != -1){
                    if (DEBUG) printf("Some file is pending, wait for client to response\n");
                    continue;
                }

                fileName = std::string(output_buffer, n-1);
                fileHandle = open(fileName.c_str(), O_RDONLY);
                if (fileHandle < 0) {
                    // Open file failed, print error
                    if (DEBUG) printf("Sockpeer::run Open file failed, error: %s\n", strerror(errno));
                    continue;
                }
                // obtain file size
                struct stat sb;
                fstat(fileHandle, &sb);
                fileSize = sb.st_size;
                printf("Sockpeer::run Server is sending \"%s\" with fileSize = %d to %d client(s)\n", fileName.c_str(), fileSize, this->tracker->peers_size());
                
                // Allocate memory
                fileBuffer = (char *)mmap(0, fileSize, PROT_READ, MAP_SHARED, fileHandle, 0);
                std::string rawData = std::string(fileBuffer, fileSize);
                // Calculate hash - this might slow bcuz C++ :shrug:
                fileHash = md5(rawData);
                printf("Sockpeer::run File hash: %s\n", fileHash.c_str());

                startTime = time(NULL);

                // Send fileInfo to all peers
                BTL::FileInfo requestedFile;
                requestedFile.set_filename(fileName);
                requestedFile.set_filehash(fileHash);
                requestedFile.set_filesize(fileSize);
                for (auto peer: this->tracker->peers()){
                    dataOut = wrapMessage(BTL::MessageType::FILEINFO, this->localPort, &requestedFile);
                    networkSend(peer.host(), peer.port(), dataOut);
                    peer.set_isseeder(false); 
                }

                // Begin send fileData
                block_count = 0;
                while ((block_count * this->dataSize) < fileSize){
                    // Prepare information
                    int offset = block_count * this->dataSize;
                    int read_length = (fileSize - offset) < this->dataSize ? (fileSize - offset) : this->dataSize;
                    rawData = std::string(fileBuffer + offset, read_length);

                    // Prepare fileData object
                    BTL::FileData fileData;
                    fileData.set_filename(fileName);
                    fileData.set_offset(offset);
                    fileData.set_data(rawData);
                    dataOut = wrapMessage(BTL::MessageType::FILEDATA, this->localPort, &fileData);
                    // VERSION 1: SEND ALL TO ALL
                    // for (auto peer: this->tracker->peers()){
                    //     // Should spawn thread to do this?
                    //     networkSend(peer.host(), peer.port(), dataOut);
                    //     printf("Sending block: %d to %s:%d\n", block_count, peer.host().c_str(), peer.port());
                    // }
                    // VERSION 2: SEND 1/peer_size
                    auto peer = this->tracker->peers(block_count % this->tracker->peers_size());
                    networkSend(peer.host(), peer.port(), dataOut);
                    if (DEBUG) printf("Sending block: %d to %s:%d\n", block_count, peer.host().c_str(), peer.port());

                    block_count += 1;
                }
                printf("Sockpeer::run Server sent %d blocks\n", block_count);
                char buffer[block_count];
                memset(buffer, '0', block_count);
                blockMark = std::string(buffer, block_count);

                // // mark that all clients is not done
                // for (auto peer: this->tracker->peers()){
                //     marker = peer.host() + ":";
                //     marker += std::to_string(peer.port());
                //     markFile[marker] = false;
                // }

                // // Hold for more requests
                // std::cout << "Initial send done\n";
                doneWork = true;
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
                int peerPort = peerMessageType.localport();

                if (peerMessageType.message() == BTL::MessageType::HOSTINFO) {
                    // Some peer try to update its position
                    BTL::HostInfo peer;
                    google::protobuf::util::ParseDelimitedFromZeroCopyStream(&peer, &zstream, &clean_eof);
                    
                    marker = peer.host() + ":";
                    marker += std::to_string(peer.port());
                    if (this->lookup.find(marker) == this->lookup.end()){
                        // Peer not found, add new
                        BTL::HostInfo* a = this->tracker->add_peers();
                        a->set_host(peer.host());
                        a->set_port(peer.port());
                        a->set_isseeder(peer.isseeder());
                        this->lookup[marker] = true;
                    }
                    // else if (peer.isseeder() == true){
                    //     // This peer received all the file, change to seeder
                    //     for (auto a: this->tracker->peers()){
                    //         if (a.host() == peer.host() and a.port() == peer.port()){
                    //             a.set_isseeder(true);
                    //             break;
                    //         }
                    //     }
                    // }
                    // else {
                    //     if (DEBUG) printf("Sockpeer::run HOSTINFO wrong %s:%d - %d\n", peer.host().c_str(), peer.port(), peer.isseeder());
                    // }
                    // Show client list
                    if (DEBUG) {
                        std::cout << "Sockpeer::Sockpeer this client now contains " << this->tracker->peers_size() << " peer(s)\n";
                        for (auto i: this->tracker->peers()){
                            printf("%s:%d - %d\n", i.host().c_str(), i.port(), i.isseeder());
                        }
                    }
                    doneWork = true;
                }
                else if (peerMessageType.message() == BTL::MessageType::CLIENTINFO) {
                    // ClientInfo message for exchange node
                    BTL::ClientInfo peerClientInfo;
                    google::protobuf::util::ParseDelimitedFromZeroCopyStream(&peerClientInfo, &zstream, &clean_eof);
                    
                    // Blacklist self from peer ClientInfo list
                    marker = peerClientInfo.remotehost() + ":";
                    marker += std::to_string(this->localPort);
                    this->lookup[marker] = true;

                    // Add peer to network
                    marker = peerHost + ":";
                    marker += std::to_string(peerClientInfo.localport());
                    if (this->lookup.find(marker) == this->lookup.end()){
                        // Sender not found, add sender to self peers
                        BTL::HostInfo* sender = this->tracker->add_peers();
                        sender->set_host(peerHost);
                        sender->set_port(peerClientInfo.localport());
                        sender->set_isseeder(peerClientInfo.isseeder());
                        this->lookup[marker] = true;

                        // Send peer HostInfo to all of my other client
                        dataOut = wrapMessage(BTL::MessageType::HOSTINFO, this->localPort, sender);
                        for (auto peer : this->tracker->peers()){
                            if (peer.host() == sender->host() and peer.port() == sender->port()){
                                continue;
                            }
                            networkSend(peer.host(), peer.port(), dataOut);
                        }

                        // Send self ClientInfo to the sender
                        this->tracker->set_remotehost(peerHost);
                        this->tracker->set_localport(this->localPort);
                        dataOut = wrapMessage(BTL::MessageType::CLIENTINFO, this->localPort, this->tracker);
                        networkSend(peerHost, peerClientInfo.localport(), dataOut);
                    }

                    // Add peer's client list to my client list, skip if already added
                    for (auto peer : peerClientInfo.peers()){
                        marker = peer.host() + ":";
                        marker += std::to_string(peer.port());

                        if (this->lookup.find(marker) == this->lookup.end()){
                            // Peer not found, add new
                            BTL::HostInfo* a = this->tracker->add_peers();
                            a->set_host(peer.host());
                            a->set_port(peer.port());
                            a->set_isseeder(peer.isseeder());
                            this->lookup[marker] = true; 
                        }
                    }
                    // Show client list
                    if (DEBUG) {
                        std::cout << "Sockpeer::Sockpeer this client now contains " << this->tracker->peers_size() << " peer(s)\n";
                        for (auto i: this->tracker->peers()){
                            printf("%s:%d - %d\n", i.host().c_str(), i.port(), i.isseeder());
                        }
                    }
                    doneWork = true;
                }
                else if (peerMessageType.message() == BTL::MessageType::FILEINFO){
                    // Server should not read this message
                    if (this->isSeeder){
                        printf("Err? FILEINFO\n");
                        continue;
                    }
                    // One file at a time 
                    if (fileHandle != -1){
                        printf("Sockpeer::run this client already open ofstream\n");
                        continue;
                    }

                    // Only non-seeder can get this message
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
                    // memset(buffer, '0', block_count);
                    // blockDone = std::string(buffer, block_count);

                    // 
                    fileHandle = open(fileName.c_str(), O_RDWR | O_CREAT, (mode_t)0600);
                    if (ftruncate(fileHandle, fileSize) != 0){
                        printf("Sockpeer::run file trucate failed: %s\n", strerror(errno));
                        close(fileHandle);
                        continue;
                    }
                    fsync(fileHandle);

                    fileBuffer = (char *)mmap(0, fileSize, PROT_READ | PROT_WRITE, MAP_SHARED, fileHandle, 0);

                    printf("Sockpeer::run Client open %s\n", fileName.c_str());
                    // Mark time begin
                    startTime = time(NULL);
                    // Bounce this message to other client
                    // for (auto peer: this->tracker->peers()){
                    //     if (!peer.isseeder()){
                    //         // Send file info to all peer that is not seeder
                    //         dataOut = wrapMessage(BTL::MessageType::FILEINFO, this->localPort, &fileInfo);
                    //         networkSend(peer.host(), peer.port(), dataOut);  
                    //     }
                    // }
                    doneWork = true;
                }
                else if (peerMessageType.message() == BTL::MessageType::FILEDATA){
                    BTL::FileData fileData;
                    google::protobuf::util::ParseDelimitedFromZeroCopyStream(&fileData, &zstream, &clean_eof);
                    // Seeder should not read this message
                    if (this->tracker->isseeder()){
                        printf("Err? FILEDATA\n");
                        continue;
                    }
                    int block_i = fileData.offset() / this->dataSize;
                    // If this block is already set, skip it
                    if (blockMark[block_i] == '0'){
                        continue;
                    }
                    // Create new thread to deal with data
                    // std::thread threadObj(parseFunction, std::ref(this->tracker), fileData, peerHost, peerPort);
                    // threadObj.detach();
                    memcpy(fileBuffer + fileData.offset(), fileData.data().c_str(), fileData.data().length());

                    // Bounce back to other client
                    for (auto peer: this->tracker->peers()){
                        if (peer.isseeder()){
                            continue;
                        }
                        if (peer.host() == peerHost and peer.port() == peerPort){
                            continue;
                        }
                        // Send file info to all peer that is not server and not the one who sent this message
                        std::string dataOut = wrapMessage(BTL::MessageType::FILEDATA, this->localPort, &fileData);
                        networkSend(peer.host(), peer.port(), dataOut);  
                    }

                    // Mark block when done?
                    blockMark[block_i] = '0';
                    remain_block = std::count(blockMark.begin(),blockMark.end(),'1');
                    if (DEBUG) printf("%s:%d <- block %d\t REM: %d\n", peerHost.c_str(), peerPort, block_i, remain_block);
                        
                    if (remain_block == 0){
                        std::cout << "Sockpeer::run Received all\n";
                        printf("Download time: %lu sec\n", time(NULL) - startTime);
                        msync(fileBuffer, fileSize, MS_SYNC);

                        // Send done to all
                        BTL::CommonReply reply;
                        reply.set_status(-1);
                        reply.set_localport(this->localPort);
                        dataOut = wrapMessage(BTL::MessageType::COMMONREPLY, this->localPort, &reply);
                        for (auto peer: this->tracker->peers()){
                            networkSend(peer.host(), peer.port(), dataOut);
                        }
                        // break;
                    }
                    doneWork = true;
                }
                else if (peerMessageType.message() == BTL::MessageType::COMMONREPLY){
                    BTL::CommonReply reply;
                    google::protobuf::util::ParseDelimitedFromZeroCopyStream(&reply, &zstream, &clean_eof);

                    if (reply.status() == -1){
                        // Some peer is done, update it to seeder
                        // printf("Getting %s:%d\n", peerHost.c_str(), peerPort);
                        bool isDone = true;
                        for (int i = 0; i<this->tracker->peers_size(); i++){
                            auto peer = this->tracker->mutable_peers(i);
                            if (peer->host() == peerHost and peer->port() == peerPort){
                                peer->set_isseeder(true);
                            }
                            if (peer->isseeder() == false){
                                isDone = false;
                            }
                            // printf("%s:%d - %d\n", peer->host().c_str(), peer->port(), peer->isseeder());
                        }

                        if (isDone){
                            printf("All peers done\n");
                            printf("All time: %lu sec\n", time(NULL) - startTime);
                            printf("Ask time: %d\n", askTime);
                            close(fileHandle);
                            munmap(fileBuffer, fileSize);
                        }
                        doneWork = true;
                        continue;   
                    }
                    // When peer received this message, other peer is requesting for some block
                    else if (blockMark[reply.status()] == '1'){
                        // Block is not written, skip
                        continue;
                    }
                    // printf("Sockpeer::run Client %s:%d is asking for block %d\n", peerHost.c_str(), reply.localport(), reply.status());
                    if (DEBUG) printf("%s:%d ask for %d\n", peerHost.c_str(), reply.localport(), reply.status());
                    askTime++;
                    // char buffer[this->dataSize];
                    int block_i = reply.status();
                    int read_length = (fileSize - (block_i * this->dataSize)) < this->dataSize ? (fileSize - (block_i * this->dataSize)) : this->dataSize;
                    
                    // Prepare data
                    BTL::FileData fileData;
                    fileData.set_filename(fileName);
                    fileData.set_offset(block_i * this->dataSize);
                    fileData.set_data(std::string(fileBuffer + (block_i * this->dataSize), read_length));
                    // Send to that client
                    dataOut = wrapMessage(BTL::MessageType::FILEDATA, this->localPort, &fileData);
                    networkSend(peerHost, reply.localport(), dataOut);
                    doneWork = true;
                }
                else {
                    std::cout << "Command not found\n";
                }
            }
        }
        if (this->isSeeder or doneWork == true) {
            timeout = 0;
            timeout_hit = 0;
            continue;
        }
        // ask all peer for non written block
        if (fileHandle != -1){
            // Stand by mode 
            if (remain_block == 0){
                continue;
            }
            // Ask seeder for last_unknown_block
            for (last_unknown_block = 0; last_unknown_block < block_count; last_unknown_block++){
                if (blockMark[last_unknown_block] == '1'){
                    break;
                }
            }
            for (auto peer: this->tracker->peers()){
                // if (peer.isseeder()){
                    // auto peer = this->tracker->peers(rand() % this->tracker->peers_size());
                    BTL::CommonReply reply;
                    reply.set_localport(this->localPort);
                    reply.set_status(last_unknown_block);
                    dataOut = wrapMessage(BTL::MessageType::COMMONREPLY, this->localPort, &reply);
                    networkSend(peer.host(), peer.port(), dataOut);
                // }
            }
            timeout = (int)pow(2.0,timeout_hit);
            timeout_hit++;
            // usleep(1);
        }
    }
    return;
};

// Destructor
Sockpeer::~Sockpeer(){
    return;
};














































