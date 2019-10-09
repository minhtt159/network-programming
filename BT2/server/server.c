#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h> 	// for open
#include <unistd.h> // for close

// Should prepare buffer before call read & write function
ssize_t server_send(int sockfd, char* buffer, size_t len){
	ssize_t nbytes = write(sockfd, buffer, len);
	if (nbytes < 0){
		perror("Send error\n");
	}
	return nbytes;
}

ssize_t server_recv(int sockfd, char* buffer, size_t len){
	ssize_t nbytes = read(sockfd, buffer, len);
	if (nbytes < 0){
		perror("Read error\n");
	}
	return nbytes;
}
//

int main(int argc, char *argv[]){
	// Predefine buffer size
	size_t buffer_size = 1024;
	
	// Parser
	if (argc == 2){
		printf("Using:\n");
		printf("Listening port: %s\n", argv[1]);
	}
	else if (argc == 3){
		printf("Using:\n");
		printf("Listening port: %s\n", argv[1]);
		printf("Buffer size: %s\n", argv[2]);
		buffer_size = strtol(argv[2], NULL, 10);
	}
	else {
		perror("Usage:\n1\t./server <port>\n2\t./server <port> <buffer_size>\n");
		exit(0);
	}

	// Read PORT from argv
	int PORT = strtol(argv[1], NULL, 10);

	// Create socket
	int sockfd;
	sockfd = socket(AF_INET, SOCK_STREAM, 0);		// AF_INET = IPv4, SOCK_STREAM = TCP
	if (sockfd == -1){
		perror("Create socket failed\n");
		exit(0);
	}

	// Prepare servaddr
	struct sockaddr_in servaddr;
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family      = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);	// listen from all interface
	servaddr.sin_port        = htons(PORT);

	// Set sock opt
		// set SO_REUSEADDR on a socket to true (1):
	int optval = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);
	// Binding socket
	if ((bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr))) != 0){
		perror("Bind failed\n");
		exit(0);
	}
	// Listen on network
	if ((listen(sockfd, 5)) != 0){					// backlog = 5
		perror("Listen failed\n");
		exit(0);
	}
	printf("Bind and listen at %d\n", PORT);

	while(1){
		struct sockaddr_in cli;
		int connfd, len;
		len = sizeof(cli);
		// Accept data from client
		connfd = accept(sockfd, (struct sockaddr*)&cli, (socklen_t *)&len);
		if (connfd < 0) {
			continue;
		}
		// Show client info
		printf("Client accept from %s port %d\n", inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));
		
		// Logic
		// TODO: try catch exception

		// Send buffer size to client
		char buf[buffer_size];
		bzero(buf, sizeof(buf));
		int n = sprintf(buf, "%zu", buffer_size);
		server_send(connfd, buf, n);

		// Get file name
		bzero(buf, sizeof(buf));
		server_recv(connfd, buf, sizeof(buf));
		char fileName[buffer_size];		// Be careful with the buffer_size :) 
		strncpy(fileName, buf, strlen(buf));

		// Send options to client
		bzero(buf, sizeof(buf));
		char menu[] = "1. Upload\n2. Download\n";
		n = sprintf(buf, "%s", menu);
		server_send(connfd, buf, n);

		// Read options from client
		bzero(buf, sizeof(buf));
		server_recv(connfd, buf, sizeof(buf));
		int options = strtol(buf, NULL, 10);
		FILE *file;
		if (options == 1){				// Client -> Server
			file = fopen(fileName, "wb");
			if (file == NULL){
				perror("Read file error\n");
			}
			bzero(buf, sizeof(buf));
			size_t fn = 0;
			while((fn = server_recv(connfd, buf, sizeof(buf))) >= 0){
				if (fn == 0){
					break;
				}
				size_t rn = fwrite(buf, 1, fn, file);
				if (rn < fn){
					perror("File write error\n");
					break;
				}
				bzero(buf, sizeof(buf));
			}
			fclose(file);
		} 
		else if (options == 2){			// Server -> Client
			file = fopen(fileName, "rb");
			if (file == NULL){
				perror("Read file error\n");
			}
			while(!feof(file)){
				bzero(buf, sizeof(buf));
				size_t n = fread(buf, 1, buffer_size, file);
				if (n > 0){
					server_send(connfd, buf, n);
				}
				else {
					break;
				}
			}
			fclose(file);
		}
		else {							// Command not found
			perror("Command not found\n");
		}

		// Done
		close(connfd);
	}
	// ?
	close(sockfd); 
} 
