#include "net/tinypb/tinypbrpcasyncchannel.h"

#include "comm/errorcode.h"
#include "comm/reqid.h"
#include "net/tcpclient.h"
#include "net/timer.h"
#include "net/tinypb/tinypbrpccontroller.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include <string>
#include <utility>

namespace tinyrpc {

TinyPbRpcAsyncChannel::TinyPbRpcAsyncChannel(const IPAddress& peerAddr)
    : m_peerAddr(peerAddr),
      m_ioThread(std::make_unique<IOThread>())
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
    }
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
        TinyPbStruct tinyResponse;
        TcpClient client(m_peerAddr);
        int timeoutMs = getControllerTimeout(context->m_controller);
        if (timeoutMs > 0) {
            client.setTimeout(timeoutMs);
        }

        if (!client.sendAndRecvTinyPb(&context->m_tinyRequest, &tinyResponse)) {
            std::string errorInfo = client.getErrorInfo();
            if (errorInfo.empty()) {
                errorInfo = "TinyPB async network request failed";
            }
            int errorCode = client.getErrorCode() == 0 ? ERROR_RPC_CHANNEL_NETWORK : client.getErrorCode();
            if (errorCode == ERROR_TCP_TIMEOUT) {
                errorCode = ERROR_RPC_ASYNC_TIMEOUT;
                errorInfo = "async rpc request timeout, reqId = " + context->m_reqId;
            }
            finishPendingWithError(context->m_reqId, errorCode, errorInfo);
            return;
        }

        if (!handleTinyPbResponse(tinyResponse)) {
            finishPendingWithError(
                context->m_reqId,
                ERROR_RPC_REQID_MISMATCH,
                "async response reqId not found, response = " + tinyResponse.m_reqId);
        }
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
    if (m_ioThread != nullptr) {
        m_ioThread->stop();
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
    if (context == nullptr || context->m_reqId.empty() || timeoutMs <= 0 || m_ioThread == nullptr) {
        return;
    }

    context->m_timeoutTask = std::make_shared<TimerTask>(timeoutMs, false, [this, reqId = context->m_reqId]() {
        handleTimeout(reqId);
    });

    auto task = context->m_timeoutTask;
    m_ioThread->addTask([this, task]() {
        if (task == nullptr || task->isCanceled() || m_ioThread == nullptr) {
            return;
        }
        auto *reactor = m_ioThread->getReactor();
        if (reactor == nullptr || reactor->getTimer() == nullptr) {
            return;
        }
        reactor->getTimer()->addTimerTask(task);
    });
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
