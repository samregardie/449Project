cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(stm32_rear)

# N3, the drivetrain zone
target_compile_definitions(app PRIVATE NODE_ID=3)

# LEADER=random etc. for checkoff: west build-<front|rear> -p -- -DRAFT_DEFAULT_LEADER=0 (see raft_config.h)
foreach(opt RAFT_DEFAULT_LEADER RAFT_SEED)
	if(DEFINED ${opt})
		target_compile_definitions(app PRIVATE ${opt}=${${opt}})
	endif()
endforeach()

set(COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../stm32-common)

target_sources(app PRIVATE src/main.c src/drive.c src/motor.c src/encoder.c ${COMMON}/src/canbus.c ${COMMON}/src/relay.c
	${COMMON}/src/ctrl.c ${COMMON}/src/errorstate.c ${COMMON}/src/blinker.c
	${COMMON}/src/globaltime.c ${COMMON}/src/raftlog.c ${COMMON}/src/raftnode.c ${COMMON}/src/raft.c)
target_include_directories(app PRIVATE include ${COMMON}/include)
