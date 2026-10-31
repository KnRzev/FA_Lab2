cmake_minimum_required(VERSION 3.16)
project(FA_Lab2 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

foreach(number IN ITEMS 01 02 03 04 05 06 07 08 09 10)
    add_executable(task${number} task${number}.c)
    if(UNIX)
        target_link_libraries(task${number} PRIVATE m)
    endif()
endforeach()
