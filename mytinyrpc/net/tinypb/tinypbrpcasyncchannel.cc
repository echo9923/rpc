#include "net/tinypb/tinypbrpcasyncchannel.h"

#include "comm/errorcode.h"
#include "comm/reqid.h"
#include "comm/runtime.h"
#include "net/timer.h"
#include "net/tinypb/tinypbrpccontroller.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include <string>
#include <utility>

namespace tinyrpc {

namespace {

std::string splitServiceName(const std::string& methodFullName)
{
    auto pos = methodFullName.rfind('.');
    if (pos == std::string::npos) {
        return methodFullName;
    }
    return methodFullName.substr(0, pos);
}

std::string splitMethodName(const std::string& methodFullName)
{
    auto pos = methodFullName.rfind('.');
    if (pos == std::string::npos) {
        return "";
    }
    return methodFullName.substr(pos + 1);
}

class RequestContextGuard {
 public:
    explicit RequestContextGuard(const std::shared_ptr<AsyncCallContext>& context)
    {
        if (context == nullptr) {
            return;
        }
        getRuntime().setCurrentRequestContext(
            context->m_reqId,
            context->m_interfaceName,
            context->m_methodName,
            "client",
            context->m_peerAddr,
            ProtocolType::TinyPb
        );
        m_enabled = true;
    }

    ~RequestContextGuard()
    {
        if (m_enabled) {
            getRuntime().clearCurrentRequestContext();
        }
    }

 private:
    bool m_enabled {false};
};

}  // namespace

TinyPbRpcAsyncChannel::TinyPbRpcAsyncChannel(const IPAddress& peerAddr)
    : m_peerAddr(peerAddr),
      m_ioThread(std::make_unique<IOThread>()),
      m_session(std::make_unique<AsyncClientSession>(peerAddr))
{
}

TinyPbRpcAsyncChannel::~TinyPbRpcAsyncChannel()
{
    stop();
}

void TinyPbRpcAsyncChannel::CallMethod(
    const google::protobuf::MethodDescriptor *method,
    google::protobuf::RpcController *controller,
    const google::protobuf::Message *request,
    google::protobuf::Message *response,
    google::protobuf::Closure *done)
{
    auto finish = [done]() {
        if (done != nullptr) {
            done->Run();
        }
    };

    auto context = std::make_shared<AsyncCallContext>();
    context->m_controller = controller;
    context->m_request = request;
    context->m_response = response;
    context->m_done = done;
    if (method != nullptr) {
        context->m_methodFullName = method->full_name();
        context->m_interfaceName = splitServiceName(context->m_methodFullName);
        context->m_methodName = splitMethodName(context->m_methodFullName);
    }
    context->m_peerAddr = m_peerAddr.toString();
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_lastContext = context;
    }

    if (method == nullptr || request == nullptr || response == nullptr) {
        setControllerError(
            controller,
            ERROR_RPC_CHANNEL_INVALID_ARGUMENT,
            "TinyPbRpcAsyncChannel CallMethod argument is null");
        finish();
        return;
    }

    auto *tinyController = dynamic_cast<TinyPbRpcController *>(controller);
    if (tinyController != nullptr && !tinyController->getReqId().empty()) {
        context->m_reqId = tinyController->getReqId();
    } else {
        context->m_reqId = genReqId();
        if (tinyController != nullptr) {
            tinyController->setReqId(context->m_reqId);
        }
    }

    RequestContextGuard contextGuard(context);
    context->m_tinyRequest.m_reqId = context->m_reqId;
    context->m_tinyRequest.m_serviceFullName = context->m_methodFullName;
    // [第三方 API] SerializeToString 把业务 request 编码成 Protobuf 二进制串，
    // 该 payload 会作为 TinyPB request 的 pbData 进入后续 pending 发送路径。
    if (!request->SerializeToString(&context->m_tinyRequest.m_pbData)) {
        setControllerError(controller, ERROR_FAILED_SERIALIZE, "failed to serialize async request pbData");
        finish();
        return;
    }

    if (tinyController != nullptr) {
        tinyController->setCancelCallback([this, reqId = context->m_reqId]() {
            cancel(reqId);
        });
    }

    registerPending(context);
    registerTimeoutTask(context);

    if (tinyController != nullptr && tinyController->IsCanceled()) {
        cancel(context->m_reqId);
        return;
    }

    if (!m_syncFallbackEnabled) {
        return;
    }

    m_ioThread->addTask([this, context]() {
        sendContextOnIOThread(context);
    });
}

void TinyPbRpcAsyncChannel::setReqIdGenerator(std::function<std::string()> generator)
{
    m_reqIdGenerator = std::move(generator);
}

void TinyPbRpcAsyncChannel::setSyncFallbackEnabled(bool enabled)
{
    m_syncFallbackEnabled = enabled;
}

std::shared_ptr<AsyncCallContext> TinyPbRpcAsyncChannel::getLastContext() const
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    return m_lastContext;
}

size_t TinyPbRpcAsyncChannel::getPendingCount() const
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    return m_pendingContexts.size();
}

bool TinyPbRpcAsyncChannel::hasPending(const std::string& reqId) const
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    return m_pendingContexts.find(reqId) != m_pendingContexts.end();
}

bool TinyPbRpcAsyncChannel::handleTinyPbResponse(const TinyPbStruct& response)
{
    auto context = takePending(response.m_reqId);
    if (context == nullptr) {
        return false;
    }
    RequestContextGuard contextGuard(context);

    if (response.m_errCode != 0) {
        setControllerError(context->m_controller, response.m_errCode, response.m_errInfo);
        finishContext(context);
        return true;
    }

    // [第三方 API] ParseFromString 把 TinyPB response 的业务 payload
    // 反序列化到 Stub 调用方传入的 response 对象。
    if (context->m_response == nullptr || !context->m_response->ParseFromString(response.m_pbData)) {
        setControllerError(
            context->m_controller,
            ERROR_FAILED_DESERIALIZE,
            "failed to deserialize async response pbData");
        finishContext(context);
        return true;
    }

    finishContext(context);
    return true;
}

bool TinyPbRpcAsyncChannel::cancel(const std::string& reqId)
{
    auto context = takePending(reqId);
    if (context == nullptr) {
        return false;
    }
    RequestContextGuard contextGuard(context);

    auto *tinyController = dynamic_cast<TinyPbRpcController *>(context->m_controller);
    if (tinyController != nullptr) {
        tinyController->StartCancel();
    }
    setControllerError(
        context->m_controller,
        ERROR_RPC_ASYNC_CANCELED,
        "async rpc request canceled, reqId = " + reqId);
    finishContext(context);
    return true;
}

void TinyPbRpcAsyncChannel::stop()
{
    if (m_session != nullptr) {
        m_session->disconnect();
    }
    if (m_ioThread != nullptr) {
        m_ioThread->stop();
    }
    failAllPending(ERROR_RPC_CHANNEL_NETWORK, "async channel stopped");
}

void TinyPbRpcAsyncChannel::failAllPending(int errorCode, const std::string& errorInfo)
{
    std::vector<std::shared_ptr<AsyncCallContext>> contexts;
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        for (auto& pair : m_pendingContexts) {
            contexts.push_back(pair.second);
        }
        m_pendingContexts.clear();
    }
    for (auto& ctx : contexts) {
        if (ctx == nullptr) continue;
        RequestContextGuard contextGuard(ctx);
        if (ctx->m_timeoutEntry != nullptr) {
            ctx->m_timeoutEntry->cancel();
        }
        if (ctx->m_timeoutTask != nullptr) {
            ctx->m_timeoutTask->cancel();
        }
        if (ctx->m_controller != nullptr) {
            auto *tc = dynamic_cast<TinyPbRpcController *>(ctx->m_controller);
            if (tc != nullptr) {
                tc->setError(errorCode, errorInfo);
            } else {
                ctx->m_controller->SetFailed(errorInfo);
            }
        }
        if (ctx->m_done != nullptr) {
            ctx->m_done->Run();
        }
    }
}

bool TinyPbRpcAsyncChannel::isIOThreadStarted() const
{
    return m_ioThread != nullptr && m_ioThread->isStarted();
}

std::thread::id TinyPbRpcAsyncChannel::getIOThreadId() const
{
    if (m_ioThread == nullptr) {
        return std::thread::id();
    }
    return m_ioThread->getThreadId();
}

std::string TinyPbRpcAsyncChannel::genReqId() const
{
    if (m_reqIdGenerator != nullptr) {
        return m_reqIdGenerator();
    }

    return ReqIdUtil::genReqId();
}

void TinyPbRpcAsyncChannel::registerPending(const std::shared_ptr<AsyncCallContext>& context)
{
    if (context == nullptr || context->m_reqId.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingContexts[context->m_reqId] = context;
}

void TinyPbRpcAsyncChannel::registerTimeoutTask(const std::shared_ptr<AsyncCallContext>& context)
{
    int timeoutMs = getControllerTimeout(context == nullptr ? nullptr : context->m_controller);
    if (context == nullptr || context->m_reqId.empty() || timeoutMs <= 0) {
        return;
    }

    auto entry = std::make_shared<TimeoutEntry>();
    context->m_timeoutEntry = entry;
    std::string reqId = context->m_reqId;
    auto ctx = context;

    std::thread([this, entry, reqId, ctx, timeoutMs]() {
        if (entry->waitFor(std::chrono::milliseconds(timeoutMs))) {
            ctx->m_timedOut.store(true);
            // Shutdown the session socket to interrupt the blocking poll in recvResponse.
            if (m_session != nullptr && m_session->isConnected()) {
                m_session->shutdownSocket();
            }
            handleTimeout(reqId);
        }
    }).detach();
}

std::shared_ptr<AsyncCallContext> TinyPbRpcAsyncChannel::takePending(const std::string& reqId)
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    auto iter = m_pendingContexts.find(reqId);
    if (iter == m_pendingContexts.end()) {
        return nullptr;
    }

    auto context = iter->second;
    m_pendingContexts.erase(iter);
    cancelTimeoutTask(context);
    return context;
}

void TinyPbRpcAsyncChannel::cancelTimeoutTask(const std::shared_ptr<AsyncCallContext>& context)
{
    if (context != nullptr && context->m_timeoutEntry != nullptr) {
        context->m_timeoutEntry->cancel();
    }
    if (context != nullptr && context->m_timeoutTask != nullptr) {
        context->m_timeoutTask->cancel();
    }
}

void TinyPbRpcAsyncChannel::handleTimeout(const std::string& reqId)
{
    finishPendingWithError(
        reqId,
        ERROR_RPC_ASYNC_TIMEOUT,
        "async rpc request timeout, reqId = " + reqId);
}

void TinyPbRpcAsyncChannel::finishContext(const std::shared_ptr<AsyncCallContext>& context)
{
    RequestContextGuard contextGuard(context);
    if (context != nullptr) {
        auto *tinyController = dynamic_cast<TinyPbRpcController *>(context->m_controller);
        if (tinyController != nullptr) {
            tinyController->clearCancelCallback();
        }
    }

    if (context != nullptr && context->m_done != nullptr) {
        context->m_done->Run();
    }
}

void TinyPbRpcAsyncChannel::sendContextOnIOThread(const std::shared_ptr<AsyncCallContext>& context)
{
    if (context == nullptr || m_session == nullptr) {
        return;
    }
    RequestContextGuard contextGuard(context);

    m_session->setReadCallback([this](const TinyPbStruct& resp) {
        handleTinyPbResponse(resp);
    });
    m_session->setErrorCallback([this](int errorCode, const std::string& errorInfo) {
        failAllPending(errorCode, errorInfo);
    });

    if (!m_session->isConnected()) {
        if (!m_session->connect()) {
            std::string errorInfo = m_session->getErrorInfo();
            if (errorInfo.empty()) {
                errorInfo = "async session connect failed";
            }
            int errorCode = m_session->getErrorCode() == 0
                ? ERROR_RPC_CHANNEL_NETWORK
                : m_session->getErrorCode();
            finishPendingWithError(context->m_reqId, errorCode, errorInfo);
            return;
        }
    }

    auto *tinyCtrl = dynamic_cast<TinyPbRpcController *>(context->m_controller);
    if (tinyCtrl != nullptr && tinyCtrl->IsCanceled()) {
        return;
    }

    if (!m_session->sendRequest(&context->m_tinyRequest)) {
        if (!hasPending(context->m_reqId)) {
            return;
        }
        if (context->m_timedOut.load()) {
            finishPendingWithError(
                context->m_reqId,
                ERROR_RPC_ASYNC_TIMEOUT,
                "async rpc request timeout, reqId = " + context->m_reqId);
            return;
        }

        std::string errorInfo = m_session->getErrorInfo();
        if (errorInfo.empty()) {
            errorInfo = "async session sendRequest failed";
        }
        int errorCode = m_session->getErrorCode() == 0
            ? ERROR_RPC_CHANNEL_NETWORK
            : m_session->getErrorCode();
        finishPendingWithError(context->m_reqId, errorCode, errorInfo);
    }
}

bool TinyPbRpcAsyncChannel::finishPendingWithError(
    const std::string& reqId,
    int errorCode,
    const std::string& errorInfo)
{
    auto context = takePending(reqId);
    if (context == nullptr) {
        return false;
    }

    setControllerError(context->m_controller, errorCode, errorInfo);
    finishContext(context);
    return true;
}

int TinyPbRpcAsyncChannel::getControllerTimeout(google::protobuf::RpcController *controller)
{
    auto *tinyController = dynamic_cast<TinyPbRpcController *>(controller);
    if (tinyController == nullptr) {
        return 0;
    }

    return tinyController->getTimeout();
}

void TinyPbRpcAsyncChannel::setControllerError(
    google::protobuf::RpcController *controller,
    int errorCode,
    const std::string& errorInfo)
{
    if (controller == nullptr) {
        return;
    }

    auto *tinyController = dynamic_cast<TinyPbRpcController *>(controller);
    if (tinyController != nullptr) {
        tinyController->setError(errorCode, errorInfo);
        return;
    }

    controller->SetFailed(errorInfo);
}

}  // namespace tinyrpc
