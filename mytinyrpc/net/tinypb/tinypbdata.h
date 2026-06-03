#pragma once

#include "net/abstractdata.h"

#include <cstdint>
#include <memory>
#include <string>

namespace tinyrpc {

// TinyPB 协议数据结构，承载单次 RPC 请求/响应的全部字段。
// 编解码由 TinyPbCodec 负责（本任务只定义数据结构，不实现编解码）。
//
// 字段布局（按网络传输顺序）：
//   pkLen | reqIdLen | reqId | serviceNameLen | serviceFullName
//   | errCode | errInfoLen | errInfo | pbData | checkNum
//
// 其中 pkLen 为完整包长度（含自身），checkNum 为校验值。
class TinyPbStruct : public AbstractData {
 public:
    using Ptr = std::shared_ptr<TinyPbStruct>;

    // 完整 TinyPB 包长度（含所有字段 + 起止标记），encode 时计算
    int32_t m_pkLen {0};

    // 请求号长度
    int32_t m_reqIdLen {0};
    // 请求号，用于关联请求与响应
    std::string m_reqId;

    // 服务完整名长度
    int32_t m_serviceNameLen {0};
    // 服务完整名，例如 "QueryService.query_name"
    std::string m_serviceFullName;

    // RPC 错误码，0 表示无错误
    int32_t m_errCode {0};
    // 错误信息长度
    int32_t m_errInfoLen {0};
    // 错误信息字符串
    std::string m_errInfo;

    // 序列化后的 protobuf 业务数据
    std::string m_pbData;

    // 校验字段，encode 时计算；本任务只保留字段，不实现校验算法
    int32_t m_checkNum {-1};
};

}
