#pragma once

namespace tinyrpc {

// 序列化 / 反序列化错误。
constexpr int ERROR_FAILED_DESERIALIZE = 100003;     // Protobuf 反序列化失败
constexpr int ERROR_FAILED_SERIALIZE = 100004;       // Protobuf 序列化失败

// 服务端 TinyPB Dispatcher 错误。
constexpr int ERROR_SERVICE_NOT_FOUND = 100008;      // 服务未找到
constexpr int ERROR_METHOD_NOT_FOUND = 100009;       // 方法未找到
constexpr int ERROR_PARSE_SERVICE_NAME = 100010;     // 解析服务全名失败

// 客户端 RPC Channel 错误。
constexpr int ERROR_RPC_CHANNEL_INVALID_ARGUMENT = 100011; // RPC Channel 参数非法
constexpr int ERROR_RPC_CHANNEL_NETWORK = 100012;          // RPC Channel 网络收发失败
constexpr int ERROR_RPC_REQID_MISMATCH = 100013;          // RPC 响应请求号不匹配
constexpr int ERROR_RPC_ASYNC_TIMEOUT = 100018;            // 异步 RPC 请求超时
constexpr int ERROR_RPC_ASYNC_CANCELED = 100019;           // 异步 RPC 请求取消

// 同步 TCP 客户端错误。
constexpr int ERROR_TCP_CONNECT_FAILED = 100014;           // TCP 连接失败
constexpr int ERROR_TCP_SEND_FAILED = 100015;              // TCP 写入失败
constexpr int ERROR_TCP_RECV_FAILED = 100016;              // TCP 读取失败
constexpr int ERROR_TCP_TIMEOUT = 100017;                  // TCP 同步等待超时

}
