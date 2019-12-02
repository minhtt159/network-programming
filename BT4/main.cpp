/*
	Name: Tran Tuan Minh. StudentID: 15021754
*/
#include <iostream>
#include <stdlib.h>
#include "clientserver.h"	// Helper

std::string helper = "Usage:\n\
- Server: ./binary port\n\
- Client: ./binary port host:port";

int main(int argc, char** argv) {
	// Check protobuf version
	GOOGLE_PROTOBUF_VERIFY_VERSION;

	// Read args
	bool isSeeder;
	std::string remoteHost, tmp;
	int remotePort, localPort;

	if (argc == 1 or argc > 3) {
		std::cout << helper << std::endl;
		exit(-1);
	}
	else if (argc == 2) {
		isSeeder = true;
		localPort = strtol(argv[1], NULL, 10);

		remoteHost = "";
		remotePort = 0;
	}
	else {
		isSeeder = false;
		localPort = strtol(argv[1], NULL, 10);
		tmp = argv[2];
		remoteHost = tmp.substr(0, tmp.find(":"));
		remotePort = strtol(tmp.substr(tmp.find(":")+1).c_str(), NULL, 10);
	}

	// Join the network
	Sockpeer peer = Sockpeer(localPort, remoteHost, remotePort, isSeeder);
	
	// If cannot join / create, exit
	if (!peer.connected) {
		std::cerr << "Cannot connect peer to network" << std::endl;
		exit(-1);
	}

	// Run peer depend on server or client
	peer.run();
	return 1;
}



