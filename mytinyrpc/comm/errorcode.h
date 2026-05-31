#pragma once

namespace tinyrpc {

constexpr int ERROR_PARSE_SERVICE_NAME = 100010;  // 解析服务全名失败
constexpr int ERROR_SERVICE_NOT_FOUND = 100008;      // 服务未找到
constexpr int ERROR_METHOD_NOT_FOUND = 100009;       // 方法未找到
constexpr int ERROR_FAILED_DESERIALIZE = 100003;     // 反序列化失败
constexpr int ERROR_FAILED_SERIALIZE = 100004;       // 序列化失败
constexpr int ERROR_RPC_CHANNEL_INVALID_ARGUMENT = 100011; // RPC Channel 参数非法
constexpr int ERROR_RPC_CHANNEL_NETWORK = 100012;          // RPC Channel 网络收发失败
constexpr int ERROR_RPC_MSGREQ_MISMATCH = 100013;          // RPC 响应请求号不匹配
constexpr int ERROR_TCP_CONNECT_FAILED = 100014;           // TCP 连接失败
constexpr int ERROR_TCP_SEND_FAILED = 100015;              // TCP 写入失败
constexpr int ERROR_TCP_RECV_FAILED = 100016;              // TCP 读取失败
constexpr int ERROR_TCP_TIMEOUT = 100017;                  // TCP 同步等待超时

}
