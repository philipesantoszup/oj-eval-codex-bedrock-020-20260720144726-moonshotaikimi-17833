cmake_minimum_required(VERSION 3.10)
project(buddy C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "-O2 -w")
set(CMAKE_C_FLAGS_DEBUG "")
set(CMAKE_C_FLAGS_RELEASE "-O2 -w")

add_executable(code main.c buddy.c)

target_compile_options(code PRIVATE -O2 -w)
