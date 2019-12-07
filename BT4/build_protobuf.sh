#!/usr/bin/env bash

if [[ ! -f $(which protoc) ]]; then
	git clone https://github.com/protocolbuffers/protobuf.git && cd protobuf && git submodule update --init --recursive 
	cd ./protobuf
	./autogen.sh
	if [ "$(uname)" == "Darwin" ]; then
		./configure     
	elif [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then
		./configure --disable-shared CFLAGS="-fPIC -no-pie"
	make && make install
else
	echo "Protobuf is installed"
fi