cmake_minimum_required(VERSION 3.10)
project(buddy C)

set(CMAKE_C_STANDARD 11)

# Override any environment CFLAGS
unset(ENV{CFLAGS})
set(CMAKE_C_FLAGS "")
set(CMAKE_C_FLAGS_RELEASE "")
set(CMAKE_C_FLAGS_DEBUG "")

add_executable(code main.c buddy.c)

# Set compile options on target, these can't be overridden
# We need to disable the specific warning that causes issues
target_compile_options(code PRIVATE
    $<$<NOT:$<CONFIG:Debug>>:-O2>
    -Wno-int-conversion
)
