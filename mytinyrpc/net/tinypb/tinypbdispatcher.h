#pragma once

#include "net/abstractdispatcher.h"

#include <memory>
#include <string>

namespace tinyrpc {

// TinyPbDispatcher 负责 TinyPB 协议的最小分发逻辑。
// 当前阶段不做真正的服务注册和方法调用，
// 仅解析 serviceFullName，构造协议级假响应并写回连接。
//
// 后续接入服务注册表后，dispatch() 内部会查找对应的
// google::protobuf::Service 并调用 CallMethod()。
class TinyPbDispatcher : public AbstractDispatcher {
 public:
    using Ptr = std::shared_ptr<TinyPbDispatcher>;

    // 处理解码后的 TinyPbStruct 请求，构造响应并写回连接。
    // 当前实现：保留请求的 msgReq、serviceFullName、pbData，
    // 通过 conn->sendProtocolData() 将响应编码后写入输出缓冲区。
    void dispatch(AbstractData *data, TcpConnection *conn) override;

    // 将 "ServiceName.methodName" 形式的完整服务名拆分为两部分。
    // fullName：完整服务名，如 "QueryService.query_name"。
    // serviceName：输出参数，拆分后的服务名部分。
    // methodName：输出参数，拆分后的方法名部分。
    // 返回值：拆分成功返回 true；fullName 为空、不含 '.'、
    // 或拆分后任一部分为空时返回 false。
    bool parseServiceFullName(
        const std::string& fullName,
        std::string& serviceName,
        std::string& methodName
    ) const;
};

}
