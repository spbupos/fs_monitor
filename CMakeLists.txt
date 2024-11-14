cmake_minimum_required(VERSION 3.6)

project(fs_monitor VERSION 0.1.0 LANGUAGES C)
set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
find_package(KernelHeaders REQUIRED)
add_definitions(-D__KERNEL__ -DMODULE)

add_executable(fs_monitor
        main.c
        service.c
        base64.c
        tracers.c
        header.h
)

target_include_directories(fs_monitor PRIVATE ${KERNELHEADERS_INCLUDE_DIRS})
