#include "net/tinypb/tinypbrpcasyncchannel.h"

#include "comm/errorcode.h"
#include "comm/msgreq.h"
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
    context->controller = controller;
    context->request = request;
    context->response = response;
    context->done = done;
    if (method != nullptr) {
        context->methodFullName = method->full_name();
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
    if (tinyController != nullptr && !tinyController->MsgReq().empty()) {
        context->msgReq = tinyController->MsgReq();
    } else {
        context->msgReq = genMsgReq();
        if (tinyController != nullptr) {
            tinyController->SetMsgReq(context->msgReq);
        }
    }

    context->tinyRequest.m_msgReq = context->msgReq;
    context->tinyRequest.m_serviceFullName = context->methodFullName;
    // [第三方 API] SerializeToString 把业务 request 编码成 Protobuf 二进制串，
    // 该 payload 会作为 TinyPB request 的 pbData 进入后续 pending 发送路径。
    if (!request->SerializeToString(&context->tinyRequest.m_pbData)) {
        setControllerError(controller, ERROR_FAILED_SERIALIZE, "failed to serialize async request pbData");
        finish();
        return;
    }

    if (tinyController != nullptr) {
        tinyController->SetCancelCallback([this, msgReq = context->msgReq]() {
            cancel(msgReq);
        });
    }

    registerPending(context);
    registerTimeoutEvent(context);

    if (tinyController != nullptr && tinyController->IsCanceled()) {
        cancel(context->msgReq);
        return;
    }

    if (!m_syncFallbackEnabled) {
        return;
    }

    m_ioThread->addTask([this, context]() {
        TinyPbStruct tinyResponse;
        TcpClient client(m_peerAddr);
        int timeoutMs = getControllerTimeout(context->controller);
        if (timeoutMs > 0) {
            client.setTimeout(timeoutMs);
        }

        if (!client.sendAndRecvTinyPb(&context->tinyRequest, &tinyResponse)) {
            std::string errorInfo = client.getErrorInfo();
            if (errorInfo.empty()) {
                errorInfo = "TinyPB async network request failed";
            }
            int errorCode = client.getErrorCode() == 0 ? ERROR_RPC_CHANNEL_NETWORK : client.getErrorCode();
            if (errorCode == ERROR_TCP_TIMEOUT) {
                errorCode = ERROR_RPC_ASYNC_TIMEOUT;
                errorInfo = "async rpc request timeout, msgReq = " + context->msgReq;
            }
            finishPendingWithError(context->msgReq, errorCode, errorInfo);
            return;
        }

        if (!handleTinyPbResponse(tinyResponse)) {
            finishPendingWithError(
                context->msgReq,
                ERROR_RPC_MSGREQ_MISMATCH,
                "async response msgReq not found, response = " + tinyResponse.m_msgReq);
        }
    });
}

void TinyPbRpcAsyncChannel::setMsgReqGenerator(std::function<std::string()> generator)
{
    m_msgReqGenerator = std::move(generator);
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

bool TinyPbRpcAsyncChannel::hasPending(const std::string& msgReq) const
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    return m_pendingContexts.find(msgReq) != m_pendingContexts.end();
}

bool TinyPbRpcAsyncChannel::handleTinyPbResponse(const TinyPbStruct& response)
{
    auto context = takePending(response.m_msgReq);
    if (context == nullptr) {
        return false;
    }

    if (response.m_errCode != 0) {
        setControllerError(context->controller, response.m_errCode, response.m_errInfo);
        finishContext(context);
        return true;
    }

    // [第三方 API] ParseFromString 把 TinyPB response 的业务 payload
    // 反序列化到 Stub 调用方传入的 response 对象。
    if (context->response == nullptr || !context->response->ParseFromString(response.m_pbData)) {
        setControllerError(
            context->controller,
            ERROR_FAILED_DESERIALIZE,
            "failed to deserialize async response pbData");
        finishContext(context);
        return true;
    }

    finishContext(context);
    return true;
}

bool TinyPbRpcAsyncChannel::cancel(const std::string& msgReq)
{
    auto context = takePending(msgReq);
    if (context == nullptr) {
        return false;
    }

    auto *tinyController = dynamic_cast<TinyPbRpcController *>(context->controller);
    if (tinyController != nullptr) {
        tinyController->StartCancel();
    }
    setControllerError(
        context->controller,
        ERROR_RPC_ASYNC_CANCELED,
        "async rpc request canceled, msgReq = " + msgReq);
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

std::string TinyPbRpcAsyncChannel::genMsgReq() const
{
    if (m_msgReqGenerator != nullptr) {
        return m_msgReqGenerator();
    }

    return MsgReqUtil::genMsgNumber();
}

void TinyPbRpcAsyncChannel::registerPending(const std::shared_ptr<AsyncCallContext>& context)
{
    if (context == nullptr || context->msgReq.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingContexts[context->msgReq] = context;
}

void TinyPbRpcAsyncChannel::registerTimeoutEvent(const std::shared_ptr<AsyncCallContext>& context)
{
    int timeoutMs = getControllerTimeout(context == nullptr ? nullptr : context->controller);
    if (context == nullptr || context->msgReq.empty() || timeoutMs <= 0 || m_ioThread == nullptr) {
        return;
    }

    context->timeoutEvent = std::make_shared<TimerEvent>(timeoutMs, false, [this, msgReq = context->msgReq]() {
        handleTimeout(msgReq);
    });

    auto event = context->timeoutEvent;
    m_ioThread->addTask([this, event]() {
        if (event == nullptr || event->isCanceled() || m_ioThread == nullptr) {
            return;
        }
        auto *reactor = m_ioThread->getReactor();
        if (reactor == nullptr || reactor->getTimer() == nullptr) {
            return;
        }
        reactor->getTimer()->addTimerEvent(event);
    });
}

std::shared_ptr<AsyncCallContext> TinyPbRpcAsyncChannel::takePending(const std::string& msgReq)
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    auto iter = m_pendingContexts.find(msgReq);
    if (iter == m_pendingContexts.end()) {
        return nullptr;
    }

    auto context = iter->second;
    m_pendingContexts.erase(iter);
    cancelTimeoutEvent(context);
    return context;
}

void TinyPbRpcAsyncChannel::cancelTimeoutEvent(const std::shared_ptr<AsyncCallContext>& context)
{
    if (context != nullptr && context->timeoutEvent != nullptr) {
        context->timeoutEvent->cancel();
    }
}

void TinyPbRpcAsyncChannel::handleTimeout(const std::string& msgReq)
{
    finishPendingWithError(
        msgReq,
        ERROR_RPC_ASYNC_TIMEOUT,
        "async rpc request timeout, msgReq = " + msgReq);
}

void TinyPbRpcAsyncChannel::finishContext(const std::shared_ptr<AsyncCallContext>& context)
{
    if (context != nullptr) {
        auto *tinyController = dynamic_cast<TinyPbRpcController *>(context->controller);
        if (tinyController != nullptr) {
            tinyController->ClearCancelCallback();
        }
    }

    if (context != nullptr && context->done != nullptr) {
        context->done->Run();
    }
}

bool TinyPbRpcAsyncChannel::finishPendingWithError(
    const std::string& msgReq,
    int errorCode,
    const std::string& errorInfo)
{
    auto context = takePending(msgReq);
    if (context == nullptr) {
        return false;
    }

    setControllerError(context->controller, errorCode, errorInfo);
    finishContext(context);
    return true;
}

int TinyPbRpcAsyncChannel::getControllerTimeout(google::protobuf::RpcController *controller)
{
    auto *tinyController = dynamic_cast<TinyPbRpcController *>(controller);
    if (tinyController == nullptr) {
        return 0;
    }

    return tinyController->Timeout();
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
        tinyController->SetError(errorCode, errorInfo);
        return;
    }

    controller->SetFailed(errorInfo);
}

}  // namespace tinyrpc
