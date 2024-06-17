/**
* Copyright (C) 2019-2021 Xilinx, Inc
*
* Licensed under the Apache License, Version 2.0 (the "License"). You may
* not use this file except in compliance with the License. A copy of the
* License is located at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
* WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
* License for the specific language governing permissions and limitations
* under the License.
*/

#define OCL_CHECK(error, call)                                                                   \
    call;                                                                                        \
    if (error != CL_SUCCESS) {                                                                   \
        printf("%s:%d Error calling " #call ", error code is: %d\n", __FILE__, __LINE__, error); \
        exit(EXIT_FAILURE);                                                                      \
    }

#include <thread>
#include "host_blowfish.h"
#include "utils.h" // for inputSizes
#include <fstream>
#include <iostream>
#include <stdlib.h>
#include <chrono>

#include <stdio.h>
#include <math.h>

#define NUM_GLOBAL_WITEMS 1024

#define NUM_LOOPS 1024 // number of times to run the test per input_size
#define NUM_INPUTSIZES 18 // number of input sizes to test
#define MAX_INPUTSIZE 8388608 // maximum input size to test
#define OUTPUT_FILE_PATH "results/timing_results.txt" // output size in bytes
#define VERBOSE   0 // print the input and output

static const int DATA_SIZE = 4096;

static const std::string error_message =
    "Error: Result mismatch:\n"
    "i = %d CPU result = %d Device result = %d\n";

int main(int argc, char* argv[]) {
    // TARGET_DEVICE macro needs to be passed from gcc command line
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <xclbin>" << std::endl;
        return EXIT_FAILURE;
    }

    std::string xclbinFilename = argv[1];

    // Compute the size of array in bytes
    size_t size_in_bytes = DATA_SIZE * sizeof(int);

    // Creates a vector of DATA_SIZE elements with an initial value of 10 and 32
    // using customized allocator for getting buffer alignment to 4k boundary

    std::vector<cl::Device> devices;
    cl_int err;
    cl::Context context;
    cl::CommandQueue q;
    cl::Kernel krnl_blowfish; //kernel_blowfish
    cl::Program program;
    std::vector<cl::Platform> platforms;
    bool found_device = false;

    // traversing all Platforms To find Xilinx Platform and targeted
    // Device in Xilinx Platform
    cl::Platform::get(&platforms);
    for (size_t i = 0; (i < platforms.size()) & (found_device == false); i++) {
        cl::Platform platform = platforms[i];
        std::string platformName = platform.getInfo<CL_PLATFORM_NAME>();
        if (platformName == "Xilinx") {
            devices.clear();
            platform.getDevices(CL_DEVICE_TYPE_ACCELERATOR, &devices);
            if (devices.size()) {
                found_device = true;
                break;
            }
        }
    }
    if (found_device == false) {
        std::cout << "Error: Unable to find Target Device " << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "INFO: Reading " << xclbinFilename << std::endl;
    FILE* fp;
    if ((fp = fopen(xclbinFilename.c_str(), "r")) == nullptr) {
        printf("ERROR: %s xclbin not available please build\n", xclbinFilename.c_str());
        exit(EXIT_FAILURE);
    }
    // Load xclbin
    std::cout << "Loading: '" << xclbinFilename << "'\n";
    std::ifstream bin_file(xclbinFilename, std::ifstream::binary);
    bin_file.seekg(0, bin_file.end);
    unsigned nb = bin_file.tellg();
    bin_file.seekg(0, bin_file.beg);
    char* buf = new char[nb];
    bin_file.read(buf, nb);

    // Creating Program from Binary File
    cl::Program::Binaries bins;
    bins.push_back({buf, nb});
    bool valid_device = false;
    for (unsigned int i = 0; i < devices.size(); i++) {
        auto device = devices[i];
        std::string deviceName;
        #ifndef HW_SIM
            deviceName = "xilinx_u250_gen3x16_xdma_shell_4_1";
        #else
            deviceName = "xilinx_u250_gen3x16_xdma_4_1_202210_1";
        #endif
        if (device.getInfo<CL_DEVICE_NAME>() == deviceName){
            // Creating Context and Command Queue for selected Device
            OCL_CHECK(err, context = cl::Context(device, nullptr, nullptr, nullptr, &err));
            // OCL_CHECK(err, q = cl::CommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE, &err));
            OCL_CHECK(err, q = cl::CommandQueue(context, device, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | CL_QUEUE_PROFILING_ENABLE, &err));
            std::cout << "Trying to program device[" << i << "]: " << device.getInfo<CL_DEVICE_NAME>() << std::endl;
            cl::Program program(context, {device}, bins, nullptr, &err);
            if (err != CL_SUCCESS) {
                std::cout << "Failed to program device[" << i << "] with xclbin file!\n";
            } else {
                std::cout << "Device[" << i << "]: program successful!\n";
                // OCL_CHECK(err, krnl_vector_add = cl::Kernel(program, "krnl_vadd", &err));
                OCL_CHECK(err, krnl_blowfish = cl::Kernel(program, "krnl_blowfish", &err));
                // OCL_CHECK(err, krnl_vector_minus = cl::Kernel(program, "krnl_vminus", &err));
                valid_device = true;
                break; // we break because we found a valid device
            }
        }
    }

    // Verify the result
    int match = 0;

    if (!valid_device) {
        std::cout << "Failed to program any device found, exit!\n";
        exit(EXIT_FAILURE);
    }

    
    std::cout << "*****************************************" << std::endl;
    std::cout << "Starting Blowfish Encryption (Out-Of-Order No Host Memory)" << std::endl;
    std::cout << "*****************************************" << std::endl;

    std::chrono::microseconds::rep end_to_end_time[NUM_INPUTSIZES] = {0}; // total time

    for (uint32_t i = 0; i < NUM_INPUTSIZES; i++){

        // get the current inputSize
        size_t inputSize = inputSizeOptions[i];
        
        const int modVal = 512;
        // 1024 iterations of file input
        std::vector<uint8_t*> ptr_plainText;
        std::vector<uint8_t*> ptr_cipherText;
        std::vector<cl::Buffer> buffer_plainText(modVal);
        std::vector<cl::Buffer> buffer_cipherText(modVal);
        
        for(uint32_t i = 0; i < NUM_LOOPS; i++){

            /* Read plaintext from file */
            FILE *plaintextFile = fopen("inputs/plaintext.txt", "rb");
            if (plaintextFile == NULL) {
                perror("Error opening plaintext file");
                return 1;
            }
            fseek(plaintextFile, 0, SEEK_END);
            uint32_t Osize = ftell(plaintextFile);
            if (Osize < inputSize) {
                printf("Error: Input size is larger than the plaintext file size\n");
                return 1;
            }

            uint8_t* tmpPtr;
            posix_memalign((void**)&tmpPtr, 4096, inputSize);
            fseek(plaintextFile, 0, SEEK_SET);
            fread(tmpPtr, sizeof(uint8_t), inputSize, plaintextFile);
            fclose(plaintextFile);
            // added each of the pointers to the vector (in total there's 1024 pointers in the vector)
            ptr_plainText.push_back(tmpPtr);

            // allocate memory for the cipherText output
            uint8_t* tmpPtr2;
            posix_memalign((void**)&tmpPtr2, 4096, inputSize);
            ptr_cipherText.push_back(tmpPtr2);

        }

        double exec_time = 0.0;

        std::chrono::steady_clock::time_point t1;
        std::chrono::steady_clock::time_point t2;
        std::chrono::duration<double> duration;

        cl::vector<cl::Event>* vectorEvents[3];
        for(unsigned j = 0; j < 3; j++){
            vectorEvents[j] = new std::vector<cl::Event>[NUM_LOOPS];
        }

        // uint8_t* ptr_plainText;
        // posix_memalign((void**)&ptr_plainText, 4096, inputSize);

        // uint8_t* ptr_cipherText;
        // posix_memalign((void**)&ptr_cipherText, 4096, inputSize);

        // size_t* ptr_inputLength;
        // posix_memalign((void**)&ptr_inputLength, 4096, sizeof(size_t));
        // *ptr_inputLength = inputSize;

        int inputLength = inputSize;

        // int iter = 0;
        // int curr_loop = 0;
        // uint32_t Psize = ceil(inputSize / 8.0) * 8;
        // // printf("Iteraion: [%d]\n", curr_loop);
        // // printf("Encrypted Data: ");
        // while (iter < Psize) {
        //     printf("%.2X%.2X%.2X%.2X ", ptr_cipherText[curr_loop][iter], ptr_cipherText[curr_loop][iter + 1],
        //             ptr_cipherText[curr_loop][iter + 2], ptr_cipherText[curr_loop][iter + 3]);
        //     printf("%.2X%.2X%.2X%.2X ", ptr_cipherText[curr_loop][iter + 4], ptr_cipherText[curr_loop][iter + 5],
        //             ptr_cipherText[curr_loop][iter + 6], ptr_cipherText[curr_loop][iter + 7]);
        //     iter += 8;
        // }
        // printf("\n");

        // Get the end_to_end_start starting time point for the current inputSize
	    std::chrono::steady_clock::time_point totalTimeExec = std::chrono::steady_clock::now();

        // run the measurement 1024 times and report the total for each input after the initial programming
        for (uint32_t curr_loop = 0; curr_loop < NUM_LOOPS; curr_loop++) {

            if (VERBOSE)
                printf("Running test for input size: [%s]\n", inputSizeStrings[i].c_str());

            size_in_bytes = inputSize;

            if(VERBOSE){
                printf("Before Allocating memory for plainText and cipherText\n");
            }


            // These commands will allocate memory on the Device. The cl::Buffer objects can
            // be used to reference the memory locations on the device.
            int err;
            buffer_plainText[curr_loop%modVal] = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY, size_in_bytes, ptr_plainText[curr_loop], &err);
            // buffer_plainText.push_back(cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY, size_in_bytes, ptr_plainText[curr_loop], &err));
            if(err != CL_SUCCESS){
			    std::cerr << "Could not allocate buffer, error number: " << err << "\n";
			    return EXIT_FAILURE;
		    }
            if(VERBOSE){
                printf("Allocated buffer for plainText\n");
            }

            uint8_t* tmpPtr;
            posix_memalign((void**)&tmpPtr, 4096, inputSize);
            buffer_cipherText[curr_loop%modVal] = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, size_in_bytes, ptr_cipherText[curr_loop], &err);
            // buffer_cipherText.push_back(cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, size_in_bytes, ptr_cipherText[curr_loop], &err));
            if(err != CL_SUCCESS){
                std::cerr << "Could not allocate buffer, error number: " << err << "\n";
                return EXIT_FAILURE;
		    }
            if(VERBOSE){
                printf("Allocated buffer for cipherText\n");
            }


            int narg = 0;
            err = krnl_blowfish.setArg(narg++, buffer_plainText[curr_loop%modVal]);
            // err = krnl_blowfish.setArg(narg++, buffer_plainText[curr_loop]);
            if(err != CL_SUCCESS){
                std::cerr << "Could not set arg: " << err << "\n";
                return EXIT_FAILURE;
            }
            err = krnl_blowfish.setArg(narg++, inputLength);
            if(err != CL_SUCCESS){
                std::cerr << "Could not set arg: " << err << "\n";
                return EXIT_FAILURE;
            }
            err = krnl_blowfish.setArg(narg++, buffer_cipherText[curr_loop%modVal]);
            // err = krnl_blowfish.setArg(narg++, buffer_cipherText[curr_loop]);
            if(err != CL_SUCCESS){
                std::cerr << "Could not set arg: " << err << "\n";
                return EXIT_FAILURE;
            }


            // Wait for last enqueued event if no more space
            if(curr_loop >= modVal-1){
                vectorEvents[2][curr_loop-(modVal-1)][0].wait();
            }


            // verify the input
            if(VERBOSE && curr_loop == NUM_LOOPS - 1){
                printf("inputSize: %d\n", inputSize);
                for (int i = 0; i < inputSize; i++) {
                    std::cout<<ptr_plainText[0][i];
                }
                printf("\n");
            }

            // if(VERBOSE && curr_loop == NUM_LOOPS - 1){
            //     int iter = 0;
            //     uint32_t Psize = ceil(inputSize / 8.0) * 8;
            //     printf("tmpPtr: ");
            //     while (iter < Psize) {
            //         printf("%.2X%.2X%.2X%.2X ", tmpPtr[iter], tmpPtr[iter + 1],
            //                 tmpPtr[iter + 2], tmpPtr[iter + 3]);
            //         printf("%.2X%.2X%.2X%.2X ", tmpPtr[iter + 4], tmpPtr[iter + 5],
            //                 tmpPtr[iter + 6], tmpPtr[iter + 7]);
            //         iter += 8;
            //     }
            //     printf("\n");
            // }


            //Copy input data to device global memory
            //Register the event for migratememobj
            vectorEvents[0][curr_loop].push_back(cl::Event());
            if(curr_loop == 0){
                OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_plainText[curr_loop%modVal]}, 0, nullptr, &vectorEvents[0][curr_loop][0]));
                // OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_plainText[curr_loop]}, 0, nullptr, &vectorEvents[0][curr_loop][0]));
                //OCL_CHECK(err, q.finish());
            }else{
                OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_plainText[curr_loop%modVal]}, 0, &vectorEvents[2][curr_loop-1], &vectorEvents[0][curr_loop][0]));
                // OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_plainText[curr_loop]}, 0, &vectorEvents[2][curr_loop-1], &vectorEvents[0][curr_loop][0]));
                //OCL_CHECK(err, q.finish());
            }

            if(VERBOSE){
                printf("Migrated plainText to device\n");
            }


            //Launch the kernel and use event we just pushed into the vector
            //Register the event for enqueueTask
		    vectorEvents[1][curr_loop].push_back(cl::Event());
		    OCL_CHECK(err, err = q.enqueueTask(krnl_blowfish, &vectorEvents[0][curr_loop], &vectorEvents[1][curr_loop][0]));
		    // OCL_CHECK(err, err = q.enqueueTask(krnl_blowfish));
            //OCL_CHECK(err, q.finish());

            if(VERBOSE){
                printf("Kernel launched\n");
            }

            //Copy output data back to host local memory
            //Register the event for migratememobj
            vectorEvents[2][curr_loop].push_back(cl::Event());
            OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_cipherText[curr_loop%modVal]}, CL_MIGRATE_MEM_OBJECT_HOST, &vectorEvents[1][curr_loop], &vectorEvents[2][curr_loop][0]));
            // OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_cipherText[curr_loop]}, CL_MIGRATE_MEM_OBJECT_HOST, &vectorEvents[1][curr_loop], &vectorEvents[2][curr_loop][0]));
            //OCL_CHECK(err, q.finish());
            // OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_cipherText[curr_loop%modVal]}, CL_MIGRATE_MEM_OBJECT_HOST);
            
            if(VERBOSE){
                printf("Migrated cipherText to host\n");
            }


            // verify the output
            if(VERBOSE){
                int iter = 0;
                uint32_t Psize = ceil(inputSize / 8.0) * 8;
                printf("Iteraion: [%d]\n", curr_loop);
                printf("Encrypted Data: ");
                while (iter < Psize) {
                    printf("%.2X%.2X%.2X%.2X ", ptr_cipherText[curr_loop][iter], ptr_cipherText[curr_loop][iter + 1],
                            ptr_cipherText[curr_loop][iter + 2], ptr_cipherText[curr_loop][iter + 3]);
                    printf("%.2X%.2X%.2X%.2X ", ptr_cipherText[curr_loop][iter + 4], ptr_cipherText[curr_loop][iter + 5],
                            ptr_cipherText[curr_loop][iter + 6], ptr_cipherText[curr_loop][iter + 7]);
                    iter += 8;
                }
                printf("\n");
                //std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            // if(VERBOSE && curr_loop == NUM_LOOPS - 1){
            //     int iter = 0;
            //     uint32_t Psize = ceil(inputSize / 8.0) * 8;
            //     printf("Iteraion: [%d]\n", curr_loop);
            //     printf("Encrypted Data: ");
            //     while (iter < Psize) {
            //         printf("%.2X%.2X%.2X%.2X ", tmpPtr[iter], tmpPtr[iter + 1],
            //                 tmpPtr[iter + 2], tmpPtr[iter + 3]);
            //         printf("%.2X%.2X%.2X%.2X ", tmpPtr[iter + 4], tmpPtr[iter + 5],
            //                 tmpPtr[iter + 6], tmpPtr[iter + 7]);
            //         iter += 8;
            //     }
            //     printf("\n");
            // }


        } // end of loop

        // for(unsigned int i = NUM_LOOPS - modVal; i < NUM_LOOPS; i++){
            vectorEvents[2][NUM_LOOPS-1][0].wait();
        // }

        std::chrono::steady_clock::time_point totalTimeExecEnd = std::chrono::steady_clock::now();
        duration = totalTimeExecEnd - totalTimeExec;
        exec_time += std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
        end_to_end_time[i] = exec_time;

        // TIMTING TEST RESULT
        std::cout << "\nTIMING TEST RESULT: [" << inputSizeStrings[i]<< "]"<<std::endl;
        std::cout << "=====================================================================" << std::endl;
        printf("End to end time: %.2f microseconds\n", exec_time);
        std::cout << "=====================================================================" << std::endl;

        for(int i = 0; i < NUM_LOOPS; i++){
            free(ptr_plainText[i]);
            free(ptr_cipherText[i]);
        }

        for(unsigned int j = 0; j < 3; j++){
            delete[] vectorEvents[j];
        }

    }

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
        fprintf(outputFile, "%s\t%ld\n",
            inputSizeStrings[i].c_str(),
            end_to_end_time[i]);
    }

    // Close the file
    fclose(outputFile);
            
    return (match ? EXIT_FAILURE : EXIT_SUCCESS);  

}
