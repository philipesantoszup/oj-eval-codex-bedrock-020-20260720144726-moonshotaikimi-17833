cmake_minimum_required(VERSION 3.10)
project(buddy C)

set(CMAKE_C_STANDARD 11)
# Don't use strict warnings for main.c since we can't modify it
set(CMAKE_C_FLAGS "-w")

add_executable(code main.c buddy.c)
