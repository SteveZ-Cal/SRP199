#ifndef HMLIB_TOP_H
#define HMLIB_TOP_H

#include <ap_axi_sdata.h>
#include <ap_int.h>
#include <ap_utils.h>
#include <hls_stream.h>
#include <iostream>

#include <stdint.h>
#include <math.h>
#include <limits.h>
	 
#define HOST_MEM_FROM_USER_STREAM_DEF hls::stream<ap_axiu<514,0,0,0> >& hostMemStrmFromUser1

#define HOST_MEM_TO_USER_STREAM_DEF hls::stream<ap_axiu<512,0,0,0> >& hostMemStrmToUser1

//functions to handle memory between host and streams(set exit, set mode)
//SET PE TO EXIT code = 1
//SET PE TO PROCESS code = 2

#define BURST_LENGTH 8
#define BURST_LENGTH_WRITE 1
#define MAX_PE 1
#define HM_HANDLERS 1
#define PE_PER_HANDLER (MAX_PE/HM_HANDLERS)

struct writeOutPkt{
	ap_uint<32> addr;
	ap_uint<512> value;
	ap_uint<2> stop;
};

struct handleMemPkt{
	ap_uint<512> value;
	ap_uint<1> stop;
};

struct readPktReq{
	ap_uint<32> addr;
	ap_uint<64> size;
	ap_uint<1> stop;
};

void memoryHandlerWrite(hls::stream<struct writeOutPkt>& pkt, ap_uint<512>* hostMemoryBuffer, 
	hls::stream<ap_uint<64>>& time, hls::stream<bool>& alarm, hls::stream<ap_uint<32>>& testStream);
void cycleCounter(hls::stream<ap_uint<1>>& command, hls::stream<ap_uint<64>>& outputCycle);
void sleepTimer(hls::stream<ap_uint<64>>& time, hls::stream<bool>& alarm);
void memoryHandleReadRequests(ap_uint<512>* hostMemorySection, 
	hls::stream<struct readPktReq>& readRequestMeta, 
	hls::stream<struct readPktReq>& readRequestData, 
	hls::stream<ap_uint<512>>& valueResponseMeta,
	hls::stream<ap_uint<512>>& valueResponseData);
void pollMeta(hls::stream<struct readPktReq>& readRequestMeta, 
	hls::stream<struct readPktReq>& readRequestData,
	hls::stream<ap_uint<512>>& valueMeta, 
	hls::stream<ap_uint<512>> toProcTask[2], 
	const ap_uint<32> BUFFER_SECTIONS, const ap_uint<32> DATA_IN_SECTION_SIZE, const ap_uint<32> DATA_OUT_SECTION_SIZE,
	hls::stream<ap_uint<1>>& command, hls::stream<ap_uint<64>>& outputCycle, hls::stream<ap_uint<32>>& testStream);
void sendDataUser(hls::stream<ap_uint<512>>& valueData,
	hls::stream<ap_uint<512>>& fromWaitTask,
	hls::stream<ap_uint<512> > rerouteToUser[PE_PER_HANDLER]);
void receiveDataUser(hls::stream<struct writeOutPkt>& outPktData,
	hls::stream<ap_uint<512>> fromWaitTask[2],
	hls::stream<ap_uint<513> > rerouteFromUser[PE_PER_HANDLER],
	hls::stream<bool> stopSignalReroute[PE_PER_HANDLER],
	const ap_uint<32> BUFFER_SECTIONS, const ap_uint<32> DATA_IN_SECTION_SIZE, const ap_uint<32> DATA_OUT_SECTION_SIZE);
void wrapperUserHostMemPE(ap_uint<512>* hostMemorySection,
	hls::stream<ap_uint<512> > rerouteToUser[PE_PER_HANDLER],
	hls::stream<ap_uint<513> > rerouteFromUser[PE_PER_HANDLER],
	hls::stream<bool> stopSignal[PE_PER_HANDLER],
	const ap_uint<32> BUFFER_SECTIONS, const ap_uint<32> DATA_IN_SECTION_SIZE, const ap_uint<32> DATA_OUT_SECTION_SIZE);
void wrapperHostMemStrmToUser(hls::stream<ap_uint<512> >& rerouteToUser, hls::stream<ap_axiu<512,0,0,0> >& hostMemStrmToUser, hls::stream<bool>& stopSignal);
void wrapperHostMemStrmFromUser(hls::stream<ap_uint<513> >& rerouteFromUser, hls::stream<ap_axiu<514,0,0,0> >& hostMemStrmFromUser);


#endif

