
#include "hmlib.h"
#include <sys/mman.h>

HMLib::HMLib(){
	for(unsigned int i = 0; i < HMLIB_HANDLERS; i++){
		hmStatesTracker[i] = false;
	}

	didInitialize = false;
}

HMLib::~HMLib(){
	if(didInitialize){
		stopKernel();
		q.finish();
		std::cout << "Cleanup complete for 1st kernel" << "\n";
		q.finish();
		std::cout << "Cleanup complete for 2nd kernel" << "\n";
	}
}

bool HMLib::initialize(const std::string binaryFile, const std::string kernelName, const unsigned int bufferSections, const unsigned int inputSize, const unsigned int outputSize){
	if(didInitialize){
		return true;
	}

	std::vector<cl::Device> devices = xcl::get_xil_devices();
	std::vector<unsigned char> fileBuf = xcl::read_binary_file(binaryFile);
	cl::Program::Binaries bins{ {fileBuf.data(), fileBuf.size()} };
	unsigned int valid_device = 0;
	cl_int err = 0;

	for (unsigned int i = 0; i < devices.size(); i++) {
		device = devices[i];
		// Creating Context and Command Queue for selected Device
		context = cl::Context(device, NULL, NULL, NULL, &err);
		if(err != CL_SUCCESS){
			std::cerr << "Could not create context device, error number: " << err << "\n";
			return false;
		}

		q = cl::CommandQueue(context, device, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE|CL_QUEUE_PROFILING_ENABLE, &err);
		if(err != CL_SUCCESS){
			std::cerr << "Could not create command queue, error number: " << err << "\n";
			return false;
		}
		std::cout << "Trying to program device[" << i << "]: " << device.getInfo<CL_DEVICE_NAME>() << std::endl;
		#ifndef HW_SIM
		if (device.getInfo<CL_DEVICE_NAME>() != "xilinx_u250_gen3x16_xdma_shell_4_1") {
		#else
		if (device.getInfo<CL_DEVICE_NAME>() != "xilinx_u250_gen3x16_xdma_4_1_202210_1") {
		#endif
			continue;
		}

		cl::Program program(context, { device }, bins, NULL, &err);
	
		if(err != CL_SUCCESS){
			std::cerr << "Failed to program device[" << i << "] with xclbin file!\n";
		}else{
			std::cout << "Device[" << i << "]: program successful!\n";
			// Creating Kernel

			HMLibKernel = cl::Kernel(program, "memAccelerate", &err);
			if(err != CL_SUCCESS){
				std::cerr << "Could not create HMLib kernel, error number: " << err << "\n";
				return false;
			}
			userKernel = cl::Kernel(program, kernelName.c_str(), &err);
			if(err != CL_SUCCESS){
				std::cerr << "Could not create look up accelerate kernel, error number: " << err << "\n";
				return false;
			}
		}

		valid_device++;
		break; // we break because we found a valid device
	}

	if(valid_device == 0) {
		std::cerr << "Failed to program any device found, exit!\n";
		return false;
	}

	//Enable memory for large NDT table space for NDT Lookup
	cl_mem_ext_ptr_t hostBufferExt;
	hostBufferExt.flags = XCL_MEM_EXT_HOST_ONLY;
	hostBufferExt.obj = nullptr;
	hostBufferExt.param = 0;

	hostMemStates[0].metaSize = 64 * sizeof(char);
	hostMemStates[0].inputSize = inputSize;
	hostMemStates[0].outSize = outputSize/* + hostMemStates[0].metaSize*/;
	hostMemStates[0].oneEntry = hostMemStates[0].inputSize + hostMemStates[0].metaSize + hostMemStates[0].outSize;
	hostMemStates[0].bufferSections = bufferSections;

	for(unsigned int i = 0; i < HMLIB_HANDLERS; i++){
		HMLibKernelMemory[i] = cl::Buffer(context, CL_MEM_READ_WRITE | CL_MEM_EXT_PTR_XILINX, hostMemStates[0].oneEntry * hostMemStates[0].bufferSections, &hostBufferExt, &err);
		if(err != CL_SUCCESS){
			std::cerr << "Could not allocate buffer for HMLibKernelMemory, error number: " << err << "\n";
			return EXIT_FAILURE;
		}
	}

	//Enqueue kernel to start HMLib kernel
	int argN = 0;
	err = HMLibKernel.setArg(argN, (unsigned int)hostMemStates[0].bufferSections);
	if(err != CL_SUCCESS){
		std::cerr << "Could not set argument for bundle host memory accelerate kernel, error number: " << err << "\n";
		return EXIT_FAILURE;
	}
	argN++;

	err = HMLibKernel.setArg(argN, (unsigned int)(hostMemStates[0].inputSize/BUS_WIDTH_BYTES));
	if(err != CL_SUCCESS){
		std::cerr << "Could not set argument for bundle host memory accelerate kernel, error number: " << err << "\n";
		return EXIT_FAILURE;
	}
	argN++;

	err = HMLibKernel.setArg(argN, (unsigned int)(hostMemStates[0].outSize/BUS_WIDTH_BYTES));
	if(err != CL_SUCCESS){
		std::cerr << "Could not set argument for bundle host memory accelerate kernel, error number: " << err << "\n";
		return EXIT_FAILURE;
	}
	argN++;
	
	for(unsigned int i = 0; i < HMLIB_HANDLERS; i++){
		err = HMLibKernel.setArg(argN, HMLibKernelMemory[i]);
		if(err != CL_SUCCESS){
			std::cerr << "Could not set argument for bundle host memory accelerate kernel, error number: " << err << "\n";
			return EXIT_FAILURE;
		}
		argN++;
	}


	//Map to host for setting values and set other copies of hmStates;
	for(unsigned int i = 1; i < HMLIB_HANDLERS; i++){
		hostMemStates[i].oneEntry = hostMemStates[0].oneEntry;
		hostMemStates[i].bufferSections = hostMemStates[0].bufferSections;
		hostMemStates[i].outSize = hostMemStates[0].outSize;
		hostMemStates[i].inputSize = hostMemStates[0].inputSize;
		hostMemStates[i].metaSize = hostMemStates[0].metaSize;
	}

	for(unsigned int i = 0; i < HMLIB_HANDLERS; i++){
		std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();

		HMLibMappedMem[i] = (char*)q.enqueueMapBuffer(HMLibKernelMemory[i], CL_TRUE, CL_MAP_WRITE, 0, hostMemStates[i].oneEntry * hostMemStates[i].bufferSections, nullptr, nullptr, &err);
		if(err != CL_SUCCESS){
			std::cerr << "Could not map HMLibMappedMem, error number: " << err << "\n";
			return EXIT_FAILURE;
		}

		std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
        	std::chrono::duration<double> duration = t2 - t1;
        	std::cout << "MAP TIME NS: " <<  std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count() << "\n";

		hostMemStates[i].HMLibID = i;
		hostMemStates[i].metaStart = HMLibMappedMem[i];
		hostMemStates[i].metaEnd = HMLibMappedMem[i] + hostMemStates[i].bufferSections * hostMemStates[i].metaSize;
		hostMemStates[i].inputStart = hostMemStates[i].metaEnd;
		hostMemStates[i].inputEnd = hostMemStates[i].inputStart + hostMemStates[i].inputSize * hostMemStates[i].bufferSections;
		hostMemStates[i].outputStart = hostMemStates[i].inputEnd;

		hostMemStates[i].outputEnd = hostMemStates[i].outputStart + hostMemStates[i].outSize * hostMemStates[i].bufferSections;
	
		hostMemStates[i].inputMetaPtr = hostMemStates[i].metaStart;
		hostMemStates[i].inputPtr = hostMemStates[i].metaEnd;
		hostMemStates[i].programCounter = 0;
		hostMemStates[i].totalSize = 0;
		hostMemStates[i].timeWaitSend = 0;
		hostMemStates[i].copyTimeIn = 0;
		hostMemStates[i].oneSendTime = 0;
		
		hostMemStates[i].outputMetaPtr = hostMemStates[i].metaStart;
		hostMemStates[i].outputPtr = hostMemStates[i].outputStart;
		hostMemStates[i].copyTimeOut = 0;
		hostMemStates[i].latencies = 0;
		hostMemStates[i].prefetchHelp = 0;
		hostMemStates[i].computeLatency = 0;
		hostMemStates[i].threadProcessed = 0;

		hostMemStates[i].oneReadTime = 0;
		hostMemStates[i].overallTime = 0;
		hostMemStates[i].timeWaitRead = 0;

		hostMemStates[i].full = new std::atomic<unsigned int>();
		*(hostMemStates[i].full) = 0;

		memset(HMLibMappedMem[i], 0, hostMemStates[i].metaSize * hostMemStates[i].bufferSections + hostMemStates[i].oneEntry * hostMemStates[i].bufferSections);

		std::cout << "INSPECT HANDLER META BUFFER INITIALIZE: " << i << " " << (void*)HMLibMappedMem[i] << "\n";
		for(unsigned int k = 0; k < hostMemStates[i].bufferSections; k++){
			std::cout << "META BUFFER SECTION: " << k << "\n";
			for(unsigned int l = 0; l < hostMemStates[i].metaSize; l++){
				std::cout << (int)*(HMLibMappedMem[i] + hostMemStates[i].metaSize*k + l) << " " << " ";
			}
			std::cout << "\n";
		}
	}

	q.enqueueTask(HMLibKernel);
	q.enqueueTask(userKernel);

	std::cout << "Initialization complete" << "\n";
	didInitialize = true;
	return true;
}

struct HMLibUniqueHandler* HMLib::getHMLibUniqueHandler(unsigned int HMLibID){
	if(!didInitialize){
		std::cerr << "HMLib Object not initialized! Initialize before calling getHMLibUniqueHandler." << "\n";
		return nullptr;
	}
	if(hmStatesTracker[HMLibID]){
		std::cerr << "Someone is using this HMLib object. ID: " << HMLibID << "\n";
		return nullptr;
	}

	struct HMLibUniqueHandler* val = new struct HMLibUniqueHandler;
	
	memcpy(val, &hostMemStates[HMLibID], sizeof(struct HMLibUniqueHandler));
	
	hmStatesTracker[HMLibID] = true;
	return val;
}

bool HMLib::returnHMLibUniqueHandler(struct HMLibUniqueHandler* val, unsigned int HMLibID){
	if(!didInitialize){
		std::cerr << "HMLib Object not initialized! Initialize before calling returnHMLibUniqueHandler." << "\n";
		return false;
	}
	if(!hmStatesTracker[HMLibID]){
		std::cerr << "HMLib object. ID: " << HMLibID << " not in use." << "\n";
		return false;
	}
	
	memcpy(&hostMemStates[HMLibID], val, sizeof(struct HMLibUniqueHandler));
	
	delete val;
	val = nullptr;
	hmStatesTracker[HMLibID] = false;
	return true;
}

int HMLib::sendInput(const char* buffer[MAX_BATCH_SIZE], const unsigned int sizes[MAX_BATCH_SIZE], const unsigned int batchRequest, unsigned int& batched, const uint16_t code, const uint64_t timeoutNS, struct HMLibUniqueHandler* hmo){
	if(!didInitialize){
		printLock.lock();
		std::cerr << "HMLib Object not initialized! Initialize before calling sendInput." << "\n";
		printLock.unlock();
		return -2;
	}
	if(hmo == nullptr){
		printLock.lock();
		std::cerr << "HMLibUniqueHandler passed is not initialized. Call getHMLibUniqueHandler." << "\n";
		printLock.unlock();
		return -2;
	}

	std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();

	unsigned int currentPE = hmo->programCounter % 1;//PE_PER_HANDLER;
	char* metaPtr = hmo->inputMetaPtr;
	char* inputPtr = hmo->inputPtr;

	while(true){
		if(hmo->full->load() == hmo->bufferSections){
			std::chrono::steady_clock::time_point giveUpTimer = std::chrono::steady_clock::now();
			std::chrono::duration<double> missed = giveUpTimer - t1;
			uint64_t missedTime = std::chrono::duration_cast<std::chrono::nanoseconds>(missed).count();

			if(missedTime >= timeoutNS && timeoutNS != 0){
				hmo->timeWaitSend += missedTime;
				return -1;
			}
		}else{
			std::chrono::steady_clock::time_point spinTimer = std::chrono::steady_clock::now();
			std::chrono::duration<double> missed = spinTimer - t1;
			uint64_t missedTime = std::chrono::duration_cast<std::chrono::nanoseconds>(missed).count();

			hmo->timeWaitSend += missedTime;
			break;
		}
	}

	unsigned int totalSize = 0;
	batched = 0;
	for(unsigned int i = 0; i < batchRequest; i++){
		if(totalSize + sizes[i] <= hmo->inputSize){
			batched++;
			totalSize += sizes[i];
			totalSize = customRound(totalSize,64);
			hmo->totalSize += sizes[i];
		}else{
			break;
		}
	}

	if(totalSize > hmo->inputSize){
		printLock.lock();
		std::cerr << "Input " << hmo->HMLibID << " " << currentPE << ": --- Input length is too long: " << totalSize << "\n";
		printLock.unlock();
		return -2;
	}

	(*(hmo->full))++;
	hmo->programCounter++;

	if(stevez_debug)
		std::cout<<"(debug) HMLibL "<< "batch size: " << batched << "\n";

	//#ifdef HW_SIM
		totalSize = 0;
		for(unsigned int i = 0; i < batched; i++){
			memcpy(inputPtr+totalSize,buffer[i],sizes[i]);
			totalSize += sizes[i];
			if(stevez_debug){
				std::cout << "(debug) HMLib: " << "Memory copy" << std::endl;
				std::cout << "(debug) HMLib: " << "sizes" << " " << sizes[i] << std::endl;
				std::cout << "(debug) HMLib: " << "totalSize" << " " << totalSize << std::endl;
			}
			totalSize = customRound(totalSize,64);
		}
		if(stevez_debug){
			std::cout << "(debug) HMLib" << hmo->HMLibID << " " << currentPE << ": --- Inspect Input" << "\n";
			for(unsigned l = 0; l < totalSize; l++){
				printf("%c",inputPtr[l]);
			}
			std::cout << "\n";
		}

	/*#else
		unsigned count64 = 0;
		for(unsigned int i = 0; i < batched; i++){
			unsigned int localSize64 = sizes[i];
			unsigned int getSize = 0;
			localSize64 = customRound(localSize64, 64);
			for(unsigned int j = 0; j < localSize64/64; j++){
				__m512i val = _mm512_set_epi64(0,0,0,0,0,0,0,0);
				if(getSize + 64 <= sizes[i]){
					memcpy(&val, buffer[i]+64*j,sizeof(char)*64);
				}else{
					memcpy(&val, buffer[i]+64*j,sizeof(char)*(sizes[i] - getSize));
				}

				_mm512_stream_si512((__m512i*)(inputPtr+64*count64),val);
				count64++;
			}
		}
	#endif*/
	uint64_t sendTimePoint = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	unsigned int iterations = totalSize/64;
	uint16_t stp[4];
	uint16_t pc[2];
	uint16_t inSizes[8];
	uint16_t it[2];

	memcpy(stp,&sendTimePoint,sizeof(uint64_t));
	memcpy(pc,&(hmo->programCounter),sizeof(unsigned int));
	memcpy(inSizes,sizes,sizeof(unsigned int) * 4);
	memcpy(it,&iterations,sizeof(unsigned int));

	//new
	//0 send code 0-15
	//1 recv code 16-31
	//2 batchSize 32-47	
	//3 prefetch help 48-63
	//4 numof64iters 64-95	2
	//5 send size1 96-127	3
	//6 send size2 128-159	4
	//7 send size3 160-191	5
	//8 send size4 192-223	6
	//9 out size1 224-255	7
	//10 out size2 256-287	8
	//11 out size3 288-319	9
	//12 out size4 320-351	10
	//13 latency 352-415
	//14 sync for checkoutput thread 416-447
	//15 send pc 448-479
	//16 recv pc 480-511

	//0-0,1-32,2-64,3-96,4-128,5-160,6-192,7-224,8-256,9-288,10-320,11-352,12-384,13-416,14-448,15-480

	//#ifdef HW_SIM
		((uint16_t*)metaPtr)[0] = code;
		((uint16_t*)metaPtr)[2] = batched;
		((uint32_t*)metaPtr)[2] = totalSize/64;
		((uint32_t*)metaPtr)[3] = sizes[0];
		((uint32_t*)metaPtr)[4] = sizes[1];
		((uint32_t*)metaPtr)[5] = sizes[2];
		((uint32_t*)metaPtr)[6] = sizes[3];
		
		for(unsigned int i = 0; i < 4; i++){
			((uint32_t*)metaPtr)[7+i] = 0;
		}

		((uint16_t*)metaPtr)[22] = stp[0];
		((uint16_t*)metaPtr)[23] = stp[1];
		((uint16_t*)metaPtr)[24] = stp[2];
		((uint16_t*)metaPtr)[25] = stp[3];

		((unsigned int*)metaPtr)[13] = 1; 
		((unsigned int*)metaPtr)[14] = hmo->programCounter;
	/*#else
		
		__m512i val = _mm512_set_epi16(
			0,0,pc[1],pc[0],0,1,stp[3],stp[2],
			stp[1],stp[0],0,0,0,0,0,0,
			0,0,inSizes[7],inSizes[6],inSizes[5],inSizes[4],inSizes[3],inSizes[2],
			inSizes[1],inSizes[0],it[1],it[0],0,batched,0,code);

		_mm512_stream_si512((__m512i*)metaPtr,val);
	#endif*/

	#ifdef HW_SIM
		printLock.lock();
		unsigned int section = hmo->bufferSections - (hmo->metaEnd - metaPtr)/hmo->metaSize;
		std::cout << "HMLib " << hmo->HMLibID << " " << currentPE << ": --- Program counter: " << hmo->programCounter << "\n";
		std::cout << "HMLib " << hmo->HMLibID << " " << currentPE << ": --- Write data" << "\n";
		std::cout << "HMLib " << hmo->HMLibID << " " << currentPE << ": --- Inspect: ";
		for(unsigned l = 0; l < hmo->metaSize; l++){
			std::cout << (int)metaPtr[l] << " ";
		}
		std::cout << "\n";
		std::cout << "HMLib " << hmo->HMLibID << " " << currentPE << ": --- Buffer section: " << section << " Total Size: " << totalSize << "\n";
		unsigned int position = 0;
		for(unsigned int i = 0; i < MAX_BATCH_SIZE; i++){
			if(sizes[i] != 0){
				std::cout << "HMLib " << hmo->HMLibID << " " << currentPE << ": --- " << sizes[i] << "\n";
				position += sizes[i];
				position = customRound(position,64);
			}
		}
		printLock.unlock();
	#endif


	hmo->inputMetaPtr += hmo->metaSize;
	hmo->inputPtr += hmo->inputSize;
	if(hmo->inputMetaPtr == hmo->metaEnd){
		hmo->inputMetaPtr = hmo->metaStart;
		hmo->inputPtr = hmo->inputStart;
	}

	std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
	std::chrono::duration<double> duration = t2 - t1;
	hmo->copyTimeIn += std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();

	return 0;
}

int HMLib::checkOutput(char* outBuffer[MAX_BATCH_SIZE], unsigned int outSizes[MAX_BATCH_SIZE], unsigned int& batchProcessed, const uint64_t timeoutNS, struct HMLibUniqueHandler* hmo){
	if(!didInitialize){
		printLock.lock();
		std::cerr << "HMLib Object not initialized! Initialize before calling checkOutput." << "\n";
		printLock.unlock();
		return -2;
	}
	if(hmo == nullptr){
		printLock.lock();
		std::cerr << "HMLibUniqueHandler passed is not initialized. Call getHMLibUniqueHandler." << "\n";
		printLock.unlock();
		return -2;
	}

	std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();

	char* hmMetaPtr = hmo->outputMetaPtr;
	char* outputPtr = hmo->outputPtr;

	//Copy the result to new buffer

	uint64_t tl;
	unsigned int preHelp, status;
	unsigned int outputLengths[MAX_BATCH_SIZE] = {0};
	unsigned int inputLengths[MAX_BATCH_SIZE] = {0};
	char metaPtr[64];

	while(true){
		memcpy(metaPtr,hmMetaPtr,64);
		status = ((unsigned int *) metaPtr)[13];

		if(status == 2 /*&& *((unsigned int *)(outputPtr+hmo->outSize-hmo->metaSize)) == 0xDEADBEEF*/){
			std::chrono::steady_clock::time_point spinTimer = std::chrono::steady_clock::now();
			std::chrono::duration<double> missed = spinTimer - t1;
			uint64_t missedTime = std::chrono::duration_cast<std::chrono::nanoseconds>(missed).count();

			hmo->timeWaitRead += missedTime;
			break;
		}else{
			std::chrono::steady_clock::time_point giveUpTimer = std::chrono::steady_clock::now();
			std::chrono::duration<double> missed = giveUpTimer - t1;
			uint64_t missedTime = std::chrono::duration_cast<std::chrono::nanoseconds>(missed).count();

			if(missedTime >= timeoutNS && timeoutNS != 0){
				hmo->timeWaitRead += missedTime;
				return -1;
			}
		}
	}
	
	
	uint64_t receiveTimePoint = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	uint16_t batchCount = ((uint16_t*)metaPtr)[2];
	memcpy(inputLengths,metaPtr+12,sizeof(unsigned int)*batchCount);
	memcpy(outputLengths,metaPtr+28,sizeof(unsigned int)*batchCount);	
	memcpy(&tl,metaPtr+44,sizeof(uint64_t));

	preHelp = ((uint16_t*) metaPtr)[3];

	/*#ifdef HW_SIM
		printLock.lock();
		std::cout << "Thread Receiver: " << hmo->HMLibID << " --- Inspect: ";
		for(unsigned l = 0; l < 4; l++){
			std::cout << ((uint16_t*)metaPtr)[l] << " ";
		}
		for(unsigned l = 0; l < 9; l++){
			std::cout << ((unsigned int*)metaPtr)[l+2] << " ";
		}

		for(unsigned l = 0; l < 3; l++){
			std::cout << ((unsigned int*)metaPtr)[13+l] << " ";
		}
		std::cout << "\n";

		unsigned int position = 0;
		for(unsigned int i = 0; i < 4; i++){
			if(inputLengths[i] != 0){
				std::cout << "Thread Receiver: " << hmo->HMLibID << " --- Processed an input" << "\n";
				position += inputLengths[i];
				position = customRound(position,64);
			}
		}
		std::cout << "Thread Receiver: " << hmo->HMLibID << " --- If prefetch did help: " << preHelp << "\n";
		printLock.unlock();
	#endif*/
	#ifdef HW_SIM
		std::this_thread::sleep_for(std::chrono::microseconds(2));
	#endif	
	memcpy(outBuffer[0], metaPtr, hmo->metaSize);

	unsigned int totalOutSize = 0;
	for(uint16_t i = 0; i < batchCount; i++){
		outSizes[i] = outputLengths[i];
		if(i == 0){
			memcpy(outBuffer[i]+hmo->metaSize, outputPtr+totalOutSize, outputLengths[i]);
		}else{
			memcpy(outBuffer[i], outputPtr+totalOutSize, outputLengths[i]);
		}
		totalOutSize += outputLengths[i];
		totalOutSize = customRound(totalOutSize,64);
		if(stevez_debug){
			std::cout << "(debug) HMLib: " << "checkOutput" << std::endl;
			std::cout << "(debug) HMLib: " << "totalOutSize" << " " << totalOutSize << std::endl;
			std::cout << "(debug) HMLib" << ": --- Inspect Output" << "\n";
			// for(unsigned l = 0; l < totalOutSize; l++){
			// 	printf("%02X ",(unsigned)outputPtr[l]);
			// }
			int k = 0;
			while (k < totalOutSize) {
				printf("%.2X%.2X%.2X%.2X ", 
					(unsigned char)outputPtr[k], 
					(unsigned char)outputPtr[k + 1],
					(unsigned char)outputPtr[k + 2], 
					(unsigned char)outputPtr[k + 3]);
				printf("%.2X%.2X%.2X%.2X ", 
					(unsigned char)outputPtr[k + 4], 
					(unsigned char)outputPtr[k + 5],
					(unsigned char)outputPtr[k + 6], 
					(unsigned char)outputPtr[k + 7]);
				k += 8;
			}
			std::cout << "\n";
		}
	}
	

	((unsigned int *)hmMetaPtr)[13] = 0;
	//*((unsigned int *)(outputPtr+hmo->outSize-hmo->metaSize)) = 0;*/
	(*(hmo->full))--;

	//uint64_t receiveTimePoint = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	hmo->latencies += receiveTimePoint - tl;
	hmo->threadProcessed++;
	if(preHelp == 12345){
		hmo->prefetchHelp++;
	}

	hmo->outputMetaPtr += hmo->metaSize;
	hmo->outputPtr += hmo->outSize;
	if(hmo->outputMetaPtr == hmo->metaEnd){
		hmo->outputMetaPtr = hmo->metaStart;
		hmo->outputPtr = hmo->outputStart;
	}

	std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
	std::chrono::duration<double> duration = t2 - t1;
	hmo->copyTimeOut += std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();

	batchProcessed += batchCount;
	return 0;

}

bool HMLib::checkMemoryValue(char* memory, const unsigned int programCounter, const uint16_t code){
	bool correctSignal = false;

	std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
	int count = 1;
	while(true){
		unsigned int readPC;
		uint16_t readCode;
		
		readPC = ((volatile unsigned int*)memory)[15];
		readCode = ((volatile uint16_t*)memory)[1];
	
		#ifdef HW_SIM
			if(readPC == programCounter && readCode == code){
				correctSignal = true;
				break;
			}else{
				std::chrono::steady_clock::time_point giveUpTimer = std::chrono::steady_clock::now();
				std::chrono::duration<double> durationGiveUp = giveUpTimer - t1;
				if(std::chrono::duration_cast<std::chrono::seconds>(durationGiveUp).count() > 60 * count){
					printLock.lock();

					std::cout << "HMLib: --- Received: " << readPC << " but waiting for confirm: " << programCounter << "\n";
					std::cout << "HMLib: --- Received: " << readCode << " but waiting for code: " << code << "\n";

					printLock.unlock();
					count++;
				}
				if(std::chrono::duration_cast<std::chrono::seconds>(durationGiveUp).count() > 600){
					printLock.lock();
					std::cout << "Signal not received in time" << "\n";
					printLock.unlock();
					return false;
				}
			}
		#else
			if(readPC == programCounter && readCode == code){
				correctSignal = true;
				break;
			}else{
				std::chrono::steady_clock::time_point giveUpTimer = std::chrono::steady_clock::now();
				std::chrono::duration<double> durationGiveUp = giveUpTimer - t1;
				if(std::chrono::duration_cast<std::chrono::seconds>(durationGiveUp).count() > 10){
					printLock.lock();
					std::cout << "Signal not received in time" << "\n";

					std::cout << "HMLib: --- Received: " << readPC << " but waiting for confirm: " << programCounter << "\n";
					std::cout << "HMLib: --- Received: " << readCode << " but waiting for code: " << code << "\n";

					printLock.unlock();
					return false;
				}
			}
		#endif
	}
	
	return correctSignal;
}

bool HMLib::stopKernel(){
	if(!didInitialize){
		std::cerr << "HMLib Object not initialized! Initialize before calling stopKernel." << "\n";
		return false;
	}
	for(unsigned int i = 0; i < HMLIB_HANDLERS; i++){
		if(hmStatesTracker[i]){
			std::cerr << "Call returnHMLibUniqueHandler first for ID: " << hostMemStates[i].HMLibID << "\n";
			return false;
		}
	}
	for(unsigned int i = 0; i < HMLIB_HANDLERS; i++){
		unsigned int sectionMetaSend = hostMemStates[i].bufferSections - (hostMemStates[i].metaEnd - hostMemStates[i].inputMetaPtr)/hostMemStates[i].metaSize;
		unsigned int sectionMetaRecv = hostMemStates[i].bufferSections - (hostMemStates[i].metaEnd - hostMemStates[i].outputMetaPtr)/hostMemStates[i].metaSize;
		if(sectionMetaSend != sectionMetaRecv){
			std::cerr << "Results not retrieved for handler: " << hostMemStates[i].HMLibID << "\n";
			return false;
		}
	}

	uint16_t code = 1;
	bool correctSignal;

	for(unsigned int i = 0; i < HMLIB_HANDLERS; i++){
		for(unsigned int j = 0; j < /*PE_PER_HANDLER*/1; j++){
			unsigned int currentPE = hostMemStates[i].programCounter % 1;//PE_PER_HANDLER;
			char* metaPtr = hostMemStates[i].inputMetaPtr;
			std::cout << "HMLib " << hostMemStates[i].HMLibID << " " << currentPE << " --- Write exit" << "\n";
		
			hostMemStates[i].programCounter++;

			((uint16_t*)metaPtr)[0] = code;
			((uint16_t*)metaPtr)[2] = 0;
			((uint32_t*)metaPtr)[2] = 0;
			((uint32_t*)metaPtr)[3] = 0;
			((uint32_t*)metaPtr)[4] = 0;
			((uint32_t*)metaPtr)[5] = 0;
			((uint32_t*)metaPtr)[6] = 0;
			
			for(unsigned int i = 0; i < 4; i++){
				((uint32_t*)metaPtr)[7+i] = 0;
			}
			((unsigned int*)metaPtr)[14] = hostMemStates[i].programCounter;

			bool correctSignal = checkMemoryValue(hostMemStates[i].outputMetaPtr, hostMemStates[i].programCounter, code);

			((unsigned int*)metaPtr)[13] = 0;
			memset(hostMemStates[i].outputPtr, 0, hostMemStates[i].metaSize);

			hostMemStates[i].inputMetaPtr += hostMemStates[i].metaSize;
			hostMemStates[i].outputMetaPtr += hostMemStates[i].metaSize;
			hostMemStates[i].inputPtr += hostMemStates[i].inputSize;
			hostMemStates[i].outputPtr += hostMemStates[i].outSize;

			if(hostMemStates[i].inputMetaPtr == hostMemStates[i].metaEnd){
				hostMemStates[i].inputMetaPtr = hostMemStates[i].metaStart;
				hostMemStates[i].outputMetaPtr = hostMemStates[i].metaStart;
				hostMemStates[i].inputPtr = hostMemStates[i].inputStart;
				hostMemStates[i].outputPtr = hostMemStates[i].outputStart;
			}

			if(!correctSignal){
				return correctSignal;
			}
		}
	}

	std::this_thread::sleep_for(std::chrono::seconds(3));

	std::cout << "Exit completed" << "\n";
	for(unsigned int i = 0; i < HMLIB_HANDLERS; i++){
		std::cout << "HMLib " << hostMemStates[i].HMLibID << " DATA FULL/PARTIAL WRITES: " << ((uint64_t*)hostMemStates[i].metaStart)[0] << " " << ((uint64_t*)hostMemStates[i].metaStart)[1] << " " << ((uint64_t*)hostMemStates[i].metaStart)[2] << "\n";
	}
	return correctSignal;
}

void HMLib::printStatistics(double& overallTime){
	if(!didInitialize){
		std::cerr << "HMLib Object not initialized! Initialize before calling printStatistics." << "\n";
		return;
	}
	for(unsigned int i = 0; i < HMLIB_HANDLERS; i++){
		if(hmStatesTracker[i]){
			std::cerr << "Call returnHMLibUniqueHandler first for ID: " << hostMemStates[i].HMLibID << "\n";
			return;
		}
	}

	for(unsigned int i = 0; i < HMLIB_HANDLERS; i++){
		if(hostMemStates[i].threadProcessed != 0){
			std::cout << "Thread Receiver: ----- Statistics: ";
			std::cout << "Prefetch: &&& ";
			std::cout << "Inputs: @@@ ";
			std::cout << "Size (bytes): *** ";
			std::cout << "Avg Size (bytes): " << "\n";
			break;
		}
	}
	for(unsigned int i = 0; i < HMLIB_HANDLERS; i++){
		if(hostMemStates[i].threadProcessed != 0){
			std::cout << "Thread Receiver: " << i << " --- Statistics: ";
			std::cout << hostMemStates[i].prefetchHelp << " &&& ";
			std::cout << hostMemStates[i].threadProcessed << " @@@ ";
			std::cout << hostMemStates[i].totalSize << " *** ";
			std::cout << hostMemStates[i].totalSize/hostMemStates[i].threadProcessed << "\n";
		}
	}
	std::cout << "\n";
	for(unsigned int i = 0; i < HMLIB_HANDLERS; i++){
		if(hostMemStates[i].threadProcessed != 0){
			std::cout << "Thread Receiver: ----- Statistics: ";
			std::cout << "Time From Total (sec): &&& ";
			std::cout << "Time From Send (sec): @@@ ";
			std::cout << "Time From Read (sec): *** ";
			#ifdef HW_SIM
				std::cout << "Avg send (sec): %%% ";
				std::cout << "Avg read (sec): $$$ ";
				std::cout << "Avg latency (sec): &&& ";
				std::cout << "Throughput (GB/s):" << "\n";
			#else
				std::cout << "Avg send (uSec): %%% ";
				std::cout << "Avg read (uSec): $$$ ";
				std::cout << "Avg latency (uSec): &&& ";
				std::cout << "Throughput (GB/s):" << "\n";
			#endif
			break;
		}
	}
	for(unsigned int i = 0; i < HMLIB_HANDLERS; i++){
		if(hostMemStates[i].threadProcessed != 0){
			#ifdef HW_SIM
				overallTime =  (double)hostMemStates[i].overallTime/pow(1000.0,3);
			#else
				overallTime =  (double)hostMemStates[i].overallTime/pow(1000.0,1);
			#endif
			// overallTime =  (double)hostMemStates[i].overallTime/pow(1000.0,3);
			std::cout << "Thread Receiver: " << i << " --- Statistics: ";
			std::cout << (double)hostMemStates[i].overallTime/pow(1000.0,3) << " &&& ";
			std::cout << (double)hostMemStates[i].oneSendTime/pow(1000.0,3) << " @@@ ";
			std::cout << (double)hostMemStates[i].oneReadTime/pow(1000.0,3) << " *** ";
			#ifdef HW_SIM
				std::cout << ((double)hostMemStates[i].oneSendTime/pow(1000.0,3))/hostMemStates[i].threadProcessed << " %%% ";
				std::cout << ((double)hostMemStates[i].oneReadTime/pow(1000.0,3))/hostMemStates[i].threadProcessed << " $$$ ";
				std::cout << ((double)hostMemStates[i].latencies/pow(1000.0,3))/hostMemStates[i].threadProcessed << " &&& ";
				std::cout << ((double)hostMemStates[i].totalSize/pow(1024,3))/((double)hostMemStates[i].overallTime/pow(1000.0,3)) << "\n";
			#else
				std::cout << ((double)hostMemStates[i].oneSendTime/pow(1000.0,1))/hostMemStates[i].threadProcessed << " %%% ";
				std::cout << ((double)hostMemStates[i].oneReadTime/pow(1000.0,1))/hostMemStates[i].threadProcessed << " $$$ ";
				std::cout << ((double)hostMemStates[i].latencies/pow(1000.0,1))/hostMemStates[i].threadProcessed << " &&& ";
				std::cout << ((double)hostMemStates[i].totalSize/pow(1024,3))/((double)hostMemStates[i].overallTime/pow(1000.0,3)) << "\n";
			#endif
		}
	}
	std::cout << "\n";
	for(unsigned int i = 0; i < HMLIB_HANDLERS; i++){
		if(hostMemStates[i].threadProcessed != 0){
			double mb = (double)hostMemStates[i].totalSize/pow(1024.0,2);
			double sec = (double)hostMemStates[i].copyTimeIn/pow(1000.0,3);
			std::cout << "AVG COPY TIME SEND (us) (ns) (mb/s): " << ((double)hostMemStates[i].copyTimeIn/pow(1000.0,1))/hostMemStates[i].threadProcessed << " " << hostMemStates[i].copyTimeIn << " " << (mb/sec) << "\n";
			sec = (double)hostMemStates[i].copyTimeOut/pow(1000.0,3);
			std::cout << "AVG COPY TIME RECV (us) (ns) (mb/s): " << ((double)hostMemStates[i].copyTimeOut/pow(1000.0,1))/hostMemStates[i].threadProcessed << " " << hostMemStates[i].copyTimeOut << " " << (mb/sec) << "\n";
			std::cout << "AVG TIME WAIT SEND/RECV (us) (us): " << 
				(hostMemStates[i].timeWaitSend/pow(1000.0,1))/hostMemStates[i].threadProcessed << " " << (hostMemStates[i].timeWaitRead/pow(1000.0,1))/hostMemStates[i].threadProcessed << "\n";
		}
	}
	
	return;
}

void HMLib::printForMe(std::string message){
	printLock.lock();
	std::cout << message;
	printLock.unlock();
}

