cmake_minimum_required(VERSION 3.20)
project(FAT_PROJEKT C)

set(CMAKE_C_STANDARD 99)

add_executable(FAT_PROJEKT main.c file_reader.c file_reader.h)
target_link_libraries(FAT_PROJEKT m)
