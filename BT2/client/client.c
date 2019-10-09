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
#include <errno.h>

// Should prepare buffer before call read & write function
ssize_t client_send(int sockfd, char* buffer, size_t len){
	ssize_t nbytes = write(sockfd, buffer, len);
	if (nbytes < 0){
		perror("Send error\n");
	}
	return nbytes;
}

ssize_t client_recv(int sockfd, char* buffer, size_t len){
	ssize_t nbytes = read(sockfd, buffer, len);
	if (nbytes < 0){
		perror("Read error\n");
	}
	return nbytes;
}
//

int main(int argc, char *argv[]){
	int buffer_size = 1024;

	if (argc != 3){
		perror("Usage: ./client <host> <port>\n");
		exit(0);
	}

	// Read host and port from argv
	char *SERVER_ADDR = argv[1];
	int PORT = strtol(argv[2], NULL, 10);

	// Create socket
	int sockfd;
	sockfd = socket(AF_INET, SOCK_STREAM, 0);	// AF_INET = IPv4, SOCK_STREAM = TCP
	if (sockfd == -1){
		perror("Create socket failed\n");
		exit(0);
	}
	// Prepare servaddr
	struct sockaddr_in servaddr;
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family      = AF_INET;
	servaddr.sin_addr.s_addr = inet_addr(SERVER_ADDR);
	servaddr.sin_port        = htons(PORT);

	// Connect client socket to server socket
	if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) != 0){
		printf("%s",strerror( errno ));
		perror("Connect failed\n");
		exit(0);
	}

	// Logic
	// TODO: try catch exception

	// Get buffer_size
	char tmp_buf[buffer_size];
	bzero(tmp_buf, sizeof(tmp_buf));
	client_recv(sockfd, tmp_buf, sizeof(tmp_buf));
	// Renew buffer
	buffer_size = strtol(tmp_buf, NULL, 10);
	char buf[buffer_size];

	// Send file name
	char fileName[buffer_size];
	printf("File name: ");
	scanf("%s", fileName);
	int n = sprintf(buf, "%s", fileName);
	client_send(sockfd, buf, n);

	// Read options
	bzero(buf, sizeof(buf));
	client_recv(sockfd, buf, sizeof(buf));
	printf("%s", buf);
	int option;
	scanf("%d", &option);

	// Send option
	bzero(buf, sizeof(buf));
	n = sprintf(buf, "%d", option);
	client_send(sockfd, buf, n);

	FILE *file;
	// Send or receive file
	if (option == 1){				// Client -> Server
		file = fopen(fileName, "rb");
		if (file == NULL){
			perror("Read file error\n");
		}
		while(!feof(file)){
			bzero(buf, sizeof(buf));
			size_t fn = fread(buf, 1, buffer_size, file);
			if (fn > 0){
				client_send(sockfd, buf, fn);
			}
			else {
				break;
			}
		}
		fclose(file);
	}
	else if (option == 2){			// Server -> Client
		file = fopen(fileName, "wb");
		if (file == NULL){
			perror("Read file error\n");
		}
		bzero(buf, sizeof(buf));
		size_t fn = 0;
		while((fn = client_recv(sockfd, buf, sizeof(buf))) >= 0){
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
	else {
		perror("Command not found");
	}
	// Close
	close(sockfd);
	return 0;
} 
