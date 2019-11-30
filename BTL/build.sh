#!/bin/sh

if [[ ! -f $(which protoc) ]]; then
	echo "No Protobuf"
else
	if [ "$(uname)" == "Darwin" ]; then
		g++ -Wall main.cpp clientserver.cc network.cc md5.cpp udp.pb.cc -std=c++11 -stdlib=libc++ -lprotobuf -o macos_binary       
	elif [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then
		g++ -Wall main.cpp clientserver.cc udp.pb.cc network.cc md5.cpp -pthread -I/usr/local/include -L/usr/local/lib -lprotobuf -static -fPIC -o linux_binary
	else
		echo "Platform is not supported"
	fi
fi

