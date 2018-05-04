cmake_minimum_required(VERSION 3.8)
project(cio)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(withTests "Build Unit Tests" OFF)

include(CheckIncludeFiles)
check_include_files("sys/epoll.h" HAVE_EPOLL_H)
check_include_files("sys/poll.h" HAVE_POLL_H)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
    ${CMAKE_CURRENT_SOURCE_DIR}/cio/src/config.h)

add_subdirectory(cio)
if(withTests)
    add_subdirectory(test)
endif(withTests)
