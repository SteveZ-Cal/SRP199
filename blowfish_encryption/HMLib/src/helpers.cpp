#include "helpers.h"

unsigned int customRound(unsigned int valueToRound, unsigned int round){
	if(valueToRound%round != 0){
		return valueToRound + round - (valueToRound%round);
	}
	return valueToRound;
}

//TODO: CHANGE FUNCTION INTERFACE FOR INPUT VECTORS
std::atomic<bool> threadsReady[HMLIB_HANDLERS][2] = {false};
void parallelTaskSend(HMLib& HMLibObject, struct HMLibUniqueHandler* HMLibUH, const std::vector<char*>& inputs, const std::vector<unsigned int>& inputSizes, bool& pass){
	pass = true;
	unsigned int HMLibID = HMLibUH->HMLibID;

	threadsReady[HMLibID][0] = true;
	while(!threadsReady[HMLibID][1]);

	uint64_t timeStart = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	std::string msg = "Thread Receiver: " + std::to_string(HMLibID) + " ---- START: " + std::to_string(timeStart) + "\n";
	HMLibObject.printForMe(msg);

	unsigned int totalInputs = inputSizes.size();
	for(unsigned int j = 0; j < totalInputs;){		
		unsigned int batchedReq = 1;
		unsigned int batched = 0;

		std::chrono::steady_clock::time_point sendStart = std::chrono::steady_clock::now();
	
		if(batchedReq + j > totalInputs){
			batchedReq = totalInputs - j;
		}

		while(true){
			uint64_t timeout;
			#ifdef HW_SIM
				timeout = (uint64_t)60*1000*1000*1000;
			#else
				timeout = (uint64_t)30*1000*1000*1000;
			#endif
			//TODO: CHANGE THE CODE 2 OR KEEP IT.
			//THIS IS TO SIGNAL YOUR COMPUTE KERNEL WHAT TO DO
			int ec = HMLibObject.sendInput((const char**)((const char*)(inputs.data()+j)), inputSizes.data()+j, batchedReq, batched, 2, timeout, HMLibUH);

			if(ec >= 0){
				#ifdef HW_SIM
					std::string msg = "HMLib: " + std::to_string(HMLibID) + " --- Input sent\n";
					HMLibObject.printForMe(msg);
				#endif
				break;
			}else if(ec == -1){
				for(unsigned k = 0; k < HMLibUH->bufferSections; k++){
					char* metaPtr = HMLibUH->metaStart + HMLibUH->metaSize*k;
					unsigned int status = ((unsigned int*) metaPtr)[13];
					std::string msg = "HMLib: " + std::to_string(HMLibID) + " --- Could not find an input. Status: " + std::to_string(status) + "\n";
					HMLibObject.printForMe(msg);
				}

				#ifdef HW_SIM
					char* metaPtr = HMLibUH->inputMetaPtr;
					std::string metaInspect = "";
					for(unsigned k = 0; k < HMLibUH->bufferSections; k++){
						metaInspect += "Section: ";
						unsigned int section = HMLibUH->bufferSections - (HMLibUH->metaEnd - metaPtr)/HMLibUH->metaSize;
						metaInspect += std::to_string(section) + " ";

						for(unsigned l = 0; l < HMLibUH->metaSize; l++){
							metaInspect += std::to_string((int)metaPtr[l]) + " ";
						}
						metaInspect += "\n";

						char* inputPtr = HMLibUH->inputStart + (HMLibUH->bufferSections - (HMLibUH->metaEnd - metaPtr)/HMLibUH->metaSize) * HMLibUH->inputSize;
						unsigned int position = 0;
						for(unsigned int k = 0; k < 4; k++){
							if(((unsigned int*)(metaPtr+2))[k] != 0){
								metaInspect += std::string(inputPtr+position,((unsigned int*)(metaPtr+2))[k]) + "\n\n";
								position += ((unsigned int*)(metaPtr+2))[k];
								position = customRound(position,64);
							}
						}

						metaPtr += HMLibUH->metaSize;
						if(metaPtr == HMLibUH->metaEnd){
							metaPtr = HMLibUH->metaStart;
						}
					}
					
					std::string msg = "HMLib: " + std::to_string(HMLibID) + " --- INPSECT META BUFFER \n" + metaInspect;
				#else
					std::string msg = "HMLib: " + std::to_string(HMLibID) + " --- Could not find an available PE. Exiting\n";
				#endif

				HMLibObject.printForMe(msg);
				pass = false;
				//delete[] batchBuffer;
				return;
			}else if(ec == -2){
				std::string msg = "HMLib: " + std::to_string(HMLibID) + " --- -2 code during send\n";
				HMLibObject.printForMe(msg);
				pass = false;
				//delete[] batchBuffer;
				return;
			}
		}
		j += batched;

		std::chrono::steady_clock::time_point sendEnd = std::chrono::steady_clock::now();
		std::chrono::duration<double> duration = sendEnd - sendStart;
		HMLibUH->oneSendTime += std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
	}
	msg = "HMLib: " + std::to_string(HMLibID) + " --- Finished sending\n";
	HMLibObject.printForMe(msg);
	threadsReady[HMLibID][0] = false;
	//delete[] batchBuffer;
}

//TODO: CHANGE FUNCTION INTERFACE FOR OUTPUT VECTORS
void parallelTaskReceive(HMLib& HMLibObject, struct HMLibUniqueHandler* HMLibUH, std::vector<unsigned int>& answers, const unsigned int entries, const bool enableCheck, bool& pass){

	pass = true;
	unsigned int HMLibID = HMLibUH->HMLibID;
	unsigned int processed = 0;

	if(entries == 0){
		std::string msg = "Thread Receiver: --- No expected inputs for this thread\n";
		HMLibObject.printForMe(msg);
		return;
	}

	while(!threadsReady[HMLibID][0]);
	threadsReady[HMLibID][1] = true;
	
	char* tmpCopy[MAX_BATCH_SIZE];

	for(unsigned int i = 0; i < MAX_BATCH_SIZE; i++){
		tmpCopy[i] = new char[HMLibUH->oneEntry+HMLibUH->metaSize];
	}
	unsigned int outSizes[MAX_BATCH_SIZE] = {0};

	std::chrono::steady_clock::time_point totalStart = std::chrono::steady_clock::now();
	while(true){
		unsigned int batchProcessed = 0;
		std::chrono::steady_clock::time_point recvStart = std::chrono::steady_clock::now();

		while(true){
			
			uint64_t timeout;
			#ifdef HW_SIM
				timeout = (uint64_t)60*1000*1000*1000;
			#else
				timeout = (uint64_t)30*1000*1000*1000;
			#endif

			int ec = HMLibObject.checkOutput(tmpCopy, outSizes, batchProcessed, timeout, HMLibUH);

			if(ec == 0){
				break;
			}else if(ec == -1){
				char* metaPtr = HMLibUH->outputMetaPtr;
				unsigned int status = ((unsigned int*) metaPtr)[13];

				std::string msg = "Thread Receiver: " + std::to_string(HMLibID) + " --- Could not find an output " + std::to_string(status) + " " 
					+ std::to_string(processed) + "/" + std::to_string(entries) + "\n";
				HMLibObject.printForMe(msg);

				msg = "Thread Receiver: " + std::to_string(HMLibID) + " --- Could not find an output. ";
				for(unsigned int i = 0; i < 4; i++){
					msg += std::to_string(((unsigned int*) metaPtr)[8+i]) + " ";
				}
				msg += "\n";
				msg += "Exiting\n";
				
				HMLibObject.printForMe(msg);
				pass = false;
				for(unsigned int i = 0; i < MAX_BATCH_SIZE; i++){
					delete[] tmpCopy[i];
				}
				return;
			}if(ec == -2){
				pass = false;
				for(unsigned int i = 0; i < MAX_BATCH_SIZE; i++){
					delete[] tmpCopy[i];
				}
				return;
			}
		}
		std::chrono::steady_clock::time_point recvEnd = std::chrono::steady_clock::now();
		std::chrono::duration<double> duration = recvEnd - recvStart;
		HMLibUH->oneReadTime += std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();

		processed += batchProcessed;
		if(enableCheck){
			//TODO: MODIFY TO STORE YOUR OUTPUT INTO AN OUTPUT VECTOR
			//YOU MUST KEEP THE HMLiubUH->metaSize OFFSET WHEN COPYING OUT
			#ifdef HW_SIM
			std::string msg = "Thread Receiver: " + std::to_string(HMLibID) + " --- Entries Processed: " + std::to_string(processed) + "/" + std::to_string(entries) + "\n";
			HMLibObject.printForMe(msg);
			#endif

			for(unsigned int i = 0; i < batchProcessed; i++){
				unsigned int crcAns;
				if(i == 0){
					memcpy(&crcAns,tmpCopy[i]+HMLibUH->metaSize, sizeof(unsigned int));
				}else{
					memcpy(&crcAns,tmpCopy[i], sizeof(unsigned int));
				}
				answers.push_back(crcAns);
			}
		}
		if(processed == entries){
			break;
		}
	}
	std::chrono::steady_clock::time_point totalEnd = std::chrono::steady_clock::now();
	std::chrono::duration<double> duration = totalEnd - totalStart;		
	HMLibUH->overallTime += std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();

	uint64_t timeFinish = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	std::string msg = "Thread Receiver: " + std::to_string(HMLibID) + " --- FINISH: " + std::to_string(timeFinish) + "\n";
	HMLibObject.printForMe(msg);

	threadsReady[HMLibID][1] = false;
	for(unsigned int i = 0; i < MAX_BATCH_SIZE; i++){
		delete[] tmpCopy[i];
	}
}
