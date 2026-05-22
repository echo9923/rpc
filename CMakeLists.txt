cmake_minimum_required(VERSION 3.10)

project(mytinyrpc)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -g -O0")

include_directories(${PROJECT_SOURCE_DIR})

set(SRC
    comm/log.cc
    net/netaddress.cc
    net/tcpconnection.cc
    net/tcpserver.cc
)

add_library(mytinyrpc_obj OBJECT ${SRC})
