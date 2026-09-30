cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lab2)

target_sources(app PRIVATE src/main.c src/blinker.c src/motor.c src/encoder.c src/drive.c src/picom.c)
target_include_directories(app PRIVATE include)
