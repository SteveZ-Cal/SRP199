#include "xcl2.hpp"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdint.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <random>
#include <limits.h>
#include <thread>
#include <filesystem>
#include <sys/mman.h>

uint32_t crc32_for_byte(uint32_t r){
        for(int j = 0; j < 8; j++){
                r = (r & 1? 0: (uint32_t)0xEDB88320L) ^ r >> 1;
        }
        return r ^ (uint32_t)0xFF000000L;
}

uint32_t crc32(const void *data, size_t n_bytes){
        uint32_t table[256];
        uint32_t crc = 0;

        for(uint32_t i = 0; i < 256; i++){
                table[i] = crc32_for_byte(i);
        }
        for(uint32_t i = 0; i < n_bytes; i++){
                crc = table[(uint8_t)crc ^ ((uint8_t*)data)[i]] ^ crc >> 8;
        }
        return crc;
}

int crc_test(const std::string& binary, const std::string path){
	
	std::vector<int> fileSize;
	std::vector<char*> fileData;
	
	int entryCount = 0;
	uint64_t totalInputSize = 0;
	std::vector<std::filesystem::path> entryPath;
	for (const auto & entry : std::filesystem::recursive_directory_iterator(path)){
		if(entry.is_regular_file()){
			entryCount++;
			entryPath.push_back(entry.path());
			std::ifstream inFile(entry.path().string().c_str(), std::ifstream::binary);
			if(!inFile){
				std::cout << "Unable to open input file" << std::endl;
				exit(EXIT_FAILURE);
			}

			int inputSize;

			inFile.seekg(0, inFile.end);
			inputSize = inFile.tellg();
			inFile.seekg(0, inFile.beg);

			char* tmpPtr = new char[inputSize];

			inFile.read(tmpPtr, inputSize);
			fileData.push_back(tmpPtr);
			fileSize.push_back(inputSize);
			inFile.close();

			totalInputSize += inputSize;
		}
	}

	const int modVal = 32;
	std::vector<char*> in;
	std::vector<int*> outputCRC;
	std::vector<cl::Buffer> inOCL(modVal);
	std::vector<cl::Buffer> outputCRCOCL(modVal);
	std::vector<unsigned int> crcAnswers;

	for(int i = 0; i < entryCount; i++){
		char* ptrIn; 
		int* ptrCRC;

		posix_memalign((void**)&ptrIn, 4096, fileSize[i]);
		posix_memalign((void**)&ptrCRC, 4096, 64);

		if(madvise(ptrIn, fileSize[i], MADV_HUGEPAGE) != 0){
                        std::cout << "HUGE PAGE ADVICE IGNORED" << "\n";
                }


		in.push_back(ptrIn);
		outputCRC.push_back(ptrCRC);
	}

	for(unsigned int i = 0; i < fileData.size(); i++){
                unsigned int val = crc32(fileData[i], fileSize[i]);
                crcAnswers.push_back(val);
        }

	cl_int err;
	std::vector<cl::Device> devices = xcl::get_xil_devices();
	cl::Device device;

	for(unsigned int i = 0; i < devices.size(); i++){
		device = devices[i];

		std::cout << "Trying to program device[" << i << "]: " << device.getInfo<CL_DEVICE_NAME>() << std::endl;
		#ifndef HW_SIM
		if (device.getInfo<CL_DEVICE_NAME>() == "xilinx_u250_gen3x16_xdma_shell_4_1") {
		#else
		if (device.getInfo<CL_DEVICE_NAME>() == "xilinx_u250_gen3x16_xdma_4_1_202210_1") {
		#endif
			break;
		}
	}

	OCL_CHECK(err, cl::Context context(device, NULL, NULL, NULL, &err));
	OCL_CHECK(err, cl::CommandQueue q(context, device, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE|CL_QUEUE_PROFILING_ENABLE, &err));
	OCL_CHECK(err, std::string device_name = device.getInfo<CL_DEVICE_NAME>(&err));

	//Create Program
	auto fileBuf = xcl::read_binary_file(binary);
	cl::Program::Binaries bins{{fileBuf.data(), fileBuf.size()}};

	OCL_CHECK(err, cl::Program program(context, {device}, bins, NULL, &err));
	OCL_CHECK(err, cl::Kernel crcRegular(program, "crcCompute", &err));

	double exec_time = 0.0;

	std::chrono::steady_clock::time_point t1;
	std::chrono::steady_clock::time_point t2;
	std::chrono::duration<double> duration;

	cl::vector<cl::Event>* vectorEvents[3];
	for(unsigned j = 0; j < 3; j++){
		vectorEvents[j] = new std::vector<cl::Event>[entryCount];
	}

	for(int j = 0; j < entryCount; j++){
		memcpy(in[j], fileData[j], fileSize[j]);
	}

	std::cout << "Starting CRC32 send" << "\n";
	std::chrono::steady_clock::time_point totalTimeExec = std::chrono::steady_clock::now();
	for(int j = 0; j < entryCount; j++){
		//std::cout << "ENTRY NUMBER: " << j << "\n";
		//Allocate Buffer in Global Memory
		int err;
		inOCL[j%modVal] = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY, fileSize[j], in[j], &err);
		if(err != CL_SUCCESS){
			std::cerr << "Could not allocate buffer, error number: " << err << "\n";
			return EXIT_FAILURE;
		}
		outputCRCOCL[j%modVal] = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, 64, outputCRC[j], &err);
		if(err != CL_SUCCESS){
			std::cerr << "Could not allocate buffer, error number: " << err << "\n";
			return EXIT_FAILURE;
		}

		//Set arguments
		int nargs = 0;
		err = crcRegular.setArg(nargs++, inOCL[j%modVal]);
		if(err != CL_SUCCESS){
			std::cerr << "Could not set arg: " << err << "\n";
			return EXIT_FAILURE;
		}
		err = crcRegular.setArg(nargs++, fileSize[j%modVal]);
		if(err != CL_SUCCESS){
			std::cerr << "Could not set arg: " << err << "\n";
			return EXIT_FAILURE;
		}
		err = crcRegular.setArg(nargs++, outputCRCOCL[j%modVal]);
		if(err != CL_SUCCESS){
			std::cerr << "Could not set arg: " << err << "\n";
			return EXIT_FAILURE;
		}

		//Wait for last enqueued event if no more space
                if(j >= modVal-1){
                        vectorEvents[2][j-(modVal-1)][0].wait();
                }

		//Copy input data to device global memory
		//Register the event for migratememobj
		vectorEvents[0][j].push_back(cl::Event());
		if(j == 0){
			OCL_CHECK(err, err = q.enqueueMigrateMemObjects({inOCL[j%modVal]}, 0, nullptr, &vectorEvents[0][j][0]));
		}else{
			OCL_CHECK(err, err = q.enqueueMigrateMemObjects({inOCL[j%modVal]}, 0, &vectorEvents[2][j-1], &vectorEvents[0][j][0]));
		}

		//Launch the kernel and use event we just pushed into the vector
                //Register the event for enqueueTask
		vectorEvents[1][j].push_back(cl::Event());
		OCL_CHECK(err, err = q.enqueueTask(crcRegular, &vectorEvents[0][j], &vectorEvents[1][j][0]));

		//Copy output data back to host local memory
		//Register the event for migratememobj
		vectorEvents[2][j].push_back(cl::Event());
		OCL_CHECK(err, err = q.enqueueMigrateMemObjects({outputCRCOCL[j%modVal]}, CL_MIGRATE_MEM_OBJECT_HOST, &vectorEvents[1][j], &vectorEvents[2][j][0]));

	}
	//std::cout << "WAITING FOR" << "\n";
	//for(unsigned int i = entryCount - modVal; i < entryCount; i++){
		vectorEvents[2][entryCount-1][0].wait();
	//}

	std::chrono::steady_clock::time_point totalTimeExecEnd = std::chrono::steady_clock::now();
        duration = totalTimeExecEnd - totalTimeExec;
        exec_time += std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();

	std::cout << "COMPARE ANSWER" << "\n";
        for(int j = 0; j < entryCount; j++){
                //std::cout << *outputCRC[j] << "\n";
                if(crcAnswers[j] != *(outputCRC[j])){
                        std::cout << "Wrong values(CPU,FPGA) at: " << j << " " << crcAnswers[j] << " " << *(outputCRC[j]) << "\n";
                }
        }

        for(unsigned int j = 0; j < 3; j++){
                delete[] vectorEvents[j];
        }


        std::cout << "Task execution stopped here" << std::endl;
        printf( "|-------------------------+-------------------------|\n"
                "| Kernel                  |    Wall-Clock Time      |\n"
                "|-------------------------+-------------------------|\n");


        //Calculate compression throughput & compression ratio
        std::cout << "End to end Regular execution time is " << exec_time/pow(1000,3) << " s\n";

        for(int i = 0; i < entryCount; i++){
                free(in[i]);
                free(outputCRC[i]);
                delete[] fileData[i];
        }

        return EXIT_SUCCESS;
}


int main(int argc, char* argv[]){
	if(argc != 3){
		std::cout << "Usage: " << argv[0] << " <XCLBIN File> <input path>" << std::endl;
		return EXIT_FAILURE;
	}

	int ret;

	ret = crc_test(argv[1], argv[2]);
	if(ret != 0){
		std::cout << "Test failed !!!" << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
