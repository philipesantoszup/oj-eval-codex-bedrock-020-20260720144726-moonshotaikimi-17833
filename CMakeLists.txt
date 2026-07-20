cmake_minimum_required(VERSION 3.10)
project(buddy C)

set(CMAKE_C_STANDARD 11)

# Override default compiler flags to suppress all warnings
set(CMAKE_C_FLAGS "")
set(CMAKE_C_FLAGS_DEBUG "")
set(CMAKE_C_FLAGS_RELEASE "")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "")
set(CMAKE_C_FLAGS_MINSIZEREL "")

# Set flags to suppress warnings
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_C_FLAGS "-w -fpermissive")
endif()

add_executable(code main.c buddy.c)

# Disable additional warnings for the target
target_compile_options(code PRIVATE -w)
