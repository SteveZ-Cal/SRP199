#ifndef HMLIB_H
#define HMLIB_H

#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <future>
#include <mutex>
#include <cstring>
#include <limits.h>
#include <chrono>
#include <tuple>
#include <time.h>
#include <deque>
#include <thread>

#include "CL/cl_ext_xilinx.h"
#include "xcl2.hpp"
#include "experimental/xclbin_util.h"
#include <uuid/uuid.h>
#include <xclhal2.h>
#include <x86intrin.h>
#include <emmintrin.h>

#include "helpers.h"

#define MAX_BATCH_SIZE 4

struct HMLibUniqueHandler{
	//64
	unsigned int oneEntry;
	unsigned int bufferSections;
	unsigned int outSize;
	unsigned int inputSize;
	unsigned int metaSize;
	unsigned int HMLibID;
	char* metaStart;
	char* metaEnd;
	char* inputStart;
	char* inputEnd;
	char* outputStart;

	char* outputEnd;
	char pad1[56];

	//64 bytes
	char* inputMetaPtr;
	char* inputPtr;
	unsigned int programCounter;
	uint64_t totalSize;
	uint64_t timeWaitSend;
	uint64_t copyTimeIn;
	uint64_t oneSendTime;
	char pad2[20];

	//64
	char* outputMetaPtr;
	char* outputPtr;
	uint64_t copyTimeOut;
	uint64_t latencies;
	uint64_t prefetchHelp;
	uint64_t computeLatency;
	uint64_t threadProcessed;
	char pad3[8];

	//64
	uint64_t oneReadTime;
	uint64_t overallTime;
	uint64_t timeWaitRead;
	char pad[40];

	std::atomic<unsigned int>* full;
};

class HMLib{
	private:
		std::mutex printLock;
		
		cl::CommandQueue q;
		cl::Device device;
		cl::Context context;
		cl::Kernel HMLibKernel;
		cl::Kernel userKernel;

		cl::Buffer HMLibKernelMemory[HMLIB_HANDLERS];

		struct HMLibUniqueHandler hostMemStates[HMLIB_HANDLERS];

		bool didInitialize;

		char* HMLibMappedMem[HMLIB_HANDLERS];

		bool hmStatesTracker[HMLIB_HANDLERS];
	public:
		HMLib();
		~HMLib();
		bool initialize(const std::string binaryFile, const std::string kernelName, const unsigned int bufferSections, const unsigned int inputSize, const unsigned int outputSize);

		struct HMLibUniqueHandler* getHMLibUniqueHandler(unsigned int HMid);
		bool returnHMLibUniqueHandler(struct HMLibUniqueHandler* val, unsigned int HMid);

		int sendInput(const char* buffer[MAX_BATCH_SIZE], const unsigned int sizes[MAX_BATCH_SIZE], const unsigned int batchRequest, unsigned int& batched, const uint16_t code, const uint64_t timeoutNS, struct HMLibUniqueHandler* hmo);
		int checkOutput(char* outBuffer[MAX_BATCH_SIZE], unsigned int outSizes[MAX_BATCH_SIZE], unsigned int& batchProcessed, const uint64_t timeoutNS, struct HMLibUniqueHandler* hmo);
		
		bool checkMemoryValue(char* memory, const unsigned int programCounter, const uint16_t code);
		bool stopKernel();
		void printForMe(std::string message);

		void printStatistics(double& overallTime);
};

#endif
