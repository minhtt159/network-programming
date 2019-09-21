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
#define MAX 512

void check_in(){
	printf("Name: Tran Tuan Minh\n");
	printf("MSV : 15021754\n");
}

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
	if (argc != 3){
		perror("Usage: ./client <host> <port>\n");
		exit(0);
	}
	check_in();
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
	char buf[MAX];
	char name[MAX-40];
	bzero(&name, sizeof(name));

	// Send "Hello server!"
	bzero(&buf, sizeof(buf));
	strncpy(buf, "Hello server!\n", 14);
	client_send(sockfd, buf, strlen(buf));
	// Read "Hello client!"
	client_recv(sockfd, buf, sizeof(buf));
	printf("%s",buf);
	// Read "What is your name?"
	client_recv(sockfd, buf, sizeof(buf));
	printf("%s",buf);
	// Scan name
	scanf("%s",name);
	bzero(buf, sizeof(buf));
	strncpy(buf, "My name is ", 11);
	strncpy(buf + 11, name, strlen(name));
	strncpy(buf + 11 + strlen(name), "! Nice to meet you!\n", 20);
	// Send "My name is ...! Nice to meet you!"	
	client_send(sockfd, buf, strlen(buf));
	// Send "Bye!"
	bzero(&buf, sizeof(buf));
	strncpy(buf, "Bye!\n", 5);
	client_send(sockfd, "Bye!\n", strlen(buf));

	// Close
	close(sockfd);
	return 0;
} 
