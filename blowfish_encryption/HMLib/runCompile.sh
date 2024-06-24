#!/bin/bash
rm -rf v++*.log xcd.log xrc.log src/.run

RD='\033[0;31m'
GN='\033[0;32m'
CY='\033[0;36m'
NC='\033[0m'

COMMAND="$1"
COMMAND2="$2"
FREQ=300
EMU_TYPE=sw_emu
LIB_EMU_TYPE=-lxrt_swemu
VER=2022.2
EN_PROF=""
PLATFORM=xilinx_u250_gen3x16_xdma_4_1_202210_1

source /opt/xilinx/xrt/setup.sh
source /opt/xilinx/tools/Vitis_HLS/$VER/settings64.sh

compile_opencl(){
	LIB_EMU_TYPE=-lxrt_hwemu
	EMU_TYPE=hw_emu
	IS_HW_SIM="-DHW_SIM"

	cd src
	echo -e "${CY}Running Vitis make for $EMU_TYPE... ${NC}"

	if [[ $COMMAND2 == real ]]
	then
		LIB_EMU_TYPE=-lxrt_core
		EMU_TYPE=real
		IS_HW_SIM=""
	fi

	(set -x; g++ -std=c++17 \
	-Wall \
	-O3 \
	-DFPGA_DEVICE -DC_KERNEL $IS_HW_SIM \
	-I/opt/xilinx/xrt/include \
	-I/opt/xilinx \
	-I/opt/xilinx/tools/Vitis_HLS/$VER/include \
	-Isrc \
	-c xcl2.cpp host.cpp helpers.cpp hmlib.cpp) 

	
	if [ $? -ne 0 ]
	then
		echo -e "${RD}OpenCL section failed to compile ${NC}"
		exit 1
	fi

	g++ -o test.$EMU_TYPE.out xcl2.o host.o helpers.o hmlib.o -L/opt/xilinx/xrt/lib -lOpenCL -lpthread -lrt -lstdc++ -luuid $LIB_EMU_TYPE

	if [ $? -ne 0 ]
	then
		echo -e "${RD}OpenCL section failed to link ${NC}"
		exit 1
	fi

	emconfigutil --platform $PLATFORM --od .
	cd ../
}

compile_kernel(){
	PIDS=""
	FAIL=0
	extraCommands=""
	(set -x; rm src/*-$EMU_TYPE.xo src/workload-$EMU_TYPE.xclbin)

	if [[ $EMU_TYPE == hw_emu ]]
	then
		extraCommands="--advanced.param compiler.fsanitize=address,memory"
		extraCommands="${extraCommands} --advanced.param compiler.deadlockDetection=true"
		##extraCommands="${extraCommands} --advanced.param compiler.enableIncrHwEmu=true"
	fi
	

	########## BUILDS MEM HOST MEMORY ACCELERATE KERNEL ##########
	echo -e "${CY}Running Vitis $EMU_TYPE make for HMLIB kernel... ${NC}"

	(set -x; g++ -std=c++17 -w -O3 \
	-I/opt/xilinx/xrt/include \
	-I/opt/xilinx -I/opt/xilinx/tools/Vitis_HLS/$VER/include \
	-Isrc \
	-c src/krnl_memory_controller/hmlib_top.cpp \
	src/krnl_memory_controller/hmlib_kernel.cpp; \
	rm hmlib_top.o hmlib_kernel.o)

	if [ $? -ne 0 ]
	then
		echo -e "${RD}g++ compile check failed ${NC}"
		exit 1
	fi

	(set -x; v++ -c -t $EMU_TYPE \
	--include src \
	--include src/krnl_memory_controller \
	$extraCommands \
	--platform $PLATFORM \
	-s --kernel memAccelerate \
	--kernel_frequency $FREQ \
	-R2 \
	src/krnl_memory_controller/hmlib_top.cpp \
	src/krnl_memory_controller/hmlib_kernel.cpp \
	-o src/workload-mem-$EMU_TYPE.xo) &

	PIDS="$PIDS $!"


	########## BUILDS SIPHASH ACCELERATE KERNEL ##########
	echo -e "${CY}Running Vitis $EMU_TYPE make for Blowfish ACCELERATE kernel... ${NC}"

	(set -x; v++ -c -t $EMU_TYPE \
	--include src \
	$extraCommands \
	--platform $PLATFORM \
	-s --kernel blowfish_HM \
	--kernel_frequency $FREQ \
	-R2 \
	src/blowfish.cpp -o src/workload-blowfish-$EMU_TYPE.xo) &
	PIDS="$PIDS $!"

	for job in $PIDS
	do
		wait $job || let "FAIL+=1"
	done

	if [ "$FAIL" -ne "0" ];
	then
		echo -e "${RD}Build process failed${NC}"
		exit 1
	fi

	########## Return early, we do not want to generate bitstream for ./runCompile hls ##########
	if [[ $EMU_TYPE == hw || $EMU_TYPE == sw_emu ]]
	then
		return
	fi

	########## LINK THE KERNELS TOGETHER ##########
	echo -e "${CY}Running Vitis $EMU_TYPE link... ${NC}"

	v++ -l $EN_PROF -t $EMU_TYPE \
	--config src/k2k.cfg \
	--include src \
	--platform $PLATFORM \
	--kernel_frequency $FREQ \
	-R2 \
	src/workload-mem-$EMU_TYPE.xo src/workload-blowfish-$EMU_TYPE.xo -o src/workload-$EMU_TYPE.xclbin

	if [ $? -ne 0 ]
	then
		echo -e "${RD}Failed to link kernel object ${NC}"
		exit 1
	fi

}

run_program(){
	cd src
	for i in {1..1}
	do
		# time XCL_EMULATION_MODE=$EMU_TYPE ./test.$EMU_TYPE.out ../data_dir/65b_files workload-$EMU_TYPE.xclbin 1
		if [[ $COMMAND2 == real ]]
		then
			./test.real.out ../inputs/ workload-hw.xclbin 0
		else
			time XCL_EMULATION_MODE=$EMU_TYPE ./test.$EMU_TYPE.out ../inputs/ workload-$EMU_TYPE.xclbin 0
		fi
		
		if [ $? -ne 0 ]
		then
			echo -e "${RD}Error in the program ${NC}"
			exit 1
		else
			echo -e "${GN}Test passed ${NC}"
		fi
	done

	cd ../
}

if [[ $COMMAND == compilecl ]]
then
	kill -9 $(pidof xsim)
	kill -9 $(pidof xsimk)

	compile_opencl
fi

if [[ $COMMAND == compilekernel ]]
then
	kill -9 $(pidof xsim)
	kill -9 $(pidof xsimk)
	rm -rf _x
	EMU_TYPE=hw_emu
	EN_PROF="--profile.data all:all:all --profile.exec all:all"

	compile_kernel
fi

if [[ $COMMAND == run ]]
then
	kill -9 $(pidof xsim)
	kill -9 $(pidof xsimk)
	rm -rf _x
	EMU_TYPE=hw_emu
	EN_PROF="$--profile.data all:all:all --profile.exec all:all"

	run_program
fi

if [[ $COMMAND == doall ]]
then
	kill -9 $(pidof xsim)
	kill -9 $(pidof xsimk)
	rm -rf _x
	EN_PROF="--profile.data all:all:all --profile.exec all:all"

	EMU_TYPE=hw_emu	
	compile_opencl
	compile_kernel
	run_program

fi

if [[ $COMMAND == hw ]]
then

	v++ -l -t hw \
	--config src/k2k.cfg \
	--include src \
	--platform $PLATFORM \
	-s --kernel_frequency $FREQ \
	-R1 src/workload-mem-hw.xo src/workload-blowfish-hw.xo -o src/workload-hw.xclbin

	exit 0
fi

if [[ $COMMAND == hls ]]
then
	
	rm -rf FPGArpt/ _x
	mkdir FPGArpt
	EMU_TYPE=hw
	compile_kernel

	echo -e "${CY}Copying Vitis HLS reports for Blowfish ACCELERATE kernel... ${NC}"
	cp _x/workload-blowfish-hw/blowfish_HM/blowfish_HM/solution/syn/report/*.rpt FPGArpt/

	echo -e "${CY}Copying Vitis HLS reports for HMLib kernel... ${NC}"
	cp _x/workload-mem-hw/memAccelerate/memAccelerate/solution/syn/report/*.rpt FPGArpt/

	exit 0
fi
