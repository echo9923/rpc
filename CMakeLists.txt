cmake_minimum_required(VERSION 3.10)

project(mytinyrpc)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -g -O0")

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/mytinyrpc)

set(SRC
    mytinyrpc/comm/log.cc
    mytinyrpc/net/fd_util.cc
    mytinyrpc/net/netaddress.cc
    mytinyrpc/net/tcpconnection.cc
    mytinyrpc/net/tcpserver.cc
)

add_library(mytinyrpc_obj OBJECT ${SRC})

add_executable(test_tcp_echo_server
    testcases/testtcpchoserver.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)
