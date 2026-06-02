#pragma once

#include "net/netaddress.h"
#include "net/tinypb/tinypbdata.h"

#include <google/protobuf/service.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace tinyrpc {

// AsyncCallContext — 异步 RPC 调用上下文。
//
// 当前任务只建立生命周期外壳，因此上下文保存非拥有指针：
// request/response/controller/closure 仍由 Protobuf Stub 调用方持有。
// 后续任务会把上下文放入 pending map 并延长跨事件生命周期。
struct AsyncCallContext {
    std::string msgReq;
    std::string methodFullName;
    TinyPbStruct tinyRequest;
    google::protobuf::RpcController *controller {nullptr};
    const google::protobuf::Message *request {nullptr};
    google::protobuf::Message *response {nullptr};
    google::protobuf::Closure *done {nullptr};
};

// TinyPbRpcAsyncChannel 是 Protobuf Stub 的异步 RPC 外壳。
//
// 当前已支持 msgReq -> AsyncCallContext pending 表和 response 匹配。
// 默认仍临时复用同步 TinyPbRpcChannel 完成网络请求；禁用同步 fallback 后，
// CallMethod() 只注册 pending，等待 handleTinyPbResponse() 完成上下文。
// 不包含并发异步 IO、连接池、异步超时和取消。
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
    void setSyncFallbackEnabled(bool enabled);

    std::shared_ptr<AsyncCallContext> getLastContext() const;
    size_t getPendingCount() const;
    bool hasPending(const std::string& msgReq) const;
    bool handleTinyPbResponse(const TinyPbStruct& response);

 private:
    std::string genMsgReq() const;
    void registerPending(const std::shared_ptr<AsyncCallContext>& context);
    void removePending(const std::string& msgReq);
    void finishContext(const std::shared_ptr<AsyncCallContext>& context);
    static void setControllerError(
        google::protobuf::RpcController *controller,
        int errorCode,
        const std::string& errorInfo);

    IPAddress m_peerAddr;
    std::function<std::string()> m_msgReqGenerator;
    std::shared_ptr<AsyncCallContext> m_lastContext;
    std::unordered_map<std::string, std::shared_ptr<AsyncCallContext>> m_pendingContexts;
    bool m_syncFallbackEnabled {true};
};

}  // namespace tinyrpc
