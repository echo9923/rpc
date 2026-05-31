#pragma once

namespace tinyrpc {

constexpr int ERROR_PARSE_SERVICE_NAME = 100010;  // 解析服务全名失败
constexpr int ERROR_SERVICE_NOT_FOUND = 100008;      // 服务未找到
constexpr int ERROR_METHOD_NOT_FOUND = 100009;       // 方法未找到
constexpr int ERROR_FAILED_DESERIALIZE = 100003;     // 反序列化失败
constexpr int ERROR_FAILED_SERIALIZE = 100004;       // 序列化失败
constexpr int ERROR_RPC_CHANNEL_INVALID_ARGUMENT = 100011; // RPC Channel 参数非法
constexpr int ERROR_RPC_CHANNEL_NETWORK = 100012;          // RPC Channel 网络收发失败

}
