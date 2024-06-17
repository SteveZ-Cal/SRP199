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

#include "host_histogram_equalization.h"
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
#define OUTPUT_FILE_PATH "results/timing_results.txt" // output size in bytes
#define VERBOSE   0 // print the input and output
#define TEST      0 // perform the test
#define DEBUG     0 // print the intermediate results
#define BYTES_PER_DOUBLE 8

#define BINS_NUM 256 // number of bins for kernel output of histogram equalization

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
    cl::Kernel krnl_histogram_equalization; //kernel_histogram_equalization
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
            OCL_CHECK(err, q = cl::CommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE, &err));
            std::cout << "Trying to program device[" << i << "]: " << device.getInfo<CL_DEVICE_NAME>() << std::endl;
            cl::Program program(context, {device}, bins, nullptr, &err);
            if (err != CL_SUCCESS) {
                std::cout << "Failed to program device[" << i << "] with xclbin file!\n";
            } else {
                std::cout << "Device[" << i << "]: program successful!\n";
                // OCL_CHECK(err, krnl_vector_add = cl::Kernel(program, "krnl_vadd", &err));
                OCL_CHECK(err, krnl_histogram_equalization = cl::Kernel(program, "krnl_histogram_equalization", &err));
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
    std::cout << "Starting Image Histogram Equalization (InMemOrder)" << std::endl;
    std::cout << "*****************************************" << std::endl;

    // create timer for each secitions to be timed
    std::chrono::microseconds::rep kernl_execution_time[NUM_INPUTSIZES] = {0}; // kernel execution time
    std::chrono::microseconds::rep kernl_arg_time[NUM_INPUTSIZES] = {0};     // kernel argument setup time
    std::chrono::microseconds::rep alloc_opencl_buffer_time[NUM_INPUTSIZES] = {0}; // buffer allocation time
    std::chrono::microseconds::rep buffer_to_fpga_time[NUM_INPUTSIZES] = {0}; // buffer to fpga time
    std::chrono::microseconds::rep fpga_to_buffer_time[NUM_INPUTSIZES] = {0}; // fpga to buffer time

    for (uint32_t i = 0; i < NUM_INPUTSIZES; i++){


        size_t inputSize = inputSizeOptions[i];

        uint8_t* ptr_plainText;
        posix_memalign((void**)&ptr_plainText, 4096, inputSize);

        double* ptr_cipherText;
        posix_memalign((void**)&ptr_cipherText, 4096, BINS_NUM * sizeof(double));
        // memset(ptr_plainText, 0.0, inputSize * sizeof(double));

        // size_t inputSize = inputSizeOptions[i] / BYTES_PER_DOUBLE;

        // double* ptr_plainText;
        // posix_memalign((void**)&ptr_plainText, 4096, inputSize * sizeof(double));
        // // Check if memory allocation was successful
        // if (ptr_plainText != NULL) {
        //     // Initialize the memory to all zeroes
        //     memset(ptr_plainText, 0.0, inputSize * sizeof(double));

        // }else{
        //     printf("Memory allocation failed\n");
        //     return 1;
        // } 

        // //always set the first of plainText to 1 for testing purpose
        // ptr_plainText[0] = 1.0;

        // double* ptr_cipherText;
        // posix_memalign((void**)&ptr_cipherText, 4096, inputSize * sizeof(double));

        // if (ptr_cipherText != NULL) {
        //     // Initialize the memory to all zeroes
        //     memset(ptr_cipherText, 0.0, inputSize * sizeof(double));
        // }else{
        //     printf("Memory allocation failed\n");
        //     return 1;
        // }

        // printf("****************Initial ptr_cipherText*********: %d\n", inputSize);
        // for(size_t i=0; i<inputSize; i++){
        //     printf("%.3f \t", ptr_cipherText[i]);
        // }
        // printf("\n");

        // printf("*************inputSize*********: [%d]\n", inputSize);
        // for (size_t i = 0; i < inputSize; ++i) {
        //     printf("%.3f \t", ptr_plainText[i]);
        // }


        int inputLength = inputSize;

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

        // run the measurement 1024 times and report the total for each input after the initial programming
        for (uint32_t curr_loop = 0; curr_loop < NUM_LOOPS; curr_loop++) {

            if (VERBOSE)
                printf("Running test for input size: %s\n", inputSizeStrings[i].c_str());

            size_in_bytes = inputSize;


            if (VERBOSE && curr_loop == NUM_LOOPS - 1){
                printf("InputSize: %d\n", inputSize);
            }


            if(VERBOSE && DEBUG){
                printf("Histogram Equlization Frequency: \n");
                for(size_t i=0; i<BINS_NUM; i++){
                    // printf("%.3f \t", ptr_cipherText[i]);
                    std::cout << ptr_cipherText[i] << "\t";
                }
                printf("\n");
            }


            // Get the alloc_opencl_buffer starting time point
            auto alloc_opencl_buffer_start = std::chrono::steady_clock::now();
            // These commands will allocate memory on the Device. The cl::Buffer objects can
            // be used to reference the memory locations on the device.
            OCL_CHECK(err, cl::Buffer buffer_plainText(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY, size_in_bytes, ptr_plainText, &err));
            // OCL_CHECK(err, cl::Buffer buffer_inputLength(context, CL_MEM_READ_ONLY, size_in_bytes, NULL, &err));
            // OCL_CHECK(err, cl::Buffer buffer_inputLength(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY, sizeof(size_t), ptr_inputLength, &err));
            OCL_CHECK(err, cl::Buffer buffer_cipherText(context, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, BINS_NUM*(sizeof(double)), ptr_cipherText, &err));
            // Get the kernl_arg ending time point
            auto alloc_opencl_buffer_end = std::chrono::steady_clock::now();
            auto alloc_opencl_buffer = std::chrono::duration_cast<std::chrono::microseconds>(alloc_opencl_buffer_end - alloc_opencl_buffer_start);
            alloc_opencl_buffer_time[i] += alloc_opencl_buffer.count();

            // Get the kernl_arg starting time point
            auto kernl_arg_start = std::chrono::steady_clock::now();
            // set the kernel Arguments
            int narg = 0;
            OCL_CHECK(err, err = krnl_histogram_equalization.setArg(narg++, buffer_plainText));
            // OCL_CHECK(err, err = krnl_histogram_equalization.setArg(narg++, buffer_inputLength));
            OCL_CHECK(err, err = krnl_histogram_equalization.setArg(narg++, inputLength));
            OCL_CHECK(err, err = krnl_histogram_equalization.setArg(narg++, buffer_cipherText));
            // Get the kernl_arg ending time point
            auto kernl_arg_end = std::chrono::steady_clock::now();
            auto kernl_arg = std::chrono::duration_cast<std::chrono::microseconds>(kernl_arg_end - kernl_arg_start);
            kernl_arg_time[i] += kernl_arg.count();

            // verify the input
            if(VERBOSE && curr_loop == NUM_LOOPS - 1){
                printf("Host Input: \n");
                for (int i = 0; i < inputSize; i++) {
                    std::cout<< ptr_plainText[i];
                }
                printf("\n");
            }

            // Get the buffer_to_fpga starting time point
            auto buffer_to_fpga_start = std::chrono::steady_clock::now();
            // Data will be migrated to kernel space
            // OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_plainText, buffer_inputLength}, 0 /* 0 means from host*/));
            OCL_CHECK(err, q.enqueueMigrateMemObjects({buffer_plainText}, 0 /* 0 means from host*/));
            OCL_CHECK(err, q.finish());
            // Get the buffer_to_fpga ending time point
            auto buffer_to_fpga_end = std::chrono::steady_clock::now();
            auto buffer_to_fpga = std::chrono::duration_cast<std::chrono::microseconds>(buffer_to_fpga_end - buffer_to_fpga_start);
            buffer_to_fpga_time[i] += buffer_to_fpga.count();

            // Get the kernl_execution starting time point
            auto kernl_execution_start = std::chrono::steady_clock::now();
            // Launch the Kernel 
            OCL_CHECK(err, err = q.enqueueTask(krnl_histogram_equalization));
            OCL_CHECK(err, q.finish());
            // Get the kernl_execution ending time point
            auto kernl_execution_end = std::chrono::steady_clock::now();
            auto kernl_execution = std::chrono::duration_cast<std::chrono::microseconds>(kernl_execution_end - kernl_execution_start);
            kernl_execution_time[i] += kernl_execution.count();

            // Get the fpga_to_buffer starting time point
            auto fpga_to_buffer_start = std::chrono::steady_clock::now();
            // The result of the previous kernel execution will need to be retrieved in
            // order to view the results. This call will transfer the data from FPGA to
            // source_results vector
            OCL_CHECK(err, q.enqueueMigrateMemObjects({buffer_cipherText}, CL_MIGRATE_MEM_OBJECT_HOST));
            OCL_CHECK(err, q.finish());

            // Get the fpga_to_buffer ending time point
            auto fpga_to_buffer_end = std::chrono::steady_clock::now();
            auto fpga_to_buffer = std::chrono::duration_cast<std::chrono::microseconds>(fpga_to_buffer_end - fpga_to_buffer_start);
            fpga_to_buffer_time[i] += fpga_to_buffer.count();

            // verify the output
            // if(VERBOSE && curr_loop == NUM_LOOPS - 1){
            //     int iter = 0;
            //     uint32_t Psize = ceil(inputSize / 8.0) * 8;
            //     printf("Iteraion: [%d]\n", curr_loop);
            //     while (iter < Psize) {
            //         printf("%.2X%.2X%.2X%.2X ", ptr_cipherText[iter], ptr_cipherText[iter + 1],
            //                 ptr_cipherText[iter + 2], ptr_cipherText[iter + 3]);
            //         printf("%.2X%.2X%.2X%.2X ", ptr_cipherText[iter + 4], ptr_cipherText[iter + 5],
            //                 ptr_cipherText[iter + 6], ptr_cipherText[iter + 7]);
            //         iter += 8;
            //     }
            //     printf("\n");
            // }

            if(TEST && curr_loop == NUM_LOOPS - 1){
                printf("Histogram Equlization Frequency: \n");
                for(size_t i=0; i<BINS_NUM; i++){
                    // printf("%.3f \t", ptr_cipherText[i]);
                    std::cout << ptr_cipherText[i] << "\t";
                }
                printf("\n");
            }

            // reset the output buffer
            memset(ptr_cipherText, 0.0, BINS_NUM * sizeof(double));

        } // end of loop

        // TIMTING TEST RESULT
        std::cout << "\nTIMING TEST RESULT: [" << inputSizeStrings[i]<< "]"<<std::endl;
        std::cout << "=====================================================================" << std::endl;
        std::cout << "Alloc Opencl Buffer Time: " << alloc_opencl_buffer_time[i] << " microseconds" << std::endl;
        std::cout << "=====================================================================" << std::endl;
        std::cout << "Kernel Argument Setting Time: " << kernl_arg_time[i] << " microseconds" << std::endl;
        std::cout << "=====================================================================" << std::endl;
        std::cout << "Buffer to FPGA Time: " << buffer_to_fpga_time[i] << " microseconds" << std::endl;
        std::cout << "=====================================================================" << std::endl;
        std::cout << "Kernel Execution Time: " << kernl_execution_time[i] << " microseconds" << std::endl;
        std::cout << "=====================================================================" << std::endl;
        std::cout << "FPGA to Buffer Time: " << fpga_to_buffer_time[i] << " microseconds" << std::endl;
        std::cout << "=====================================================================\n" << std::endl;

        free(ptr_plainText);
        free(ptr_cipherText);
        // free(ptr_inputLength);
    }

    // Open a file for writing
    FILE *outputFile = fopen(OUTPUT_FILE_PATH, "w");
    if (outputFile == NULL) {
        printf("Failed to open the output file.\n");
        return 1;
    }

    fprintf(outputFile, "Data Size\tAlloc OpenCL Buffer Time (μs)\tKernel Argument Setting Time (μs)\tBuffer to FPGA Time (μs)\tKernel Execution Time (μs)\tFPGA to Buffer Time (μs)\n");

    // Write results to the file
    for (int i = 0; i < NUM_INPUTSIZES; ++i) {    
        // Write absolute times
        fprintf(outputFile, "%s\t%ld\t%ld\t%ld\t%ld\t%ld\n",
            inputSizeStrings[i].c_str(),
            alloc_opencl_buffer_time[i],
            kernl_arg_time[i],
            buffer_to_fpga_time[i],
            kernl_execution_time[i],
            fpga_to_buffer_time[i]);
    }

    // Write the header
    fprintf(outputFile, "Data Size\tAlloc OpenCL Buffer Time (%%)\tKernel Argument Setting Time (%%)\tBuffer to FPGA Time (%%)\tKernel Execution Time (%%)\tFPGA to Buffer Time (%%)\n");

    // Write the results
    for (int i = 0; i < NUM_INPUTSIZES; ++i) {
        // Calculate total time
        auto total_time = alloc_opencl_buffer_time[i] + kernl_arg_time[i] + buffer_to_fpga_time[i] + kernl_execution_time[i] + fpga_to_buffer_time[i];

        // Calculate percentages
        double alloc_opencl_buffer_percentage = (double)alloc_opencl_buffer_time[i] / total_time * 100;
        double kernl_arg_percentage = (double)kernl_arg_time[i] / total_time * 100;
        double buffer_to_fpga_percentage = (double)buffer_to_fpga_time[i] / total_time * 100;
        double kernl_execution_percentage = (double)kernl_execution_time[i] / total_time * 100;
        double fpga_to_buffer_percentage = (double)fpga_to_buffer_time[i] / total_time * 100;

        // Write percentages
        fprintf(outputFile, "%s\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\n",
                inputSizeStrings[i].c_str(),
                alloc_opencl_buffer_percentage,
                kernl_arg_percentage,
                buffer_to_fpga_percentage,
                kernl_execution_percentage,
                fpga_to_buffer_percentage);

    }

    // Close the file
    fclose(outputFile);
            
    return (match ? EXIT_FAILURE : EXIT_SUCCESS);  

}