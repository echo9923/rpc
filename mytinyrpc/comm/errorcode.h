#pragma once

namespace tinyrpc {

constexpr int ERROR_PARSE_SERVICE_NAME = 100010;  // 解析服务全名失败
constexpr int ERROR_SERVICE_NOT_FOUND = 100008;      // 服务未找到
constexpr int ERROR_METHOD_NOT_FOUND = 100009;       // 方法未找到
constexpr int ERROR_FAILED_DESERIALIZE = 100003;     // 反序列化失败
constexpr int ERROR_FAILED_SERIALIZE = 100004;       // 序列化失败

}
