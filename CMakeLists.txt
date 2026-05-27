cmake_minimum_required(VERSION 3.10)

project(mytinyrpc)

enable_language(ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -g -O0")

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/mytinyrpc)

find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

set(SRC
    mytinyrpc/comm/log.cc
    mytinyrpc/coroutine/coctx_swap.S
    mytinyrpc/coroutine/coroutine.cc
    mytinyrpc/coroutine/coroutine_hook.cc
    mytinyrpc/net/fdevent.cc
    mytinyrpc/net/fdutil.cc
    mytinyrpc/net/netaddress.cc
    mytinyrpc/net/reactor.cc
    mytinyrpc/net/tcpbuffer.cc
    mytinyrpc/net/tcpconnection.cc
    mytinyrpc/net/tcpserver.cc
)

add_library(mytinyrpc_obj OBJECT ${SRC})

add_executable(test_tcp_echo_server
    testcases/testtcpchoserver.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)

add_executable(test_fdevent
    testcases/test_fdevent.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)

add_executable(test_reactor
    testcases/test_reactor.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)

add_executable(test_abstract_codec
    testcases/test_abstract_codec.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)
target_link_libraries(test_abstract_codec ${GTEST_LIBRARIES} pthread)

add_executable(test_tcp_buffer
    testcases/test_tcp_buffer.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)

add_executable(test_coroutine
    testcases/test_coroutine.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)

add_executable(test_hook
    testcases/test_hook.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)
target_link_libraries(test_hook ${GTEST_LIBRARIES} pthread)
