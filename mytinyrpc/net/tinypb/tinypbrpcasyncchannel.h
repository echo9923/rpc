#pragma once

#include "net/netaddress.h"

#include <google/protobuf/service.h>

#include <functional>
#include <memory>
#include <string>

namespace tinyrpc {

// AsyncCallContext — 异步 RPC 调用上下文。
//
// 当前任务只建立生命周期外壳，因此上下文保存非拥有指针：
// request/response/controller/closure 仍由 Protobuf Stub 调用方持有。
// 后续任务会把上下文放入 pending map 并延长跨事件生命周期。
struct AsyncCallContext {
    std::string msgReq;
    std::string methodFullName;
    google::protobuf::RpcController *controller {nullptr};
    const google::protobuf::Message *request {nullptr};
    google::protobuf::Message *response {nullptr};
    google::protobuf::Closure *done {nullptr};
};

// TinyPbRpcAsyncChannel 是 Protobuf Stub 的异步 RPC 外壳。
//
// 任务七十四仅解决接口和生命周期观察点：保存一次调用上下文，
// 内部临时复用同步 TinyPbRpcChannel 完成网络请求，并保证成功或失败都会执行 done。
// 不包含 pending map、并发异步 IO、响应乱序匹配和超时取消。
class TinyPbRpcAsyncChannel : public google::protobuf::RpcChannel {
 public:
    explicit TinyPbRpcAsyncChannel(const IPAddress& peerAddr);

    void CallMethod(
        const google::protobuf::MethodDescriptor *method,
        google::protobuf::RpcController *controller,
        const google::protobuf::Message *request,
        google::protobuf::Message *response,
        google::protobuf::Closure *done) override;

    void setMsgReqGenerator(std::function<std::string()> generator);

    std::shared_ptr<AsyncCallContext> getLastContext() const;

 private:
    std::string genMsgReq() const;
    static void setControllerError(
        google::protobuf::RpcController *controller,
        int errorCode,
        const std::string& errorInfo);

    IPAddress m_peerAddr;
    std::function<std::string()> m_msgReqGenerator;
    std::shared_ptr<AsyncCallContext> m_lastContext;
};

}  // namespace tinyrpc
