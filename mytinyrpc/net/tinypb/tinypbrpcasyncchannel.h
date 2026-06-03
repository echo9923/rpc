#pragma once

#include "net/netaddress.h"
#include "net/iothread.h"
#include "net/tinypb/tinypbdata.h"

#include <google/protobuf/service.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace tinyrpc {

class TimerEvent;

// AsyncCallContext — 异步 RPC 调用上下文。
//
// 当前任务只建立生命周期外壳，因此上下文保存非拥有指针：
// request/response/controller/closure 仍由 Protobuf Stub 调用方持有。
// 后续任务会把上下文放入 pending map 并延长跨事件生命周期。
struct AsyncCallContext {
    std::string m_reqId;
    std::string m_methodFullName;
    TinyPbStruct m_tinyRequest;
    google::protobuf::RpcController *m_controller {nullptr};
    const google::protobuf::Message *m_request {nullptr};
    google::protobuf::Message *m_response {nullptr};
    google::protobuf::Closure *m_done {nullptr};
    std::shared_ptr<TimerEvent> m_timeoutEvent;
};

// TinyPbRpcAsyncChannel 是 Protobuf Stub 的异步 RPC 外壳。
//
// 当前已支持 reqId -> AsyncCallContext pending 表和 response 匹配。
// 默认仍临时复用同步 TinyPbRpcChannel 完成网络请求；禁用同步 fallback 后，
// CallMethod() 只注册 pending，等待 handleTinyPbResponse() 完成上下文。
// 不包含并发异步 IO、连接池、异步超时和取消。
class TinyPbRpcAsyncChannel : public google::protobuf::RpcChannel {
 public:
    explicit TinyPbRpcAsyncChannel(const IPAddress& peerAddr);
    ~TinyPbRpcAsyncChannel();

    void CallMethod(
        const google::protobuf::MethodDescriptor *method,
        google::protobuf::RpcController *controller,
        const google::protobuf::Message *request,
        google::protobuf::Message *response,
        google::protobuf::Closure *done) override;

    void setReqIdGenerator(std::function<std::string()> generator);
    void setSyncFallbackEnabled(bool enabled);

    std::shared_ptr<AsyncCallContext> getLastContext() const;
    size_t getPendingCount() const;
    bool hasPending(const std::string& reqId) const;
    bool handleTinyPbResponse(const TinyPbStruct& response);
    bool cancel(const std::string& reqId);
    void stop();
    bool isIOThreadStarted() const;
    std::thread::id getIOThreadId() const;

 private:
    std::string genReqId() const;
    void registerPending(const std::shared_ptr<AsyncCallContext>& context);
    void registerTimeoutEvent(const std::shared_ptr<AsyncCallContext>& context);
    std::shared_ptr<AsyncCallContext> takePending(const std::string& reqId);
    void cancelTimeoutEvent(const std::shared_ptr<AsyncCallContext>& context);
    void handleTimeout(const std::string& reqId);
    void finishContext(const std::shared_ptr<AsyncCallContext>& context);
    bool finishPendingWithError(
        const std::string& reqId,
        int errorCode,
        const std::string& errorInfo);
    static int getControllerTimeout(google::protobuf::RpcController *controller);
    static void setControllerError(
        google::protobuf::RpcController *controller,
        int errorCode,
        const std::string& errorInfo);

    IPAddress m_peerAddr;
    std::function<std::string()> m_reqIdGenerator;
    std::shared_ptr<AsyncCallContext> m_lastContext;
    std::unordered_map<std::string, std::shared_ptr<AsyncCallContext>> m_pendingContexts;
    mutable std::mutex m_pendingMutex;
    std::unique_ptr<IOThread> m_ioThread;
    bool m_syncFallbackEnabled {true};
};

}  // namespace tinyrpc
