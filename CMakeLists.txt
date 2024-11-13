cmake_minimum_required(VERSION 3.6)

project(fs_journaler VERSION 0.1.0 LANGUAGES C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
find_package(KernelHeaders REQUIRED)
add_definitions(-D__KERNEL__ -DMODULE)

add_executable(fs_journaler
        main.c
        service.c
        header.h
)

target_include_directories(fs_journaler PRIVATE ${KERNELHEADERS_INCLUDE_DIRS})
