#include "hmlib_top.h"
#include <string.h>
void memoryHandlerWrite(hls::stream<struct writeOutPkt>& pkt, ap_uint<512>* hostMemoryBuffer, 
	hls::stream<ap_uint<64>>& time, hls::stream<bool>& alarm, hls::stream<ap_uint<32>>& testStream){
	#pragma HLS inline off

	const unsigned int BURST_LENGTH_PRAGMA = 8;
	
	ap_uint<32> exit = 0;
	struct writeOutPkt getPkt;
	bool brokeChain = false;
	ap_uint<32> address = 0;
	ap_uint<32> outstandingWrites = 0;
	hls::stream<ap_uint<512> > values;
	#pragma HLS stream variable=values depth=BURST_LENGTH_PRAGMA
	hls::stream<ap_uint<32> > addresses;
	#pragma HLS stream variable=addresses depth=BURST_LENGTH_PRAGMA
	bool stopped = false;
	ap_uint<64> stats[2] = {0,0};

	SERVICE_MEMORY_WRITE: while(!stopped){
		#pragma HLS loop_tripcount max=10 min=10
		#pragma HLS pipeline II=1

		bool flush = false;
		if(pkt.read_nb(getPkt)){
			if(outstandingWrites == 0){
				address = getPkt.addr;
			}
			if(getPkt.stop == 1){
				flush = true;
				exit++;
			}
			if(getPkt.stop == 2){
				flush = true;
			}

			values.write(getPkt.value);
			addresses.write(getPkt.addr);

			if(getPkt.addr != address+outstandingWrites && !brokeChain){
				brokeChain = true;	
			}
			outstandingWrites++;
			
		}

		if(outstandingWrites == BURST_LENGTH_PRAGMA || flush || brokeChain){
			if(outstandingWrites == BURST_LENGTH_PRAGMA && !brokeChain){
				CHAIN_FLUSH: for(ap_uint<32> i = 0; i < BURST_LENGTH_PRAGMA; i++){
					ap_uint<32> throwAddr = addresses.read();
					hostMemoryBuffer[address+i] = values.read();
				}
				stats[0]++;
			}else{
				BREAK_CHAIN_FLUSH: for(ap_uint<32> i = 0; i < BURST_LENGTH_PRAGMA; i++){
					if(i < outstandingWrites-1){
						hostMemoryBuffer[addresses.read()] = values.read();
					}
				}
				ENFORCE_SLEEP:{
					time.write(4);
					ap_wait();
					bool wait = alarm.read();
					if(wait){
						hostMemoryBuffer[addresses.read()] = values.read();
					}
				}
				stats[1]++;	
			}

			brokeChain = false;
			address = 0;
			outstandingWrites = 0;
		}
		if(exit == PE_PER_HANDLER){
			stopped = true;
		}
		
	}

	time.write(0);
	ap_uint<512> put = 0;
	put.range(63,0) = stats[0];
	put.range(127,64) = stats[1];
	put.range(191,128) = testStream.read();
	hostMemoryBuffer[0] = put;
}

void sleepTimer(hls::stream<ap_uint<64>>& time, hls::stream<bool>& alarm){
	#pragma HLS inline off
	ap_uint<64> counter = 0;
	ap_uint<64> sleepTime = 0;

	SLEEP_TIMER: while(true){
		#pragma HLS pipeline II=1
		#pragma HLS loop_tripcount max=10 min=10

		if(counter == sleepTime){
			if(sleepTime != 0){
				alarm.write(true);
				sleepTime = 0;
			}
			bool read = time.read_nb(sleepTime);
			if(sleepTime == 0 && read){
				break;
			}
			counter = 0;
		}else{
			counter++;
		}
	}
}

void cycleCounter(hls::stream<ap_uint<1>>& command, hls::stream<ap_uint<64>>& outputCycle){
	#pragma HLS inline off
	ap_uint<64> counter = 0;	
	ap_uint<1> state = 1;

	CYCLE_COUNTER: while(true){
		#pragma HLS pipeline II=1
		#pragma HLS loop_tripcount max=10 min=10

		if(command.read_nb(state)){
			if(state == 0){
				break;
			}else if(state == 1){
				outputCycle.write(counter);
			}
		}
		counter++;
	}
}

void memoryHandleReadRequests(ap_uint<512>* hostMemorySection, 
	hls::stream<struct readPktReq>& readRequestMeta, 
	hls::stream<struct readPktReq>& readRequestData, 
	hls::stream<ap_uint<512>>& valueResponseMeta,
	hls::stream<ap_uint<512>>& valueResponseData){

	#pragma HLS inline off

	hls::stream<struct readPktReq> feedBack;
	#pragma HLS stream variable=feedBack depth=2
	ap_uint<1> multiplex = 0;
	ap_uint<1> multiplexStore = 0;
	ap_uint<32> counter = 0;

	READ_REQ_FETCH: while(true){
		#pragma HLS pipeline
		#pragma HLS loop_tripcount max=10 min=10

		bool readyToGet = false;
		struct readPktReq getPkt;

		if(feedBack.read_nb(getPkt)){
			readyToGet = true;
			multiplex = multiplexStore;
		}else if(readRequestData.read_nb(getPkt)){
			readyToGet = true;
			multiplex = 1;
		}else if(readRequestMeta.read_nb(getPkt)){
			readyToGet = true;
			multiplex = 0;
		}	

		if(readyToGet){
			ap_uint<512> value[BURST_LENGTH];

			memcpy(value,hostMemorySection+getPkt.addr,BURST_LENGTH*64);
			for(ap_uint<32> i = 0; i < BURST_LENGTH; i++){
				if(multiplex == 0 && i < getPkt.size){
					valueResponseMeta.write(value[i]);
				}else if(multiplex == 1 && i < getPkt.size){
					valueResponseData.write(value[i]);
				}
			}
			if(getPkt.size > BURST_LENGTH){
				getPkt.addr += BURST_LENGTH;
				getPkt.size -= BURST_LENGTH;
				multiplexStore = multiplex;
				feedBack.write(getPkt);
			}
		}

		if(getPkt.stop == 1){
			break;
		}
	}
}

void pollMeta(hls::stream<struct readPktReq>& readRequestMeta, 
	hls::stream<struct readPktReq>& readRequestData,
	hls::stream<ap_uint<512>>& valueMeta, 
	hls::stream<ap_uint<512>> toProcTask[2], 
	const ap_uint<32> BUFFER_SECTIONS, const ap_uint<32> DATA_IN_SECTION_SIZE,
	hls::stream<ap_uint<1>>& command, hls::stream<ap_uint<64>>& outputCycle, hls::stream<ap_uint<32>>& testStream){

	#pragma HLS inline off
	ap_uint<32> tmp = 0;
	ap_uint<64> diff = 0;
	ap_uint<64> valueCounter = 0;
	ap_int<32> tracker = 0;

	ap_uint<32> expectedProgramCounter = 1;
	ap_uint<32> currentProgramCounter = 0;
	ap_uint<32> bufferSectionCounter = 0;
	ap_uint<32> breakOut = 0;
	bool prefetchTrigger = false;
	bool sendOnce = false;

	SEND_META: while(breakOut != PE_PER_HANDLER){
		#pragma HLS pipeline
		#pragma HLS loop_tripcount max=10 min=10

		READ_COUNT:{
			command.write(1);
			ap_wait();
			valueCounter = outputCycle.read();
		}

		if(valueCounter - diff >= 32 && tracker <= BURST_LENGTH){
			readPktReq reqMeta;
			reqMeta.size = BURST_LENGTH;
			reqMeta.addr = tmp;
			reqMeta.stop = 0;
			
			if(readRequestMeta.write_nb(reqMeta)){
				tracker += BURST_LENGTH;
				tmp += BURST_LENGTH;
				if(tmp == BUFFER_SECTIONS){
					tmp = 0;
				}
			}
			diff = valueCounter;
		}
	
		ap_uint<512> getMetaData;
		if(valueMeta.read_nb(getMetaData)){
			if(getMetaData.range(479,448) != currentProgramCounter && getMetaData.range(479,448) == expectedProgramCounter){
				if(getMetaData.range(15,0) == 1){
					breakOut++;
				}
				if(!prefetchTrigger){
					prefetchTrigger = true;
				}else{
					getMetaData.range(63,48) = 12345;
				}

				getMetaData.range(511,480) = getMetaData.range(479,448);

				ap_uint<512> toSendProc;
				ap_uint<512> toRecvProc;

				toRecvProc = getMetaData;
				toSendProc = getMetaData;

				toProcTask[0].write(toSendProc);
				toProcTask[1].write(toRecvProc);

				readPktReq reqData;
				reqData.size = getMetaData.range(95,64);
				reqData.addr = BUFFER_SECTIONS+bufferSectionCounter*DATA_IN_SECTION_SIZE;
				reqData.stop = 0;

				
				readRequestData.write(reqData);

				bufferSectionCounter++;
				currentProgramCounter++;
				expectedProgramCounter++;
				if(bufferSectionCounter == BUFFER_SECTIONS){
					bufferSectionCounter = 0;
				}
			}else{
				prefetchTrigger = false;
			}
			tracker--;
		}
	}

	TEST_IO:{
		READ_REMAINDER: for(ap_int<32> i = 0; i < tracker; i++){
			#pragma HLS loop_tripcount max=2 min=2
			ap_uint<512> get = valueMeta.read();
		}

		ap_wait();
		testStream.write(tracker);
		readPktReq reqMeta;
		reqMeta.size = 0;
		reqMeta.addr = 0;
		reqMeta.stop = 1;
		readRequestMeta.write(reqMeta);
		ap_wait();
		command.write(0);
	}
}

void sendDataUser(hls::stream<ap_uint<512>>& valueData,
	hls::stream<ap_uint<512>>& fromWaitTask,
	hls::stream<ap_uint<512> > rerouteToUser[PE_PER_HANDLER]){

	#pragma HLS inline off
	
	ap_uint<32> iterationsCounter = 0;
	ap_uint<32> fsm = 0;

	ap_uint<512> metaData = 0;
	ap_uint<32> batchCount = 0;
	ap_uint<32> totalIterations = 0;
	ap_uint<16> peToUse = 0;
	ap_uint<128> elements = 0;
	ap_uint<512> fromWaitProc;

	SEND_INPUTS_DATA: while(true){
		#pragma HLS loop_tripcount max=20 min=20
		#pragma HLS pipeline II=1
		
		if(fsm == 0){
			if(fromWaitTask.read_nb(fromWaitProc)){
				metaData = fromWaitProc;

				totalIterations = metaData.range(95,64);
				fsm = 1;
			}
		}else if(fsm == 1){
			batchCount = metaData.range(63,32);
			elements = metaData.range(223,96);

			ap_uint<32> currentCode = metaData.range(15,0);

			ap_uint<512> sendData = currentCode;
			sendData.range(63,32) = batchCount;
			sendData.range(95,64) = totalIterations;
			sendData.range(223,96) = elements;
			rerouteToUser[peToUse].write(sendData);
				
			iterationsCounter = 0;
			fsm = 2;	
		}else if(fsm == 2){
			if(iterationsCounter >= totalIterations){
				fsm = 0;
				peToUse++;

				if(peToUse == PE_PER_HANDLER){
					peToUse = 0;
				}
			}else{
				ap_uint<512> sendData;
				if(valueData.read_nb(sendData)){
					rerouteToUser[peToUse].write(sendData);
					iterationsCounter++;
				}
			}	
			if(metaData.range(15,0) == 1){
				break;
			}
		}
	}	
}

void receiveDataUser(hls::stream<struct writeOutPkt>& outPktData,
	hls::stream<ap_uint<512>>& fromWaitTask,
	hls::stream<ap_uint<513>> rerouteFromUser[PE_PER_HANDLER],
	hls::stream<bool> stopSignalReroute[PE_PER_HANDLER],
	const ap_uint<32> BUFFER_SECTIONS, const ap_uint<32> DATA_IN_SECTION_SIZE, const ap_uint<32> DATA_OUT_SECTION_SIZE){
	#pragma HLS inline off

	ap_uint<32> memIndexOut = 0;
	ap_uint<512> metaData = 0;
	ap_uint<4> fsm = 0;

	ap_uint<32> peToUse = 0;
	ap_uint<32> bufferSectionCounter = 0;
	ap_uint<32> batchCount = 0;

	ap_uint<4> count = 0;

	ap_uint<513> dataFromUser;
	ap_uint<512> fromSendProc;

	RECEIVE_HASHES: while(true){
		#pragma HLS loop_tripcount max=10 min=10
		#pragma HLS pipeline
		if(fsm == 0){
			if(fromWaitTask.read_nb(fromSendProc)){
				metaData = fromSendProc;
				batchCount = metaData.range(47,32);

				count = 0;
				memIndexOut = 0;
				fsm = 1;
			}
		}else if(fsm == 1){	
			if(rerouteFromUser[peToUse].read_nb(dataFromUser)){
				metaData.range(31,16) = dataFromUser.range(15,0);	
				fsm = 2;
			}
		}else if(fsm == 2){
			if(rerouteFromUser[peToUse].read_nb(dataFromUser)){
				struct writeOutPkt pkt;
				pkt.addr = BUFFER_SECTIONS+BUFFER_SECTIONS*DATA_IN_SECTION_SIZE+bufferSectionCounter*DATA_OUT_SECTION_SIZE+memIndexOut;
				pkt.stop = 0;

				if(dataFromUser.range(512,512) == 1){
					metaData.range(255+count*32,224+count*32) = dataFromUser.range(31,0);

					fsm = 3;
				}else{
					pkt.value = dataFromUser.range(511,0);
					memIndexOut++;
					outPktData.write(pkt);
				}
			}
			
		}else if(fsm == 3){
			count++;
			if(count < batchCount){
				fsm = 2;
			}else{
				struct writeOutPkt pkt;				

				pkt.addr = bufferSectionCounter;
				pkt.stop = 0;

				metaData.range(447,416) = 2;
					
				if(metaData.range(15,0) == 1){
					pkt.stop = 1;
				}else{
					pkt.stop = 2;
				}
				pkt.value = metaData;
				outPktData.write(pkt);

				fsm = 0;

				bufferSectionCounter++;
				peToUse++;

				if(bufferSectionCounter == BUFFER_SECTIONS){
					bufferSectionCounter = 0;
				}
				if(peToUse == PE_PER_HANDLER){
					peToUse = 0;
				}

				if(metaData.range(15,0) == 1){
					break;
				}
			}
		}
	}
	for(ap_uint<32> i = 0; i < PE_PER_HANDLER; i++){
		stopSignalReroute[i].write(true);
	}
}

void wrapperUserHostMemPE(ap_uint<512>* hostMemorySection,
	hls::stream<ap_uint<512> > rerouteToUser[PE_PER_HANDLER],
	hls::stream<ap_uint<513> > rerouteFromUser[PE_PER_HANDLER],
	hls::stream<bool> stopSignal[PE_PER_HANDLER],
	const ap_uint<32> BUFFER_SECTIONS, const ap_uint<32> DATA_IN_SECTION_SIZE, const ap_uint<32> DATA_OUT_SECTION_SIZE){

	#pragma HLS inline off

	#pragma HLS dataflow

	hls::stream<ap_uint<512>> waitToProcs[2];
	#pragma HLS stream variable=waitToProcs depth=4
	hls::stream<struct writeOutPkt> outPktDataPipe;
	#pragma HLS stream variable=outPktDataPipe depth=4

	hls::stream<struct readPktReq> readRequestMeta;
	#pragma HLS stream variable=readRequestMeta depth=4
	hls::stream<struct readPktReq> readRequestData;
	#pragma HLS stream variable=readRequestData depth=4

	hls::stream<ap_uint<512>> valueResponseMeta;
	#pragma HLS stream variable=valueResponseMeta depth=16
	#pragma HLS bind_storage variable=valueResponseMeta type=FIFO impl=bram
	hls::stream<ap_uint<512>> valueResponseData;
	#pragma HLS stream variable=valueResponseData depth=8

	hls::stream<ap_uint<1>> command;
	hls::stream<ap_uint<64>> outputCycle;
	hls::stream<ap_uint<32>> testStream;

	hls::stream<ap_uint<64>> time;
	hls::stream<bool> alarm;

	cycleCounter(command, outputCycle);
	sleepTimer(time, alarm);

	memoryHandleReadRequests(hostMemorySection, 
		readRequestMeta, 
		readRequestData, 
		valueResponseMeta,
		valueResponseData);

	pollMeta(readRequestMeta, 
		readRequestData,
		valueResponseMeta, 
		waitToProcs, 
		BUFFER_SECTIONS, DATA_IN_SECTION_SIZE,
		command, outputCycle, testStream);
	
	sendDataUser(valueResponseData,
		waitToProcs[0],
		rerouteToUser);
 
	receiveDataUser(outPktDataPipe,
		waitToProcs[1],
		rerouteFromUser,
		stopSignal,
		BUFFER_SECTIONS, DATA_IN_SECTION_SIZE, DATA_OUT_SECTION_SIZE);

	memoryHandlerWrite(outPktDataPipe, hostMemorySection, 
		time, alarm, testStream);	
}	

void wrapperHostMemStrmToUser(hls::stream<ap_uint<512> >& rerouteToUser, hls::stream<ap_axiu<512,0,0,0> >& hostMemStrmToUser, hls::stream<bool>& stopSignal){
	#pragma HLS inline off

	MULTIPLEX_USER_PE_PIPE_IN: while(true){
		#pragma HLS loop_tripcount max=100 min=100
		ap_axiu<512,0,0,0> sendPkt;
			
		if(rerouteToUser.read_nb(sendPkt.data)){
			hostMemStrmToUser.write(sendPkt);
		}
		
		bool exit = false;
		if(stopSignal.read_nb(exit)){
			if(exit == true){
				break;
			}
		}
	}
}

void wrapperHostMemStrmFromUser(hls::stream<ap_uint<513> >& rerouteFromUser, hls::stream<ap_axiu<514,0,0,0> >& hostMemStrmFromUser){
	#pragma HLS inline off

	MULTIPLEX_USER_PE_PIPE_OUT: while(true){
		#pragma HLS loop_tripcount max=100 min=100
		ap_axiu<514,0,0,0> getPkt;

		getPkt = hostMemStrmFromUser.read();
		rerouteFromUser.write(getPkt.data.range(512,0));

		if(getPkt.data.range(513,513) == 1){
			break;
		}
	}
}


