cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(stm32_rear)

# N3, the drivetrain zone
target_compile_definitions(app PRIVATE NODE_ID=3)

set(COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../stm32-common)

# Common files (blinker, errorstate) are added here once they're ported to CAN
target_sources(app PRIVATE src/main.c src/drive.c src/motor.c src/encoder.c ${COMMON}/src/canbus.c)
target_include_directories(app PRIVATE include ${COMMON}/include)
