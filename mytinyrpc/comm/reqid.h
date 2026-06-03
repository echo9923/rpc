#pragma once

#include <string>

namespace tinyrpc {

// ReqIdUtil 负责生成框架层请求号。
// 请求号用于关联 TinyPB request/response，当前同步客户端只允许单 in-flight，
// 但仍需要 reqId 校验，避免把错误响应解析成业务结果。
class ReqIdUtil {
 public:
    // 生成非空、进程内递增且不重复的请求号。
    static std::string genReqId();
};

}
