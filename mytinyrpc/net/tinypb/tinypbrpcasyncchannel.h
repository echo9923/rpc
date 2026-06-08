#pragma once

#include "net/asyncclientsession.h"
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

class TimerTask;

struct AsyncCallContext {
    std::string m_reqId;
    std::string m_methodFullName;
    TinyPbStruct m_tinyRequest;
    google::protobuf::RpcController *m_controller {nullptr};
    const google::protobuf::Message *m_request {nullptr};
    google::protobuf::Message *m_response {nullptr};
    google::protobuf::Closure *m_done {nullptr};
    std::shared_ptr<TimerTask> m_timeoutTask;
};

// TinyPbRpcAsyncChannel 是 Protobuf Stub 的异步 RPC 外壳。
//
// 当前已支持 reqId -> AsyncCallContext pending 表、response 匹配、
// 超时和取消。内部持有一个 IOThread 和一个长生命周期 AsyncClientSession。
// 默认仍通过 IOThread 同步执行网络请求（sync fallback），但连接由 session 管理，
// 不再每次创建临时 TcpClient。
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
    void registerTimeoutTask(const std::shared_ptr<AsyncCallContext>& context);
    std::shared_ptr<AsyncCallContext> takePending(const std::string& reqId);
    void cancelTimeoutTask(const std::shared_ptr<AsyncCallContext>& context);
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
    std::unique_ptr<AsyncClientSession> m_session;
    bool m_syncFallbackEnabled {true};
};

}  // namespace tinyrpc
