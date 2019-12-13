#include "clientserver.h"
#include <google/protobuf/util/delimited_message_util.h>

int DEBUG = 1;
int BACKOFF = 1;
int quit_flag = 2;

void debug(const char *fmt, ...)
{
    if (DEBUG) {
        va_list ap;
        fflush(stdout);
        fprintf(stderr, "debug: ");
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
        fflush(stderr);
    }
}

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

// Serialize message before sending
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

    // Create peer info list -> this might contain nothing at start
    this->tracker      = new BTL::ClientInfo();
    this->tracker->set_localport(localPort);

    // Some helper variables
    this->localPort  = localPort;
    this->isSeeder  = isSeeder;

    this->max_windows = 4096;

    if (!isSeeder) {
        // Add server to tracker list
        BTL::HostInfo* server = this->tracker->add_peers();
        server->set_host(remoteHost);
        server->set_port(remotePort);
        server->set_isseeder(true);

        // Add server to lookup
        std::string marker = remoteHost + ":" + std::to_string(remotePort);
        this->lookup[marker] = true;
        
        // Get initial message from server
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
                debug("dataOut: %lu, n: %d\n", dataOut.size(), n);
                continue;
            }
            n = this->networkObj->networkRecv(charReply, this->BUFFSIZE, &servaddr);
            if (n != -1){
                break;
            }
            else {
                if (DEBUG) std::cout << "\rSockpeer::Sockpeer recv error, attempt " << i << ", try again" << std::flush;
            }
            i--;
        }
        // After 10 attemps, server no response
        if (n == -1){
            std::cout << "\nNo reply from server\n";
            this->connected = false;
            return;
        }
        // Parse message from server
        serverReply = std::string(charReply, n);                // Convert serverReply to string
        std::string serverHost = inet_ntoa(servaddr.sin_addr);  // Convert serverIP

        // Protobuf parser
        BTL::MessageType serverMessageType;
        BTL::ClientInfo serverClientInfo;
        std::stringstream stream = std::stringstream(serverReply);
        google::protobuf::io::IstreamInputStream zstream(&stream);
        bool clean_eof = true;
        google::protobuf::util::ParseDelimitedFromZeroCopyStream(&serverMessageType, &zstream, &clean_eof);
        clean_eof = true;
        google::protobuf::util::ParseDelimitedFromZeroCopyStream(&serverClientInfo, &zstream, &clean_eof);

        // Blacklist self from peer ClientInfo list
        marker = serverClientInfo.remotehost() + ":" + std::to_string(this->localPort);
        this->lookup[marker] = true;

        // Add peers from server's peer list
        for (auto peer: serverClientInfo.peers()) {
            marker = peer.host() + ":" + std::to_string(peer.port());
            if (this->lookup.find(marker) == this->lookup.end()){
                // If peer not found in lookup map, add new
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
        this->tracker->set_localport(localPort);
        this->tracker->set_isseeder(true);
    }
    this->connected = true;
    return;
};

void Sockpeer::finalize(){
    if (this->fileHandle == 0){
        printf("Sockpeer::finalize Why here?\n");
        return;
    }
    printf("All peers done\n");
    printf("All time: %lu sec\n", time(NULL) - this->startTime);
    printf("Asked: %d\tDup: %d\n", this->askTime, this->dupTime);

    if (close(this->fileHandle) == 0){
        this->fileHandle = 0;
        munmap(this->fileBuffer, this->fileSize);
        this->fileName = "";
        this->fileHash = "";
        this->fileSize = 0;
    }
    else {
        printf("Sockpeer::run Cannot close file\n");
    }
    this->cc_window.clear();
    quit_flag--;
}

void Sockpeer::run(){
    // Helper variable
    static std::string blockMark = "1";

    size_t n;
    char output_buffer[this->BUFFSIZE];
    std::string dataOut;
    std::string marker;

    // Create 2 poll for socket and stdin
    struct pollfd fds[2];
    // Stream for reading / writing files
    this->fileHandle = 0;
    this->fileName = "";
    this->fileHash = "";
    this->fileSize = 0;

    int block_count;
    int remain_block = 0;
    bool doneWork;

    int timeout;
    if (BACKOFF){
        timeout = 20;
    }
    else {
        timeout = 150;
    }

    // I/O Multiplexing
    fds[0].fd = this->networkObj->recvfd;
    fds[0].events = POLLIN | POLLPRI;
    if (this->isSeeder){
        // Server print listed files

        // fd = 0 (stdin)
        fds[1].fd = 0;
        fds[1].events = POLLIN | POLLPRI;
    }
    
    // Normally we'd check an exit condition, but for this example we loop endlessly.
    while (quit_flag) {
        doneWork = false;
        int ret = 0;
        if (this->isSeeder){
            ret = poll(fds, 2, timeout);    // Server will continuously ask if client is done receiving file
        }
        else {
            // if (this->tracker->isseeder()){
            //     ret = poll(fds, 1, timeout);    // Client when become a seeder, ask other peers
            // }
            // else {
            //     ret = poll(fds, 1, -1);         // Client will passively wait for a packet
            // }
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

                if (this->fileHandle != 0){
                    debug("Some file is pending, wait for client to response\n");
                    continue;
                }

                this->fileName = std::string(output_buffer, n-1);       // Convert stdin to fileName
                this->fileHandle = open(fileName.c_str(), O_RDONLY);    // Open file (must be exist)
                if (this->fileHandle < 0) {
                    // Open file failed, print error
                    debug("Sockpeer::run Open file failed, error: %s\n", strerror(errno));
                    continue;
                }

                // obtain file size
                struct stat sb;
                fstat(this->fileHandle, &sb);
                this->fileSize = sb.st_size;
                printf("Sockpeer::run Server is sending \"%s\" with fileSize = %zu to %d client(s)\n", this->fileName.c_str(), this->fileSize, this->tracker->peers_size());
                
                block_count = (this->fileSize/this->dataSize) + 1;      // Number of blocks
                char buffer[block_count];
                memset(buffer, '0', block_count);
                blockMark = std::string(buffer, block_count);           // blockMark[i] = 0 if block[i] contain written data

                // Memory map file content to fileBuffer
                this->fileBuffer = (char *)mmap(0, this->fileSize, PROT_READ, MAP_SHARED, this->fileHandle, 0);

                // Convert to string to calculate hash
                std::string rawData = std::string(this->fileBuffer, this->fileSize);
                this->fileHash = md5(rawData);                          // This might slow due to C++
                printf("Sockpeer::run File hash: %s\n", this->fileHash.c_str());

                this->startTime = time(NULL);                           // Mark start time
                this->askTime = 0;                                      // Mark how many time this peer receive FILECACHE message 

                // Send fileInfo to all peers
                BTL::FileInfo requestedFile;
                requestedFile.set_filename(this->fileName);
                requestedFile.set_filehash(this->fileHash);
                requestedFile.set_filesize(this->fileSize);
                dataOut = wrapMessage(BTL::MessageType::FILEINFO, this->localPort, &requestedFile);
                for (int i=0; i < this->tracker->peers_size(); i++){
                    auto peer = this->tracker->mutable_peers(i);
                    networkSend(peer->host(), peer->port(), dataOut);   // Send requested file to all peers
                    peer->set_isseeder(false);                          // Mark all peer is client
                }
                doneWork = true;
            }

            /* Check if data to read from socket */
            if (fds[0].revents & (POLLIN | POLLPRI)) {
                // Recv packet
                char charData[this->BUFFSIZE];
                struct sockaddr_in peerAddress;

                n = this->networkObj->networkRecv(charData, this->BUFFSIZE, &peerAddress);
                if (n == (size_t)-1){
                    // Something wrong with the data, ask to send again?
                    continue;
                }

                std::string peerData = std::string(charData, n);        // Convert peerReply to string
                std::string peerHost = inet_ntoa(peerAddress.sin_addr); // Convert peerIP

                // Protobuf parser
                BTL::MessageType peerMessageType;
                std::stringstream stream = std::stringstream(peerData);
                google::protobuf::io::IstreamInputStream zstream(&stream);
                bool clean_eof = true;
                google::protobuf::util::ParseDelimitedFromZeroCopyStream(&peerMessageType, &zstream, &clean_eof);
                clean_eof = true;

                size_t peerPort = peerMessageType.localport();          // Read peerPort

                if (peerMessageType.message() == BTL::MessageType::HOSTINFO) {
                    // Some peer is new to the network
                    BTL::HostInfo peer;
                    google::protobuf::util::ParseDelimitedFromZeroCopyStream(&peer, &zstream, &clean_eof);
                    
                    marker = peer.host() + ":" + std::to_string(peer.port());
                    if (this->lookup.find(marker) == this->lookup.end()){
                        // Peer not found, add new
                        BTL::HostInfo* a = this->tracker->add_peers();
                        a->set_host(peer.host());
                        a->set_port(peer.port());
                        a->set_isseeder(peer.isseeder());
                        this->lookup[marker] = true;
                    }
                    // Show client list
                    if (DEBUG) {
                        std::cout << "Sockpeer::Sockpeer this client now contains " << this->tracker->peers_size() << " peer(s)\n";
                        for (auto i: this->tracker->peers()){
                            printf("%s:%d - %d\n", i.host().c_str(), i.port(), i.isseeder());
                        }
                    }
                    this->askTime = 0;
                    this->dupTime = 0;
                    doneWork = true;
                }
                else if (peerMessageType.message() == BTL::MessageType::CLIENTINFO) {
                    // ClientInfo message for exchange node
                    BTL::ClientInfo peerClientInfo;
                    google::protobuf::util::ParseDelimitedFromZeroCopyStream(&peerClientInfo, &zstream, &clean_eof);
                    
                    // Blacklist self from peer ClientInfo list
                    marker = peerClientInfo.remotehost() + ":" + std::to_string(this->localPort);
                    this->lookup[marker] = true;

                    // Add peer to network
                    marker = peerHost + ":" + std::to_string(peerClientInfo.localport());
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
                        marker = peer.host() + ":" + std::to_string(peer.port());

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
                    if (this->fileHandle != 0){
                        debug("Sockpeer::run this client already open ofstream\n");
                        continue;
                    }
                    // New file will arrived, convert all peer to non seeder
                    this->tracker->set_isseeder(false);
                    for (int i=0; i < this->tracker->peers_size(); i++){
                        auto peer = this->tracker->mutable_peers(i);
                        if (peer->host() == peerHost and peer->port() == peerPort){
                            continue;
                        }
                        peer->set_isseeder(false); 
                    }
                    // Only non-seeder can get this message
                    BTL::FileInfo fileInfo;
                    google::protobuf::util::ParseDelimitedFromZeroCopyStream(&fileInfo, &zstream, &clean_eof);

                    this->fileName = fileInfo.filename();
                    this->fileHash = fileInfo.filehash();
                    this->fileSize = fileInfo.filesize();

                    // Mark helper to track written block
                    block_count = (this->fileSize/this->dataSize) + 1;              // Number of blocks
                    char buffer[block_count];
                    memset(buffer, '1', block_count);
                    blockMark = std::string(buffer, block_count);                   // blockMark[i] = 1 if block[i] not contain written data
                    remain_block = std::count(blockMark.begin(),blockMark.end(),'1');//Count for non written blocks

                    // Open/Create file
                    this->fileHandle = open(this->fileName.c_str(), O_RDWR | O_CREAT, (mode_t)0600);
                    if (ftruncate(this->fileHandle, this->fileSize) != 0){
                        printf("Sockpeer::run file trucate failed: %s\n", strerror(errno));
                        close(fileHandle);
                        continue;
                    }
                    fsync(this->fileHandle);
                    // Map file to memory (not so efficient but no need to work with files)
                    this->fileBuffer = (char *)mmap(0, this->fileSize, PROT_READ | PROT_WRITE, MAP_SHARED, this->fileHandle, 0);

                    printf("Sockpeer::run Client open %s, waiting for %5d blocks\n", this->fileName.c_str(), block_count);
                    // Mark time begin
                    startTime = time(NULL);
                    this->askTime = 0;    // Mark how many time this peer receive FILECACHE message
                    this->dupTime = 0;    // Mark how many time this peer receive duplicate FILEDATA message

                    doneWork = true;
                }
                else if (peerMessageType.message() == BTL::MessageType::FILEDATA){
                    BTL::FileData fileData;
                    google::protobuf::util::ParseDelimitedFromZeroCopyStream(&fileData, &zstream, &clean_eof);
                    // Seeder should not read this message
                    if (this->tracker->isseeder()){
                        // debug("%s:%zu send this message\n", peerHost.c_str(), peerPort);
                        this->dupTime++;
                        continue;
                    }
                    if (this->fileHandle == 0){
                        debug("Sockpeer::run FileData - No fileHandle\n");
                        continue;
                    }
                    int block_i = fileData.offset() / this->dataSize;
                    // If this block is already set, skip it
                    if (blockMark[block_i] == '0'){
                        // debug("%s:%zu send this message\n", peerHost.c_str(), peerPort);
                        this->dupTime++;
                        continue;
                    }
                    memcpy(this->fileBuffer + fileData.offset(), fileData.data().c_str(), fileData.data().length());

                    // Mark block when done?
                    blockMark[block_i] = '0';
                    remain_block = std::count(blockMark.begin(),blockMark.end(),'1');

                    // debug("%s:%zu <- block %d\t REM: %d\n", peerHost.c_str(), peerPort, block_i, remain_block);
                    std::cout << "\rREM:" << std::setw(6) << remain_block << "\tDUP:" << std::setw(6) << this->dupTime << "   " << std::flush;

                    if (remain_block == 0){
                        // If this peer is done receiving file, it become a seeder
                        printf("\nSockpeer::run Received all\n");
                        printf("Download time: %lu sec\n", time(NULL) - startTime);
                        std::string rawData = std::string(this->fileBuffer, this->fileSize);
                        if (md5(rawData) == this->fileHash) {
                            printf("FileHash is correct\n");
                        }
                        else {
                            printf("FileHash is incorrect\n");
                        }
                        msync(this->fileBuffer, this->fileSize, MS_SYNC);
                        this->tracker->set_isseeder(true);
                        // Send done to all peers
                        BTL::FileCache fileCache;
                        fileCache.set_is_seeder(false);
                        fileCache.clear_cache();
                        fileCache.add_cache(-1);
                        dataOut = wrapMessage(BTL::MessageType::FILECACHE, this->localPort, &fileCache);

                        // Check if all peer is done
                        bool isDone = true;
                        for (auto peer: this->tracker->peers()){
                            debug("FILEDATA: Sending done message to %s:%zu\n", peer.host().c_str(), peer.port());
                            networkSend(peer.host(), peer.port(), dataOut);
                            if (peer.isseeder() == false){
                                isDone = false;
                            }
                        }
                        if (isDone){
                            this->finalize();
                            blockMark = "";
                        }
                    }
                    doneWork = true;
                }
                else if (peerMessageType.message() == BTL::MessageType::FILECACHE){
                    BTL::FileCache fileCache;
                    google::protobuf::util::ParseDelimitedFromZeroCopyStream(&fileCache, &zstream, &clean_eof);
                    if (fileHandle == 0){
                        debug("Sockpeer::run FileCache - No fileHandle\n");
                        continue;
                    }
                    // If this received message from peer, send known block
                    if (fileCache.is_seeder() == false){
                        if (fileCache.cache(0) == (uint32_t)-1){
                            // Mark this peer is done
                            bool isDone = true;
                            for (int i = 0; i < this->tracker->peers_size(); i++){
                                auto peer = this->tracker->mutable_peers(i);
                                if (peer->host() == peerHost and peer->port() == peerPort){
                                    peer->set_isseeder(true);
                                    debug("Recv done message from %s:%zu\n", peer->host().c_str(), peer->port());
                                }
                                if (peer->isseeder() == false){
                                    isDone = false;
                                }
                            }
                            if (isDone and this->tracker->isseeder()){
                                this->finalize();
                                blockMark = "";
                            }
                        }
                        else {
                            for (auto block_i: fileCache.cache()){
                                if (blockMark[block_i] == '1'){
                                    // Don't know this block, skip
                                    continue;
                                }
                                int read_length = (this->fileSize - (block_i * this->dataSize)) < this->dataSize ? (this->fileSize - (block_i * this->dataSize)) : this->dataSize;
                                BTL::FileData fileData;
                                fileData.set_filename(this->fileName);
                                fileData.set_offset(block_i * this->dataSize);
                                fileData.set_data(std::string(this->fileBuffer + (block_i * this->dataSize), read_length));
                                dataOut = wrapMessage(BTL::MessageType::FILEDATA, this->localPort, &fileData);
                                // debug("Sending block %d - %d to %s:%zu\n", block_i, read_length, peerHost.c_str(), peerPort);
                                networkSend(peerHost, peerPort, dataOut);
                            }
                        }
                    }
                    // If this received message from seeder, answer it unknown block or -1 if done
                    else {
                        askTime++;
                        BTL::FileCache reply;
                        reply.set_is_seeder(false);
                        reply.clear_cache();
                        if (remain_block == 0){
                            reply.add_cache(-1);

                            dataOut = wrapMessage(BTL::MessageType::FILECACHE, this->localPort, &reply);
                            window s = window(peerHost, peerPort, -1);
                            if (this->cc_window.find(s) == this->cc_window.end()){
                                debug("FILECACHE: Send done message to %s:%zu\n", peerHost.c_str(), peerPort);
                                this->cc_window[s] = dataOut;
                            }
                        }
                        else {
                            int cache_size = 0;
                            for (int i = 0; i < block_count; i++){
                                if (blockMark[i] == '1'){
                                    reply.add_cache(i);
                                    cache_size++;
                                }
                                if (cache_size > 300){
                                    break;
                                }
                            }
                            
                            dataOut = wrapMessage(BTL::MessageType::FILECACHE, this->localPort, &reply);
                            window s = window(peerHost, peerPort, reply.cache_size());
                            if (this->cc_window.find(s) == this->cc_window.end()){
                                debug("Asking %s:%zu for %d blocks\n", peerHost.c_str(), peerPort, reply.cache_size());
                                this->cc_window[s] = dataOut;
                            }
                        }
                    }
                    doneWork = true;
                }
                else {
                    std::cout << "Command not found\n";
                }
            }
        }
        if (this->fileHandle == 0){
            // No work, continue
            continue;
        }
        if (doneWork == true){
            if (BACKOFF){
                timeout = 20;
            }
            else {
                timeout = 150;
            }
            continue;
        }
        else if (BACKOFF){
            timeout = timeout * 2;
        }
        if (this->tracker->isseeder()) {
            // Ask all non seeder peer if they are done
            BTL::FileCache cache;
            cache.set_is_seeder(true);
            cache.clear_cache();
            dataOut = wrapMessage(BTL::MessageType::FILECACHE, this->localPort, &cache);
            for (auto peer: this->tracker->peers()){
                if (peer.isseeder() == false) {
                    networkSend(peer.host(), peer.port(), dataOut);
                    debug("Asking %s:%d\n", peer.host().c_str(), peer.port());
                }
            }
            // Check for all peer done here?
        }
        else {
            while (this->cc_window.size() != 0){
                auto x = this->cc_window.begin();
                networkSend(x->first.host, x->first.port, x->second);
                this->cc_window.erase(this->cc_window.begin());
            }
        }
    }
    return;
};

// Destructor
Sockpeer::~Sockpeer(){
    return;
};














































