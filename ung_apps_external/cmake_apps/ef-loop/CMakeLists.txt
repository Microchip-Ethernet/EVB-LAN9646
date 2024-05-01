# Copyright (c) 2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: (GPL-2.0)

project (ef-loop C)

cmake_minimum_required(VERSION 2.6)

find_package(Threads REQUIRED)


## libev ################################################
find_path(LibEV_INCLUDE_DIR ev.h
    /usr/include
    /usr/local/include
)

find_library(LibEV_LIBRARY NAMES ev)

if (NOT (LibEV_INCLUDE_DIR AND LibEV_LIBRARY))
    message(FATAL_ERROR "Could not find libev.")
endif ()

## ef-loop (this project) ####################################
add_definitions(-Wall)

include_directories(${LibEV_INCLUDE_DIR})

add_executable(ef-loop ef-loop.c)

target_link_libraries(ef-loop ${LibEV_LIBRARY})
target_link_libraries(ef-loop ${CMAKE_THREAD_LIBS_INIT} )

install(TARGETS ef-loop RUNTIME DESTINATION bin)

