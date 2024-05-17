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

#include "host_blowfish.h"
#include "utils.h" // for inputSizes
#include <fstream>
#include <iostream>
#include <stdlib.h>
#include <chrono>

#include <stdio.h>

#define NUM_GLOBAL_WITEMS 1024

#define NUM_LOOPS 1024 // number of times to run the test per input_size
#define NUM_INPUTSIZES 18 // number of input sizes to test
#define OUTPUT_FILE_PATH "results/timing_results.txt" // output size in bytes
#define VERBOSE   0

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
        if (device.getInfo<CL_DEVICE_NAME>() != deviceName){
            // Creating Context and Command Queue for selected Device
            OCL_CHECK(err, context = cl::Context(device, nullptr, nullptr, nullptr, &err));
            OCL_CHECK(err, q = cl::CommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE, &err));
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


    // create timer for each secitions to be timed
    std::chrono::microseconds::rep kernl_execution_time = 0; // kernel execution time
    std::chrono::microseconds::rep kernl_arg_time = 0;     // kernel argument setup time
    std::chrono::microseconds::rep alloc_opencl_buffer_time = 0; // buffer allocation time
    std::chrono::microseconds::rep buffer_to_fpga_time = 0; // buffer to fpga time
    std::chrono::microseconds::rep fpga_to_buffer_time = 0; // fpga to buffer time

    // Verify the result
    int match = 0;

    if (!valid_device) {
        std::cout << "Failed to program any device found, exit!\n";
        exit(EXIT_FAILURE);
    }


    for (uint32_t i = 0; i < 3; i++){

        size_t inputSize = inputSizeOptions[i];

        // run the measurement 1024 times and report the total for each input after the initial programming
        for (uint32_t curr_loop = 0; curr_loop < NUM_LOOPS; curr_loop++) {

            // Get the alloc_opencl_buffer starting time point
            auto alloc_opencl_buffer_start = std::chrono::steady_clock::now();
            // These commands will allocate memory on the Device. The cl::Buffer objects can
            // be used to reference the memory locations on the device.
            OCL_CHECK(err, cl::Buffer buffer_plainText(context, CL_MEM_READ_ONLY, size_in_bytes, NULL, &err));
            OCL_CHECK(err, cl::Buffer buffer_inputLength(context, CL_MEM_READ_ONLY, size_in_bytes, NULL, &err));
            OCL_CHECK(err, cl::Buffer buffer_cipherText(context, CL_MEM_READ_ONLY, size_in_bytes, NULL, &err));
            // Get the kernl_arg ending time point
            auto alloc_opencl_buffer_end = std::chrono::steady_clock::now();
            auto alloc_opencl_buffer = std::chrono::duration_cast<std::chrono::microseconds>(alloc_opencl_buffer_end - alloc_opencl_buffer_start);
            alloc_opencl_buffer_time += alloc_opencl_buffer.count();

            // Get the kernl_arg starting time point
            auto kernl_arg_start = std::chrono::steady_clock::now();
            // set the kernel Arguments
            int narg = 0;
            OCL_CHECK(err, err = krnl_blowfish.setArg(narg++, buffer_plainText));
            OCL_CHECK(err, err = krnl_blowfish.setArg(narg++, buffer_inputLength));
            OCL_CHECK(err, err = krnl_blowfish.setArg(narg++, buffer_cipherText));
            // Get the kernl_arg ending time point
            auto kernl_arg_end = std::chrono::steady_clock::now();
            auto kernl_arg = std::chrono::duration_cast<std::chrono::microseconds>(kernl_arg_end - kernl_arg_start);
            kernl_arg_time += kernl_arg.count();

            // We then need to map our OpenCL buffers to get the pointers
            uint8_t* ptr_plainText;
            int* ptr_inputLength;
            uint8_t* ptr_cipherText;

            OCL_CHECK(err,
                    ptr_plainText = (uint8_t*)q.enqueueMapBuffer(buffer_plainText, CL_TRUE, CL_MAP_WRITE, 0, size_in_bytes, NULL, NULL, &err));
            OCL_CHECK(err,
                    ptr_inputLength = (int*)q.enqueueMapBuffer(buffer_inputLength, CL_TRUE, CL_MAP_WRITE, 0, size_in_bytes, NULL, NULL, &err));       
            OCL_CHECK(err, 
                    ptr_cipherText = (uint8_t*)q.enqueueMapBuffer(buffer_cipherText, CL_TRUE, CL_MAP_READ, 0, size_in_bytes, NULL, NULL, &err));

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
            fseek(plaintextFile, 0, SEEK_SET);
            fread(ptr_plainText, sizeof(uint8_t), inputSize, plaintextFile);
            fclose(plaintextFile);

            // set input length
            *ptr_inputLength = inputSize;

            // Get the buffer_to_fpga starting time point
            auto buffer_to_fpga_start = std::chrono::steady_clock::now();
            // Data will be migrated to kernel space
            OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_plainText, buffer_inputLength}, 0 /* 0 means from host*/));
            // Get the buffer_to_fpga ending time point
            auto buffer_to_fpga_end = std::chrono::steady_clock::now();
            auto buffer_to_fpga = std::chrono::duration_cast<std::chrono::microseconds>(buffer_to_fpga_end - buffer_to_fpga_start);
            buffer_to_fpga_time += buffer_to_fpga.count();

            // Get the kernl_execution starting time point
            auto kernl_execution_start = std::chrono::steady_clock::now();
            // Launch the Kernel 
            OCL_CHECK(err, err = q.enqueueTask(krnl_blowfish));
            // Get the kernl_execution ending time point
            auto kernl_execution_end = std::chrono::steady_clock::now();
            auto kernl_execution = std::chrono::duration_cast<std::chrono::microseconds>(kernl_execution_end - kernl_execution_start);
            kernl_execution_time += kernl_execution.count();

            // Get the fpga_to_buffer starting time point
            auto fpga_to_buffer_start = std::chrono::steady_clock::now();
            // The result of the previous kernel execution will need to be retrieved in
            // order to view the results. This call will transfer the data from FPGA to
            // source_results vector
            OCL_CHECK(err, q.enqueueMigrateMemObjects({buffer_cipherText}, CL_MIGRATE_MEM_OBJECT_HOST));
            // Get the fpga_to_buffer ending time point
            auto fpga_to_buffer_end = std::chrono::steady_clock::now();
            auto fpga_to_buffer = std::chrono::duration_cast<std::chrono::microseconds>(fpga_to_buffer_end - fpga_to_buffer_start);
            fpga_to_buffer_time += fpga_to_buffer.count();

            OCL_CHECK(err, q.finish());

            // if ((unsigned int)*ptr_outL == 0xDF333FD2L && (unsigned int)*ptr_outR == 0x30A71BB4L) {
            //     if (VERBOSE)
            //         printf("Test encryption success!!\n");
            //     match = 0;
            // }

            OCL_CHECK(err, err = q.enqueueUnmapMemObject(buffer_plainText, ptr_plainText));
            OCL_CHECK(err, err = q.enqueueUnmapMemObject(buffer_inputLength, ptr_inputLength));
            OCL_CHECK(err, err = q.enqueueUnmapMemObject(buffer_cipherText, ptr_cipherText));
            OCL_CHECK(err, err = q.finish());

            // if(VERBOSE)
            //     std::cout << "TEST " << (match ? "FAILED" : "PASSED") << std::endl;   

            } // end of loop

        // TIMTING TEST RESULT
        std::cout << "\nTIMING TEST RESULT: [" << inputSizeStrings[i]<< "]"<<std::endl;
        std::cout << "=====================================================================" << std::endl;
        std::cout << "Alloc Opencl Buffer Time: " << alloc_opencl_buffer_time << " microseconds" << std::endl;
        std::cout << "=====================================================================" << std::endl;
        std::cout << "Kernel Argument Setting Time: " << kernl_arg_time << " microseconds" << std::endl;
        std::cout << "=====================================================================" << std::endl;
        std::cout << "Buffer to FPGA Time: " << buffer_to_fpga_time << " microseconds" << std::endl;
        std::cout << "=====================================================================" << std::endl;
        std::cout << "Kernel Execution Time: " << kernl_execution_time << " microseconds" << std::endl;
        std::cout << "=====================================================================" << std::endl;
        std::cout << "FPGA to Buffer Time: " << fpga_to_buffer_time << " microseconds" << std::endl;
        std::cout << "=====================================================================\n" << std::endl;


        // Open a file for writing
        FILE *outputFile = fopen(OUTPUT_FILE_PATH, "r+");
        if (outputFile == NULL) {
            printf("Failed to open the output file.\n");
            return 1;
        }

        if (is_file_empty(outputFile)) {
            printf("File is empty. Opening in write mode.\n");
            fclose(outputFile); // Close file before reopening
            outputFile = fopen(OUTPUT_FILE_PATH, "w"); // Open file in write mode
        } else {
            printf("File is not empty. Opening in append mode.\n");
            fclose(outputFile); // Close file before reopening
            outputFile = fopen(OUTPUT_FILE_PATH, "a"); // Open file in append mode
        }

        // Write results to the file
        fprintf(outputFile, "TIMING TEST RESULT [%s]:\n", inputSizeStrings[i].c_str());
        fprintf(outputFile, "=====================================================================\n");
        fprintf(outputFile, "Alloc Opencl Buffer Time: %ld microseconds\n", alloc_opencl_buffer_time);
        fprintf(outputFile, "=====================================================================\n");
        fprintf(outputFile, "Kernel Argument Setting Time: %ld microseconds\n", kernl_arg_time);
        fprintf(outputFile, "=====================================================================\n");
        fprintf(outputFile, "Buffer to FPGA Time: %ld microseconds\n", buffer_to_fpga_time);
        fprintf(outputFile, "=====================================================================\n");
        fprintf(outputFile, "Kernel Execution Time: %ld microseconds\n", kernl_execution_time);
        fprintf(outputFile, "=====================================================================\n");
        fprintf(outputFile, "FPGA to Buffer Time: %ld microseconds\n", fpga_to_buffer_time);
        fprintf(outputFile, "=====================================================================\n\n");

        // Close the file
        fclose(outputFile);

    }
            
    return (match ? EXIT_FAILURE : EXIT_SUCCESS);  

}
