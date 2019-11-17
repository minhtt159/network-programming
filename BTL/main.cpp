/*
	Name: Tran Tuan Minh. StudentID: 15021754
*/
#include <iostream>
#include "clientserver.h"	// Helper

std::string helper = "Usage:\n\
- Server: ./binary port\n\
- Client: ./binary host port";

int main(int argc, char** argv) {
	// Check protobuf version
	GOOGLE_PROTOBUF_VERIFY_VERSION;

	// Read args
	bool isServer;
	std::string HOST;
	int PORT;
	if (argc == 1 or argc > 3) {
		std::cout << helper << std::endl;
		exit(-1);
	}
	else if (argc == 2) {
		isServer = true;
		HOST = "";
		PORT = strtol(argv[1], NULL, 10);
	}
	else {
		isServer = false;
		HOST = argv[1];
		PORT = strtol(argv[2], NULL, 10);
	}

	// Join the network
	Sockpeer peer = Sockpeer(HOST, PORT, isServer);
	// If cannot join / create, exit
	if (!peer.connected) {
		std::cerr << "Cannot connect peer to network" << std::endl;
		exit(-1);
	}

	// Run peer depend on server or client
	peer.run();
	return 1;
}



