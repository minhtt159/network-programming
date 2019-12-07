#include "clientserver.h"
#include <google/protobuf/util/delimited_message_util.h>
namespace util = google::protobuf::util;

int DEBUG = 0;
char menu[] = "1. Download <file>\n2. Upload <file>\n3. ListFileServer\n4. ListFileClient\n";

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
    util::SerializeDelimitedToOstream(*message, &stream);
    // Write Message to stream
    util::SerializeDelimitedToOstream(*msgData, &stream);
    // return stream
    return stream.str();
};

void listSharedFile(std::list<fileObject>* fileObjectList){
    std::string path = "./SharedFolder";
    int index = 0;
    for (const auto & entry : fs::directory_iterator(path)){
        if (entry.is_directory()){
            continue;
        }
        std::string relativePath = entry.path().u8string();
        std::string fileName = entry.path().filename().u8string();
        int fileHandle = open(relativePath.c_str(),O_RDONLY);
        if (fileHandle == -1){
            // Can't open file
            continue;
        }
        struct stat sb;
        fstat(fileHandle, &sb);
        size_t fileSize = sb.st_size;
        char buffer[fileSize];
        if (read(fileHandle, buffer, fileSize) != fileSize) {
            // Some error
            if (DEBUG) printf("Some error in listSharedFile\n");
            close(fileHandle);
            continue;
        }
        lseek(fileHandle, 0, SEEK_SET);
        std::string fileHash = md5(std::string(buffer, fileSize));

        fileObjectList->push_back({ fileHandle, fileName, relativePath, fileHash, fileSize });
        index++;
    }
}

Sockpeer::Sockpeer(int localPort, std::string remoteHost, int remotePort, bool isServer){
    // BUFFSIZE, should I let user decide?
    this->BUFFSIZE   = 1460;    // MTU default 
    this->dataSize   = 1350;    // Plenty of space for header

    // Create listen socket at networkObj
    this->networkObj = new Network(localPort, this->BUFFSIZE);
    this->localPort  = localPort;
    this->isServer = isServer;

    if (!isServer) {
        // Ping Server
        this->server = new BTL::HostInfo();
        server->set_host(remoteHost);
        server->set_port(remotePort);
        server->set_issender(!isServer);

        char charReply[this->BUFFSIZE];
        std::string serverReply;
        struct sockaddr_in servaddr;

        int i = 0;
        int n;
        // Prepare data
        std::string dataOut = wrapMessage(BTL::MessageType::HOSTINFO, localPort, this->server);
        while (i++ < 10){
            // 10 attemps to call server
            memset(&charReply, 0, sizeof(charReply));
            memset(&servaddr, 0, sizeof(servaddr));
            n = networkSend(remoteHost, remotePort, dataOut);
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
        if (DEBUG) printf("\n");
        // Parse message from server
        serverReply = std::string(charReply, n);
        std::string serverHost = inet_ntoa(servaddr.sin_addr);

        BTL::MessageType serverMessageType;
        BTL::HostInfo serverHostInfo;
        std::stringstream stream = std::stringstream(serverReply);
        google::protobuf::io::IstreamInputStream zstream(&stream);
        bool clean_eof = true;
        google::protobuf::util::ParseDelimitedFromZeroCopyStream(&serverMessageType, &zstream, &clean_eof);
        clean_eof = true;
        google::protobuf::util::ParseDelimitedFromZeroCopyStream(&serverHostInfo, &zstream, &clean_eof);

        this->localAddress = serverHostInfo.host();
    }
    else {
        this->client = new BTL::ClientInfo();
    }
    listSharedFile(&this->clientObjectList);
    this->connected = true;
    return;
};

void Sockpeer::run(){
    // Helper variable
    std::string marker;
    size_t n;
    char output_buffer[this->BUFFSIZE];
    std::string dataOut;
    std::string command;
    std::string fileName;
    std::list<Job>::iterator aJob;
    // Create 2 poll for socket and stdin
    struct pollfd fds[2];

    int block_count;
    int remain_block;
    std::string blockMark;

    int downloadCount = 0;
    int uploadCount = 0;
    bool doneWork;

    int timeout = 100;

    // Multiplexing fd
    fds[0].fd = this->networkObj->recvfd;
    fds[0].events = POLLIN | POLLPRI;
    if (!this->isServer){
        printf("%s", menu);
        // fd = 0 (stdin)
        fds[1].fd = 0;
        fds[1].events = POLLIN | POLLPRI;
    }
    
    // Normally we'd check an exit condition, but for this example we loop endlessly.
    while (true) {
        doneWork = false;
        int ret = 0;
        if (this->isServer){
            ret = poll(fds, 1, timeout);
        }
        else {
            ret = poll(fds, 2, timeout);
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
            // Server don't care about fds[1]
            if (!this->isServer and fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                printf("Error - poll indicated stdin error\n");
                break;
            }

            // Check if data to read from stdin, server don't care about fds[1]
            if (!this->isServer and fds[1].revents & (POLLIN | POLLPRI)) {
                // Read input and clear event
                n = read(0, output_buffer, sizeof(output_buffer));
                if (n <= 0) {
                    printf("Error - stdin error: %s\n", strerror(errno));
                    break;
                }
                command = std::string(output_buffer, n-1);
                printf("Command: %s\n", command.c_str());
                if (command.substr(command.length() - 5) == "ogout"){
                    break;
                }
                else if (command.substr(command.length() - 5) == "lient") {
                    this->clientObjectList.clear();
                    listSharedFile(&this->clientObjectList);
                    printf("Number of files: %lu\n", this->clientObjectList.size());
                    for (auto file: this->clientObjectList){
                        printf("%s\n", file.fileName.c_str());
                    }
                }
                else if (command.substr(command.length() - 5) == "erver") {
                    // List file server
                    BTL::ListFile request = BTL::ListFile();
                    request.set_issender(true);
                    dataOut = wrapMessage(BTL::MessageType::LISTFILE, this->localPort, &request);
                    networkSend(this->server->host(), this->server->port(), dataOut);
                }
                else {
                    int pos = command.find(' ', 0);
                    if (pos == std::string::npos){
                        // No command
                        continue;
                    }
                    fileName = command.substr(pos + 1);
                    if (command[0] == 'D' or command[0] == 'd'){
                        // Download
                        BTL::FileInfo request = BTL::FileInfo();
                        request.set_issender(true);
                        request.set_isdownload(true);
                        request.set_filename(fileName);
                        dataOut = wrapMessage(BTL::MessageType::FILEINFO, this->localPort, &request);
                        networkSend(this->server->host(), this->server->port(), dataOut);
                    }
                    else if (command[0] == 'U' or command[0] == 'u'){
                        // Upload
                        bool isFound = false;
                        for (auto file: this->clientObjectList){
                            if (fileName == file.fileName){
                                BTL::FileInfo request = BTL::FileInfo();
                                request.set_issender(true);
                                request.set_isdownload(false);
                                request.set_filename(file.fileName);
                                request.set_filesize(file.fileSize);
                                request.set_filehash(file.fileHash);
                                printf("%s\n",file.fileName.c_str());
                                dataOut = wrapMessage(BTL::MessageType::FILEINFO, this->localPort, &request);
                                networkSend(this->server->host(), this->server->port(), dataOut);
                                // Add send job
                                block_count = file.fileSize / this->dataSize + 1;
                                char buffer[block_count];
                                memset(buffer, '0', block_count);
                                blockMark = std::string(buffer, block_count);
                                remain_block = std::count(blockMark.begin(),blockMark.end(),'1');

                                BTL::HostInfo peer = BTL::HostInfo();
                                peer.set_host(this->server->host());
                                peer.set_port(this->server->port());
                                this->jobList.push_back(Job(file, peer, false, blockMark, remain_block, block_count));

                                isFound = true;
                                break;
                            }
                        }
                        if (!isFound){
                            printf("File not existed\n");
                        }
                    }
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

                std::string peerData = std::string(charData, n);
                std::string peerHost = inet_ntoa(peerAddress.sin_addr);

                // Parse message
                BTL::MessageType peerMessageType;
                std::stringstream stream = std::stringstream(peerData);
                google::protobuf::io::IstreamInputStream zstream(&stream);
                bool clean_eof = true;
                google::protobuf::util::ParseDelimitedFromZeroCopyStream(&peerMessageType, &zstream, &clean_eof);
                clean_eof = true;
                size_t peerPort = peerMessageType.localport();

                if (peerMessageType.message() == BTL::MessageType::HOSTINFO) {
                    // Some peer is new to the network
                    BTL::HostInfo peer;
                    google::protobuf::util::ParseDelimitedFromZeroCopyStream(&peer, &zstream, &clean_eof);
                    
                    marker = peer.host() + ":";
                    marker += std::to_string(peer.port());
                    if (this->lookup.find(marker) == this->lookup.end()){
                        // Peer not found, add new
                        BTL::HostInfo* a = this->client->add_peers();
                        a->set_host(peer.host());
                        a->set_port(peer.port());
                        a->set_issender(peer.issender());
                        this->lookup[marker] = true;
                        dataOut = wrapMessage(BTL::MessageType::HOSTINFO, this->localPort, a);
                        networkSend(peerHost, peerPort, dataOut);
                        // Show client list
                        if (DEBUG) {
                            std::cout << "Sockpeer::Sockpeer this client now contains " << this->client->peers_size() << " peer(s)\n";
                            for (auto i: this->client->peers()){
                                printf("%s:%d - %d\n", i.host().c_str(), i.port(), i.issender());
                            }
                        }
                    }
                    else {
                        // Peer already in list, so send the packet anyway
                        BTL::HostInfo a = BTL::HostInfo();
                        a.set_host(peer.host());
                        a.set_port(peer.port());
                        a.set_issender(peer.issender());
                        dataOut = wrapMessage(BTL::MessageType::HOSTINFO, this->localPort, &a);
                        networkSend(peerHost, peerPort, dataOut);
                        // But not show client list
                    }
                    doneWork = true;
                }
                else if (peerMessageType.message() == BTL::MessageType::FILEINFO){
                    BTL::FileInfo fileInfo;
                    google::protobuf::util::ParseDelimitedFromZeroCopyStream(&fileInfo, &zstream, &clean_eof);

                    if (fileInfo.issender()){
                        // The peer asked for something
                        if (fileInfo.isdownload()) {
                            // The peer asked for some file
                            bool isFound = false;
                            for (auto file: this->clientObjectList){
                                if (fileInfo.filename() == file.fileName){
                                    // Found file, add send job
                                    block_count = file.fileSize / this->dataSize + 1;
                                    char buffer[block_count];
                                    memset(buffer, '0', block_count);
                                    blockMark = std::string(buffer, block_count);
                                    remain_block = std::count(blockMark.begin(),blockMark.end(),'1');
                                
                                    BTL::HostInfo a = BTL::HostInfo();
                                    a.set_host(peerHost);
                                    a.set_port(peerPort);
                                    this->jobList.push_back(Job(file, a, false, blockMark, remain_block, block_count));

                                    // Send file to peer
                                    BTL::FileInfo b = BTL::FileInfo();
                                    b.set_filename(file.fileName);
                                    b.set_filesize(file.fileSize);
                                    b.set_filehash(file.fileHash);
                                    b.set_issender(false);
                                    b.set_isdownload(true);
                                    dataOut = wrapMessage(BTL::MessageType::FILEINFO, this->localPort, &b);
                                    networkSend(peerHost, peerPort, dataOut);
                                    isFound = true;
                                }
                            }
                            if (!isFound){
                                // No file found, send "" file to peer
                                BTL::FileInfo a = BTL::FileInfo();
                                a.set_filename("");
                                a.set_issender(false);
                                a.set_isdownload(true);
                                dataOut = wrapMessage(BTL::MessageType::FILEINFO, this->localPort, &a);
                                networkSend(peerHost, peerPort, dataOut);
                            }
                            doneWork = true;
                            continue;
                        }
                    }
                    // Peer is gonna send some file
                    if (fileInfo.filename() == "") {
                        printf("No file found on server\n");
                        continue;
                    }
                    std::string relativePath = "./SharedFolder/" + fileInfo.filename(); 
                    int fileHandle = open(relativePath.c_str(), O_RDWR | O_CREAT, (mode_t) 0600);
                     if (ftruncate(fileHandle, fileInfo.filesize()) != 0){
                        printf("File trucate failed: %s\n", strerror(errno));
                        close(fileHandle);
                        continue;
                    }
                    fileObject file = {fileHandle, fileInfo.filename(), relativePath, fileInfo.filehash(), fileInfo.filesize()};
                    // Add received job
                    block_count = fileInfo.filesize() / this->dataSize + 1;
                    char buffer[block_count];
                    memset(buffer, '1', block_count);
                    blockMark = std::string(buffer, block_count);
                    remain_block = std::count(blockMark.begin(),blockMark.end(),'1');

                    BTL::HostInfo a = BTL::HostInfo();
                    a.set_host(peerHost);
                    a.set_port(peerPort);
                    this->jobList.push_back(Job(file, a, true, blockMark, remain_block, block_count));
                    doneWork = true;
                }
                else if (peerMessageType.message() == BTL::MessageType::FILEDATA){
                    BTL::FileData fileData;
                    google::protobuf::util::ParseDelimitedFromZeroCopyStream(&fileData, &zstream, &clean_eof);
                    
                    // Find the coresponding job
                    for (aJob = this->jobList.begin(); aJob != this->jobList.end(); aJob++){
                        if (aJob->file.fileName == fileData.filename()){
                            lseek(aJob->file.fileHandle, fileData.offset(), SEEK_SET);
                            write(aJob->file.fileHandle, fileData.data().c_str(), fileData.data().length());

                            int block_i = fileData.offset() / this->dataSize;
                            aJob->blockMark[block_i] = '0';
                            aJob->remain_block = std::count(aJob->blockMark.begin(),aJob->blockMark.end(),'1');
                            if (DEBUG) printf("Writing offset %d from %s:%zu\tREM:%d\n", fileData.offset(), peerHost.c_str(), peerPort, aJob->remain_block);

                            if (aJob->remain_block == 0){
                                // Notify to sender
                                BTL::FileCache fileCache;
                                fileCache.set_issender(false);
                                fileCache.set_filename(fileData.filename());
                                fileCache.clear_cache();
                                fileCache.add_cache(-1);
                                dataOut = wrapMessage(BTL::MessageType::FILECACHE, this->localPort, &fileCache);
                                networkSend(peerHost, peerPort, dataOut);
                                // remove job
                                downloadCount++;
                                printf("Download: %d\tUpload: %d\n", downloadCount, uploadCount);
                                this->jobList.remove(*aJob);
                                if (DEBUG) printf("Sending -1 fileCache to server, %lu jobs left\n", this->jobList.size());
                            }
                            break;
                        }
                    }
                    doneWork = true;
                }
                else if (peerMessageType.message() == BTL::MessageType::FILECACHE){
                    BTL::FileCache fileCache;
                    google::protobuf::util::ParseDelimitedFromZeroCopyStream(&fileCache, &zstream, &clean_eof);
                    // printf("fileName: %s\n", fileCache.filename().c_str());
                    // Find coresponding job
                    for (aJob = this->jobList.begin(); aJob != this->jobList.end(); aJob++){
                        if (aJob->file.fileName == fileCache.filename()){
                            // if this received message from sender, send unknown block or -1 if done
                            if (fileCache.issender()){
                                BTL::FileCache reply;
                                reply.set_issender(false);
                                reply.set_filename(fileCache.filename());
                                reply.clear_cache();
                                if (aJob->remain_block == 0){
                                    reply.add_cache(-1);
                                }
                                else {
                                    int cache_size = 0;
                                    for (int i = 0; i < aJob->block_count; i++){
                                        if (aJob->blockMark[i] == '1'){
                                            reply.add_cache(i);
                                            cache_size++;
                                        }
                                        if (cache_size > 300){
                                            break;
                                        }
                                    }
                                }
                                if (DEBUG) printf("Asking for %d blocks\n", reply.cache_size());
                                dataOut = wrapMessage(BTL::MessageType::FILECACHE, this->localPort, &reply);
                                networkSend(peerHost, peerPort, dataOut);
                            }
                            // If this received message from peer, send the cache
                            else {
                                if (fileCache.cache(0) == -1){
                                    // Job is done
                                    uploadCount++;
                                    printf("Download: %d\tUpload: %d\n", downloadCount, uploadCount);
                                    this->jobList.remove(*aJob);
                                    break;
                                }
                                for (auto block_i: fileCache.cache()){
                                    int offset = block_i * this->dataSize;
                                    int read_length = (aJob->file.fileSize - (block_i * this->dataSize)) < this->dataSize ? (aJob->file.fileSize - (block_i * this->dataSize)) : this->dataSize;
                                    char buffer[this->dataSize];
                                    lseek(aJob->file.fileHandle, offset, SEEK_SET);
                                    read(aJob->file.fileHandle, buffer, read_length);
                                    BTL::FileData a = BTL::FileData();
                                    a.set_filename(fileCache.filename());
                                    a.set_offset(offset);
                                    a.set_data(std::string(buffer, read_length));
                                    dataOut = wrapMessage(BTL::MessageType::FILEDATA, this->localPort, &a);
                                    if (DEBUG) printf("Sending block %d - %d to %s:%zu\n", block_i, read_length, peerHost.c_str(), peerPort);
                                    networkSend(peerHost, peerPort, dataOut);
                                }
                            }
                            break;
                        }
                    }
                    doneWork = true;
                }
                else if (peerMessageType.message() == BTL::MessageType::LISTFILE){
                    BTL::ListFile listFile;
                    google::protobuf::util::ParseDelimitedFromZeroCopyStream(&listFile, &zstream, &clean_eof);
                    if (listFile.issender()) {
                        // this peer asked for list of file
                        this->clientObjectList.clear();
                        listSharedFile(&this->clientObjectList);
                        BTL::ListFile a = BTL::ListFile();
                        a.set_issender(false);
                        for (auto file: this->clientObjectList){
                            a.add_listfile(file.fileName);
                        }
                        dataOut = wrapMessage(BTL::MessageType::LISTFILE, this->localPort, &a);
                        networkSend(peerHost, peerPort, dataOut);
                    }
                    else {
                        // this peer return list of files
                        printf("Number of files: %d\n", listFile.listfile_size());
                        for (auto file: listFile.listfile()){
                            printf("%s\n", file.c_str());
                        }
                    }
                }
                else {
                    std::cout << "Command not found\n";
                }
            }
        }
        if (doneWork){
            continue;
        }
        if (this->jobList.size() == 0){
            // No work, continue
            continue;
        }
        // Tap each job
        int downloadJob = 0;
        int uploadJob = 0;
        for (aJob = this->jobList.begin(); aJob != this->jobList.end(); aJob++){
            if (aJob->isDownload){
                // Do nothing but hold for command
                downloadJob++;
            }
            else {
                // Ask for fileCache
                BTL::FileCache cache = BTL::FileCache();
                cache.set_issender(true);
                cache.set_filename(aJob->file.fileName);
                dataOut = wrapMessage(BTL::MessageType::FILECACHE, this->localPort, &cache);
                networkSend(aJob->peer.host(), aJob->peer.port(), dataOut);
                if (DEBUG) printf("Asking %s:%u for file cache of %s\n", aJob->peer.host().c_str(), aJob->peer.port(), aJob->file.fileName.c_str());
                uploadJob++;
            }
        }
        printf("There are %lu jobs, ", this->jobList.size());
        printf("%d download, %d upload\n", downloadJob, uploadJob);
    }
    return;
};

// Destructor
Sockpeer::~Sockpeer(){
    return;
};














































