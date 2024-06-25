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
#include <mutex>
#include <atomic>
#include <sys/mman.h>

#include "helpers.h"

#include <fstream>
#include <iostream>

#define NUM_LOOPS 1024 // number of times to run the test per input_size
#define NUM_INPUTSIZES 18 // number of input sizes to test
#define INPUT_FILE_PATH "../inputs/plaintext.txt" // input file path
#define OUTPUT_FILE_PATH "../results/timing_results.txt" // output size in bytes

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


int crc_test(int argc, char* argv[]){

	std::cout << "Arguments of program: ";
	for(int i = 0; i < argc; i++){
		std::cout << argv[i] << " ";
	}

	// std::vector<unsigned int> fileSizes;
	// std::vector<char*> fileData;
	// std::vector<unsigned int> crcAnswers;
	// std::vector<unsigned int> crcFPGAAnswers;

	std::cout << "\n";
	std::cout << "*****************************************" << std::endl;
    std::cout << "Starting Image Histogram Equalization (HMLib)" << std::endl;
    std::cout << "*****************************************" << std::endl;

	std::string filePaths = std::string(argv[1]);
	bool enableCheck = std::stoi(argv[3]);

	// store the end-to-end time for each input size
	double end_to_end_time[NUM_INPUTSIZES] = {0.0};
	
	for (uint32_t curr_inputsize_index = 0; curr_inputsize_index < NUM_INPUTSIZES; curr_inputsize_index++){

		std::vector<unsigned int> fileSizes;
		std::vector<char*> fileData;
		std::vector<unsigned int> crcAnswers;
		std::vector<unsigned int> crcFPGAAnswers;

		//TODO: ADD YOUR INPUT FILES AND GOLDEN ANSWERS HERE
		// int entryCount = 0;
		// uint64_t totalInputSize = 0;
		// std::vector<std::filesystem::path> entryPath;
		// for (const auto & entry : std::filesystem::recursive_directory_iterator(filePaths)){
		// 	if(entry.is_regular_file()){
		// 		entryCount++;
		// 		entryPath.push_back(entry.path());
		// 		std::ifstream inFile(entry.path().string().c_str(), std::ifstream::binary);
		// 		if(!inFile){
		// 			std::cout << "Unable to open input file" << std::endl;
		// 			exit(EXIT_FAILURE);
		// 		}

		// 		if(stevez_debug){
		// 			std::cout << "\n";
		// 			std::cout << "Reading file: " << entry.path().string() << "\n";
		// 		}

		// 		int inputSize;

		// 		inFile.seekg(0, inFile.end);
		// 		inputSize = inFile.tellg();
		// 		inFile.seekg(0, inFile.beg);

		// 		char* tmpPtr = new char[inputSize];

		// 		inFile.read(tmpPtr, inputSize);
		// 		fileData.push_back(tmpPtr);
		// 		fileSizes.push_back(inputSize);
		// 		inFile.close();

		// 		totalInputSize += inputSize;
		// 	}
		// }

		// get the current inputSize
        size_t inputSize = inputSizeOptions[curr_inputsize_index];
		size_t outputSize = inputSize;

		int entryCount = 0;

		for (uint32_t curr_loop = 0; curr_loop < NUM_LOOPS; curr_loop++){
			// if(entry.is_regular_file()){
			entryCount++;
			// 	entryPath.push_back(entry.path());
			// 	std::ifstream inFile(entry.path().string().c_str(), std::ifstream::binary);
			// 	if(!inFile){
			// 		std::cout << "Unable to open input file" << std::endl;
			// 		exit(EXIT_FAILURE);
			// 	}

			// 	if(stevez_debug){
			// 		std::cout << "\n";
			// 		std::cout << "Reading file: " << entry.path().string() << "\n";
			// 	}

			/* Read plaintext from file */
            FILE *inFile = fopen(INPUT_FILE_PATH, "rb");

			if (inFile == NULL) {
                perror("Error opening plaintext file");
                return 1;
            }
			fseek(inFile, 0, SEEK_END);
            uint32_t Osize = ftell(inFile);
            if (Osize < inputSize) {
                printf("Error: Input size is larger than the plaintext file size\n");
                return 1;
            }

			// int inputSize;

			// inFile.seekg(0, inFile.end);
			// inputSize = inFile.tellg();
			// inFile.seekg(0, inFile.beg);

			char* tmpPtr = new char[inputSize];

			fseek(inFile, 0, SEEK_SET);
            fread(tmpPtr, sizeof(uint8_t), inputSize, inFile);
            fclose(inFile);

			// inFile.read(tmpPtr, inputSize);
			fileData.push_back(tmpPtr);
			fileSizes.push_back(inputSize);
			// inFile.close();

			// totalInputSize += inputSize;
			// }
		}


		if(enableCheck){
			for(unsigned int i = 0; i < fileData.size(); i++){
				unsigned int val = crc32(fileData[i], fileSizes[i]);
				crcAnswers.push_back(val);
			}
		}

		std::thread workers[HMLIB_HANDLERS][2];
		bool pass[HMLIB_HANDLERS][2];
		struct HMLibUniqueHandler* HMLibUH[HMLIB_HANDLERS];
		HMLib HMLibObject;
		HMLibObject.initialize(std::string(argv[2]),"histogram_HM",8,inputSize,256);

		for(unsigned int i = 0; i < HMLIB_HANDLERS; i++){
			HMLibUH[i] = HMLibObject.getHMLibUniqueHandler(i);
			if(HMLibUH[i] == nullptr){
				exit(EXIT_FAILURE);;
			}
		}

		for(unsigned int i = 0; i < HMLIB_HANDLERS; i++){
			workers[i][0] = std::thread(parallelTaskSend, std::ref(HMLibObject), std::ref(HMLibUH[i]), std::ref(fileData), std::ref(fileSizes), std::ref(pass[i][0]));
			workers[i][1] = std::thread(parallelTaskReceive, std::ref(HMLibObject), std::ref(HMLibUH[i]), std::ref(crcFPGAAnswers), entryCount, enableCheck, std::ref(pass[i][1]));
		}

		for(unsigned int i = 0; i < HMLIB_HANDLERS; i++){
			for(unsigned int j = 0; j < 2; j++){
				workers[i][j].join();
			}
		}

		for(unsigned int i = 0; i < HMLIB_HANDLERS; i++){
			for(unsigned int j = 0; j < 2; j++){
				if(!pass[i][j]){
					exit(EXIT_FAILURE);
				}
			}
		}

		//TODO: WRITE YOUR GOLDEN ANSWER COMPARE HERE
		if(enableCheck){
			for(int i = 0; i < crcAnswers.size(); i++){
				if(crcAnswers[i] != crcFPGAAnswers[i]){
					if(i+1 < crcAnswers.size() && i-1 >= 0){
						std::cout << "Mnus1 values(CPU,FPGA) at: " << i-1 << " " << crcAnswers[i-1] << " " << crcFPGAAnswers[i-1] << "\n";
						std::cout << "Wrong values(CPU,FPGA) at: " << i << " " << crcAnswers[i] << " " << crcFPGAAnswers[i] << "\n";
						std::cout << "Plus1 values(CPU,FPGA) at: " << i+1 << " " << crcAnswers[i+1] << " " << crcFPGAAnswers[i+1] << "\n";
					}else{
						std::cout << "Wrong values(CPU,FPGA) at: " << i << " " << crcAnswers[i] << " " << crcFPGAAnswers[i] << "\n";
					}
					exit(EXIT_FAILURE);
				}
			}
		}

		for(unsigned int i = 0; i < HMLIB_HANDLERS; i++){
			if(!HMLibObject.returnHMLibUniqueHandler(HMLibUH[i],i)){
				exit(EXIT_FAILURE);
			}
		}
		double overallTime = 0.0;

		std::cout << "\n";
		std::cout << "INPUT SIZE: " << inputSizeStrings[curr_inputsize_index] << "\n";
		std::cout << "-----------------------------------------------------------" << std::endl;
		HMLibObject.printStatistics(overallTime);
		std::cout << "-----------------------------------------------------------" << std::endl;
		std::cout << "\n";
		
		for(unsigned int i = 0; i < fileData.size(); i++){
			delete[] fileData[i];
		}

		std::cout << "*****************************************" << std::endl;
		printf("overallTime: %f\n", overallTime);
		std::cout << "*****************************************" << std::endl;

		end_to_end_time[curr_inputsize_index] = overallTime;

	}

	std::cout << "*****************************************" << std::endl;
	std::cout << "Finished Image Histogram Equalization (HMLib)" << std::endl;
	std::cout << "*****************************************" << std::endl;

	if(writeEnable){
		// Open a file for writing
		FILE *outputFile = fopen(OUTPUT_FILE_PATH, "w");
		if (outputFile == NULL) {
			printf("Failed to open the output file.\n");
			return 1;
		}

		fprintf(outputFile, "Data Size\tEnd to End Time (μs)\n");

		// Write results to the file
		for (int i = 0; i < NUM_INPUTSIZES; ++i) {    
			// Write absolute times
			fprintf(outputFile, "%s\t%f\n",
				inputSizeStrings[i].c_str(),
				end_to_end_time[i]);
		}

		// Close the file
		fclose(outputFile);
	}

	return EXIT_SUCCESS;
}


int main(int argc, char* argv[]){
	if(argc != 4){
		std::cout << "Usage: " << argv[0] << " <input path> <XCLBIN File> <enable check>" << std::endl;
		return EXIT_FAILURE;
	}

	int ret;

	ret = crc_test(argc, argv);
	if(ret != 0){
		std::cout << "Test failed !!!" << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
