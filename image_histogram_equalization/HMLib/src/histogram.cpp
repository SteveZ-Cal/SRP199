#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <hls_stream.h>
#include <ap_utils.h>
#include <ap_int.h>
#include <ap_axi_sdata.h>


#include <string.h>
#include <math.h>
#include <stdlib.h>

#define BUS_WIDTH_BYTES 64
#define stevez_debug 0

#include <iostream>
#include <vector>
#include <numeric>
#include <cassert>
#include <cmath>

static const double one { 1 }; 
static const double eps { 1e-8 }; 

#define VERBOSE 0 // turn this on in sw_emu if you want to see the intermediate results
#define DEBUG 0 // turn this on in sw_emu if logic is not working as expected
#define BINS_NUM 256

void cal_acc_hist(double* freq, int inputLength, double* ret) {
    
    double acc {}; 

    for (uint32_t i = 0; i < inputLength; ++i) {
        // HLS pragma to pipeline the loop with initiation interval of 1
        #pragma HLS PIPELINE II=1
        acc += freq[i]; 
        ret[i] = acc; 
    }

    assert(abs(acc - one) < eps); 

}

void cal_round(size_t l, double* acc_hist, size_t* ret) {

    for (uint32_t i = 0; i < l; ++i) {
        // HLS pragma to pipeline the loop with initiation interval of 1
        #pragma HLS PIPELINE II=1
        ret[i] = static_cast<size_t>((l - 1) * acc_hist[i] + 0.5); 
    }

}

void cal_new_freq(double* freq, size_t* round, int inputLength, double* ret) {

    for (uint32_t i = 0; i < inputLength; ++i) {
        // HLS pragma to pipeline the loop with initiation interval of 1
        #pragma HLS PIPELINE II=1
        ret[round[i]] += freq[i]; 
        
    }

}


void krnl_histogram_equalization(hls::stream<ap_uint<512>>& hostMemStrmToUserBuffer, hls::stream<ap_axiu<514,0,0,0> >& hostMemStrmFromUser, unsigned int sizes[4], unsigned int& iterations, unsigned int batchCount){

	/********************************START OF HISTOGRAM INITIALIZATION************************************/

	double freq_plainText[BINS_NUM] = {0}, acc_hist[BINS_NUM] = {0}, freq_cipherText[BINS_NUM] = {0};
	size_t round[BINS_NUM] = {0}; 

	/********************************END OF HISTOGRAM INITIALIZATION************************************/

	unsigned int countNum = 0;
	unsigned int size = 0;	
	unsigned int outputSize = BINS_NUM; 
	double inputLength = sizes[countNum];

	//TODO: MODIFY YOUR CORE KERNEL TO HANDLE 512 BIT INPUT
	COMPUTE: for(int i = 0; i < iterations; i++){
		// #pragma HLS loop_tripcount min=8 max=8
		// #pragma HLS pipeline II=32

		uint8_t ptr_plainText[BUS_WIDTH_BYTES];

		ap_uint<512> get1 = hostMemStrmToUserBuffer.read();

		for(uint32_t j = 0; j < BUS_WIDTH_BYTES; j++){
            // HLS pragma to pipeline the loop with initiation interval of 1
            #pragma HLS PIPELINE II=1
			// reformat the input data to 64 bytes for blowfish
			ptr_plainText[j] = get1.range(8*j+7,8*j);
            assert(ptr_plainText[j] < BINS_NUM && ptr_plainText[j] >= 0); 
            freq_plainText[ptr_plainText[j]] += 1;
        }
	
		if (VERBOSE && DEBUG){
			printf("inputSize: %d\n", BUS_WIDTH_BYTES);
			for (int i = 0; i < BUS_WIDTH_BYTES; i++) {
				std::cout<<ptr_plainText[i];
			}
			printf("\n");
		}

		size += 64;
		if(size >= sizes[countNum]){
			
			countNum++;
			size = 0;

			for(uint32_t i = 0; i < BINS_NUM; i++){
				// HLS pragma to pipeline the loop with initiation interval of 1
				#pragma HLS PIPELINE II=1
				freq_plainText[i] /= (double)inputLength;
				// Initialize the output
				freq_cipherText[i] = 0;
        	}

			if (VERBOSE & DEBUG){
				std::cout << "origin_hist\t\t"; 
				for (size_t i = 0; i < BINS_NUM; ++i) {
					if(freq_plainText[i] != 0)
						std::cout << "Index: " << i << ", Element: " << freq_plainText[i] << "\t";
				}
				std::cout << std::endl;
        	}
        
			// Calculate
			cal_acc_hist(freq_plainText, BINS_NUM, acc_hist);
			if (VERBOSE){
				std::cout << "acc_hist\t\t"; 
				for (double elem : acc_hist) std::cout << elem << "\t";
				std::cout << std::endl; 
			}

			if(VERBOSE && DEBUG){
				std::cout << "origin_hist after cal_acc_hist:\n"; 
				for (size_t i = 0; i < BINS_NUM; ++i) {
					std::cout << freq_plainText[i] << "\t";
				}
				std::cout << std::endl;
			}

			cal_round(BINS_NUM, acc_hist, round);
			if (VERBOSE){
				std::cout << "round\t\t\t"; 
				for (size_t elem : round) std::cout << elem << "\t";
				std::cout << std::endl; 
			}

			if(VERBOSE && DEBUG){
				std::cout << "origin_hist after cal_round:\n"; 
				for (size_t i = 0; i < BINS_NUM; ++i) {
					std::cout << freq_plainText[i] << "\t";
				}
				std::cout << std::endl;
			}

			cal_new_freq(freq_plainText, round, BINS_NUM, freq_cipherText);

			if (VERBOSE){
				std::cout << "new_freq\t\t"; 
				for(size_t i=0; i<BINS_NUM; i++){
					std::cout << freq_cipherText[i] << "\t";
				}
				std::cout << std::endl; 
			}

			//TODO: SEND OUTPUT DATA ONCE 512 BITS ARE FILLED OR LAST OUTPUT
			ENFORCE_ORDER:{
				
				for(int loop = 0; loop < BINS_NUM/BUS_WIDTH_BYTES; loop++){
					ap_axiu<514,0,0,0> sendPkt;
					#pragma HLS unroll
					for(int k = 0; k < BUS_WIDTH_BYTES; k++){
						#pragma HLS unroll
						sendPkt.data.range(8*k+7, 8*k) = freq_cipherText[loop*BUS_WIDTH_BYTES + k];
					}
					hostMemStrmFromUser.write(sendPkt);
				}
				
				ap_wait();
				
				ap_axiu<514,0,0,0> sendPkt;

				//TODO: IF ITS THE LAST OUTPUT, SEND OUTPUT SIZE AND SET BIT-512 TO 1
				sendPkt.data = outputSize;
				sendPkt.data.range(512,512) = 1;
				hostMemStrmFromUser.write(sendPkt);
			}

		}
		
	}
    
}


void functionControl(hls::stream<ap_uint<512>>& hostMemStrmToUserBuffer, hls::stream<ap_axiu<514,0,0,0> >& hostMemStrmFromUser, 
	bool state[2], unsigned int sizes[4], unsigned int& iterations, unsigned int batchCount){
	#pragma HLS inline off

	ap_uint<512> getPkt = hostMemStrmToUserBuffer.read();
	ap_axiu<514,0,0,0> sendPkt;
	ap_uint<512> code = 0;
	code = getPkt;

	//SET PE TO EXIT code = 1
	//SET PE TO COMPUTE code = 2

	//TODO: BUILD YOUR OWN FSM OR KEEP THE CURRENT VERSION
	if(code.range(31,0) == 1){
		//TODO: THIS IS EXIT CODE. MUST BE 1
		state[0] = true;
		//TODO: ACKNOWLEDGE THE CODE AND SET BIT-512,513 TO 1 FOR KERNEL FINISH
		sendPkt.data = 1;
		hostMemStrmFromUser.write(sendPkt);
		sendPkt.data.range(512,512) = 1;
		sendPkt.data.range(513,513) = 1;
		hostMemStrmFromUser.write(sendPkt);
	}else if(code.range(31,0) == 2){
		//TODO: CODE 2 MATCHES WITH HELPER.CPP
		//GET THE NUMBER OF ITERATION IN TERMS OF 64 BYTES FROM BIT RANGE 64-95
		//batchCount = code.range(63,32);
		iterations = code.range(95,64);
		
		//TODO: GET THE ORIGINAL INPUT SIZE FROM BIT 96-127
		sizes[0] = code.range(127,96);
		/*sizes[1] = code.range(159,128);
		sizes[2] = code.range(191,160);
		sizes[3] = code.range(223,192);*/

		//TODO: ACKNOWLEDGE THE CODE
		state[1] = true;
		sendPkt.data = 2;
		hostMemStrmFromUser.write(sendPkt);
	}	
}

template <int number>
void histogramPE(hls::stream<ap_uint<512>>& hostMemStrmToUserBuffer, hls::stream<ap_axiu<514,0,0,0> >& hostMemStrmFromUser, hls::stream<bool>& stopSignal){
	#pragma HLS inline off

	unsigned sizes[4];
	unsigned int iterations;
	unsigned int batchCount;
	bool state[2];
	#pragma HLS array_partition variable=state dim=0 complete

	for(int i = 0; i < 2; i++){
		#pragma HLS unroll
		state[i] = false;
	}

	//TODO: MODIFY TO FIT YOUR OWN FSM
	while(!state[0]){
		#pragma HLS loop_tripcount max=10 min=10

		functionControl(hostMemStrmToUserBuffer, hostMemStrmFromUser, state, sizes, iterations, batchCount);
		if(state[1]){
			krnl_histogram_equalization(hostMemStrmToUserBuffer, hostMemStrmFromUser, sizes, iterations, batchCount);
			state[1] = false;
		}
	}
	stopSignal.write(true);
}

void bufferData(hls::stream<ap_axiu<512,0,0,0> >& hostMemStrmToUser, hls::stream<ap_uint<512>>& bufferFIFO, hls::stream<bool>& stopSignal){
	#pragma HLS inline off
	while(true){
		#pragma HLS pipeline II=1
		ap_axiu<512,0,0,0> getPkt;
		bool signal;
		if(hostMemStrmToUser.read_nb(getPkt)){
			bufferFIFO.write(getPkt.data);
		}
		if(stopSignal.read_nb(signal)){
			if(signal == true){
				break;
			}
		}
	}
}

extern "C" {

void histogram_HM(hls::stream<ap_axiu<512,0,0,0> >& hostMemStrmToUser1, hls::stream<ap_axiu<514,0,0,0> >& hostMemStrmFromUser1) {
	#pragma HLS INTERFACE s_axilite port=return bundle=control
	
	hls::stream<ap_uint<512>> hostMemFIFOBuffer("hostMemFIFOBuffer");
	#pragma HLS STREAM variable=hostMemFIFOBuffer depth=1024
	hls::stream<bool> stopSignal;

	#pragma HLS DATAFLOW
	//TODO: MODIFY YOUR TOP LEVEL KERNEL TO BE LIKE THIS
	//MUST HAVE THESE TWO STREAM INTERFACE hostMemStrmToUser1, hostMemStrmFormUser1
	//hostMemStrmToUser1 IS USED TO RECEIVE DATA FROM HMLIB KERNEL
	//hostMemStrmFromUser1 IS TO SEND DATA TO HMLIB KERNEL
	bufferData(hostMemStrmToUser1, hostMemFIFOBuffer,stopSignal);
	histogramPE<0>(hostMemFIFOBuffer,hostMemStrmFromUser1,stopSignal);
}
}
