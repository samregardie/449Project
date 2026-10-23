cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(stm32_front)

# N2, the steering zone
target_compile_definitions(app PRIVATE NODE_ID=2)

set(COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../stm32-common)

target_sources(app PRIVATE src/main.c src/servo.c ${COMMON}/src/canbus.c ${COMMON}/src/relay.c
	${COMMON}/src/ctrl.c ${COMMON}/src/errorstate.c ${COMMON}/src/blinker.c)
target_include_directories(app PRIVATE include ${COMMON}/include)
