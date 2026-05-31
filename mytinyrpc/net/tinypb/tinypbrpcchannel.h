#pragma once

#include "net/netaddress.h"

#include <google/protobuf/service.h>

#include <functional>
#include <string>

namespace tinyrpc {

// TinyPbRpcChannel 是 Protobuf Stub 到 TinyPB/TcpClient 的同步适配层。
// Stub 只会调用 RpcChannel::CallMethod()，不会感知 TCP 连接和 TinyPB 帧格式。
//
// 当前阶段只支持一问一答的同步 RPC：
//   1. 把 Protobuf request 序列化到 TinyPbStruct::m_pbData
//   2. 调用 TcpClient::sendAndRecvTinyPb()
//   3. 校验 response msgReq 与 request msgReq 是否一致
//   4. 不匹配时直接设置框架错误，不缓存乱序 response
//   5. 把 TinyPB response 的 m_pbData 反序列化到 Protobuf response
//
// 同步 Channel 每次 CallMethod 只有一个 in-flight request。
// 不包含：连接池、并发请求、乱序响应缓存、异步 pending map。
class TinyPbRpcChannel : public google::protobuf::RpcChannel {
 public:
    explicit TinyPbRpcChannel(const IPAddress& peerAddr);

    // [第三方 API] Protobuf 生成的 Stub 会调用 CallMethod()。
    // method 描述要调用的 RPC 方法，request/response 是业务消息对象，
    // controller 承载框架层错误，done 是调用完成回调。
    void CallMethod(
        const google::protobuf::MethodDescriptor *method,
        google::protobuf::RpcController *controller,
        const google::protobuf::Message *request,
        google::protobuf::Message *response,
        google::protobuf::Closure *done) override;

    // 设置请求号生成器，仅用于测试稳定断言；生产路径使用默认生成器。
    void setMsgReqGenerator(std::function<std::string()> generator);

 private:
    std::string genMsgReq() const;
    static void setControllerError(
        google::protobuf::RpcController *controller,
        int errorCode,
        const std::string& errorInfo);

    IPAddress m_peerAddr;
    std::function<std::string()> m_msgReqGenerator;
};

}
