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

find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# 生成 Protobuf 代码：test_tinypb_server.proto → test_tinypb_server.pb.h + test_tinypb_server.pb.cc
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS testcases/test_tinypb_server.proto)

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
    mytinyrpc/net/tcpclient.cc
    mytinyrpc/net/tcpserver.cc
    mytinyrpc/net/tinypb/tinypbcodec.cc
    mytinyrpc/net/tinypb/tinypbdispatcher.cc
    mytinyrpc/net/tinypb/tinypbrpcchannel.cc
    mytinyrpc/net/tinypb/tinypbrpccontroller.cc
    ${PROTO_SRCS}
)

add_library(mytinyrpc_obj OBJECT ${SRC})

add_executable(test_tcp_echo_server
    testcases/testtcpchoserver.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)
target_link_libraries(test_tcp_echo_server ${Protobuf_LIBRARIES})

add_executable(test_fdevent
    testcases/test_fdevent.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)
target_link_libraries(test_fdevent ${Protobuf_LIBRARIES})

add_executable(test_reactor
    testcases/test_reactor.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)
target_link_libraries(test_reactor ${Protobuf_LIBRARIES})

add_executable(test_abstract_codec
    testcases/test_abstract_codec.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)
target_link_libraries(test_abstract_codec ${GTEST_LIBRARIES} pthread ${Protobuf_LIBRARIES})

add_executable(test_tinypb_data
    testcases/test_tinypb_data.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)
target_link_libraries(test_tinypb_data ${GTEST_LIBRARIES} pthread ${Protobuf_LIBRARIES})

add_executable(test_tinypb_codec
    testcases/test_tinypb_codec.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)
target_link_libraries(test_tinypb_codec ${GTEST_LIBRARIES} pthread ${Protobuf_LIBRARIES})

add_executable(test_tcp_buffer
    testcases/test_tcp_buffer.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)
target_link_libraries(test_tcp_buffer ${Protobuf_LIBRARIES})

add_executable(test_coroutine
    testcases/test_coroutine.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)
target_link_libraries(test_coroutine ${Protobuf_LIBRARIES})

add_executable(test_hook
    testcases/test_hook.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)
target_link_libraries(test_hook ${GTEST_LIBRARIES} pthread ${Protobuf_LIBRARIES})

add_executable(test_connection_codec
    testcases/test_connection_codec.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)
target_link_libraries(test_connection_codec ${GTEST_LIBRARIES} pthread ${Protobuf_LIBRARIES})

add_executable(test_tinypb_dispatcher
    testcases/test_tinypb_dispatcher.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)
target_link_libraries(test_tinypb_dispatcher ${GTEST_LIBRARIES} pthread ${Protobuf_LIBRARIES})

add_executable(test_protobuf_service
    testcases/test_protobuf_service.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)
target_link_libraries(test_protobuf_service ${GTEST_LIBRARIES} pthread ${Protobuf_LIBRARIES})

add_executable(test_tcp_client
    testcases/test_tcp_client.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)
target_link_libraries(test_tcp_client ${GTEST_LIBRARIES} pthread ${Protobuf_LIBRARIES})

add_executable(test_tinypb_rpc_channel
    testcases/test_tinypb_rpc_channel.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)
target_link_libraries(test_tinypb_rpc_channel ${GTEST_LIBRARIES} pthread ${Protobuf_LIBRARIES})

add_executable(test_tinypb_server_client
    testcases/test_tinypb_server_client.cc
    $<TARGET_OBJECTS:mytinyrpc_obj>
)
target_link_libraries(test_tinypb_server_client pthread ${Protobuf_LIBRARIES})
