#include <pthread.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <stdio.h>
#include <cstring>
#include "Network.h"

struct threadData{
	Network* networkObj;
	std::string dataIn;
}

// 
void* parseFunction(void* threadArgs);