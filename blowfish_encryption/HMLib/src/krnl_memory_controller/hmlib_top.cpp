#include "hmlib_top.h"

extern "C"{
void memAccelerate(ap_uint<32> bufferSections,
		ap_uint<32> dataInSectionSize,
		ap_uint<32> dataOutSectionSize,

		ap_uint<512>* hostMemoryBufferUser1,

		HOST_MEM_FROM_USER_STREAM_DEF,
		HOST_MEM_TO_USER_STREAM_DEF){

	#pragma HLS INTERFACE m_axi port=hostMemoryBufferUser1 num_read_outstanding=32 num_write_outstanding=32 offset=slave bundle=gmem1

	#pragma HLS INTERFACE s_axilite port=hostMemoryBufferUser1 
	#pragma HLS INTERFACE axis port=hostMemStrmFromUser1
	#pragma HLS INTERFACE axis port=hostMemStrmToUser1


	#pragma HLS INTERFACE s_axilite port=return
	ap_uint<32> DATA_OUT_SECTION_SIZE = dataOutSectionSize;
	ap_uint<32> BUFFER_SECTIONS = bufferSections;
	ap_uint<32> DATA_IN_SECTION_SIZE = dataInSectionSize;

	hls::stream<ap_uint<512> > rerouteToUser[HM_HANDLERS][PE_PER_HANDLER];
	#pragma HLS stream variable=rerouteToUser depth=16
	hls::stream<ap_uint<513> > rerouteFromUser[HM_HANDLERS][PE_PER_HANDLER];
	#pragma HLS stream variable=rerouteFromUser depth=16
	hls::stream<bool> stopSignal[HM_HANDLERS][PE_PER_HANDLER];

	#pragma HLS dataflow

	wrapperHostMemStrmFromUser(rerouteFromUser[0][0], hostMemStrmFromUser1);

	wrapperUserHostMemPE(hostMemoryBufferUser1,
		rerouteToUser[0], 
		rerouteFromUser[0],
		stopSignal[0],
		BUFFER_SECTIONS, DATA_IN_SECTION_SIZE, DATA_OUT_SECTION_SIZE);

	wrapperHostMemStrmToUser(rerouteToUser[0][0], hostMemStrmToUser1, stopSignal[0][0]);
	
}
}
