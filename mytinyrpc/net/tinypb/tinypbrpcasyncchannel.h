#pragma once

#include "net/asyncclientsession.h"
#include "net/netaddress.h"
#include "net/iothread.h"
#include "net/tinypb/tinypbdata.h"

#include <google/protobuf/service.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace tinyrpc {

class TimerTask;

struct TimeoutEntry {
    std::mutex mutex;
    std::condition_variable cv;
    bool fired = false;
    bool canceled = false;

    void cancel() {
        std::lock_guard<std::mutex> lock(mutex);
        canceled = true;
        cv.notify_all();
    }

    bool waitFor(std::chrono::milliseconds ms) {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait_for(lock, ms, [this] { return canceled; });
        if (canceled) return false;
        fired = true;
        return true;
    }
};

struct AsyncCallContext {
    std::string m_reqId;
    std::string m_methodFullName;
    TinyPbStruct m_tinyRequest;
    google::protobuf::RpcController *m_controller {nullptr};
    const google::protobuf::Message *m_request {nullptr};
    google::protobuf::Message *m_response {nullptr};
    google::protobuf::Closure *m_done {nullptr};
    std::shared_ptr<TimerTask> m_timeoutTask;
    std::shared_ptr<TimeoutEntry> m_timeoutEntry;
    std::atomic<bool> m_timedOut {false};
};

// TinyPbRpcAsyncChannel 是 Protobuf Stub 的异步 RPC 外壳。
//
// 当前已支持 reqId -> AsyncCallContext pending 表、response 匹配、
// 超时和取消。内部持有一个 IOThread 和一个长生命周期 AsyncClientSession。
// 默认路径只在 IOThread 上投递 connect/send，响应由 session 的 EPOLLIN 读回调完成。
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
    void sendContextOnIOThread(const std::shared_ptr<AsyncCallContext>& context);
    bool finishPendingWithError(
        const std::string& reqId,
        int errorCode,
        const std::string& errorInfo);
    void failAllPending(int errorCode, const std::string& errorInfo);
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
