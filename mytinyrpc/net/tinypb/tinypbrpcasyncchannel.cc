#include "net/tinypb/tinypbrpcasyncchannel.h"

#include "comm/errorcode.h"
#include "comm/msgreq.h"
#include "net/tcpclient.h"
#include "net/tinypb/tinypbrpcchannel.h"
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

    registerPending(context);

    if (!m_syncFallbackEnabled) {
        return;
    }

    m_ioThread->addTask([this, context]() {
        TinyPbStruct tinyResponse;
        TcpClient client(m_peerAddr);
        auto *tinyController = dynamic_cast<TinyPbRpcController *>(context->controller);
        if (tinyController != nullptr && tinyController->Timeout() > 0) {
            client.setTimeout(tinyController->Timeout());
        }

        if (!client.sendAndRecvTinyPb(&context->tinyRequest, &tinyResponse)) {
            std::string errorInfo = client.getErrorInfo();
            if (errorInfo.empty()) {
                errorInfo = "TinyPB async network request failed";
            }
            int errorCode = client.getErrorCode() == 0 ? ERROR_RPC_CHANNEL_NETWORK : client.getErrorCode();
            removePending(context->msgReq);
            setControllerError(context->controller, errorCode, errorInfo);
            finishContext(context);
            return;
        }

        if (!handleTinyPbResponse(tinyResponse)) {
            setControllerError(
                context->controller,
                ERROR_RPC_MSGREQ_MISMATCH,
                "async response msgReq not found, response = " + tinyResponse.m_msgReq);
            finishContext(context);
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
    std::shared_ptr<AsyncCallContext> context;
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        auto iter = m_pendingContexts.find(response.m_msgReq);
        if (iter == m_pendingContexts.end()) {
            return false;
        }

        context = iter->second;
        m_pendingContexts.erase(iter);
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

void TinyPbRpcAsyncChannel::removePending(const std::string& msgReq)
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingContexts.erase(msgReq);
}

void TinyPbRpcAsyncChannel::finishContext(const std::shared_ptr<AsyncCallContext>& context)
{
    if (context != nullptr && context->done != nullptr) {
        context->done->Run();
    }
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
