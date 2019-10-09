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
#define MAX 512

void check_in(){
	printf("Name: Tran Tuan Minh\n");
	printf("MSV : 15021754\n");
}

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
	if (argc != 2){
		perror("Usage: ./server <port>\n");
		exit(0);
	}
	check_in();	
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
		char buf[MAX];
		bzero(&buf, sizeof(buf));
		// Read "Hello server!"
		server_recv(connfd, buf, sizeof(buf));
		// Send "Hello client!"
		bzero(&buf, sizeof(buf));
		strncpy(buf, "Hello client!\n", 14);
		server_send(connfd, buf, strlen(buf));
		// Send "What is your name?"
		bzero(&buf, sizeof(buf));
		strncpy(buf, "What is your name?\n", 19);
		server_send(connfd, buf, strlen(buf));
		// Read client name
		server_recv(connfd, buf, sizeof(buf));
		printf("Client: %s", buf);
		// Read "Bye!"
		server_recv(connfd, buf, sizeof(buf));
		if (strncmp(buf, "Bye!\n", 5) == 0){
			printf("Client close connection\n");
			close(connfd);
		}
	}
	// After chatting close the socket 
	close(sockfd); 
} 
