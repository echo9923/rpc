#include "net/tinypb/tinypbrpcasyncchannel.h"

#include "comm/errorcode.h"
#include "comm/msgreq.h"
#include "net/tinypb/tinypbrpcchannel.h"
#include "net/tinypb/tinypbrpccontroller.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include <utility>

namespace tinyrpc {

TinyPbRpcAsyncChannel::TinyPbRpcAsyncChannel(const IPAddress& peerAddr)
    : m_peerAddr(peerAddr)
{
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
    m_lastContext = context;

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

    // 当前真正网络异步 IO 还未接入。这里临时复用同步 Channel：
    // 同步 Channel 负责序列化、TinyPB 收发和 response 反序列化；
    // 外壳负责保存调用上下文，并保证 done 在成功/失败路径都执行。
    TinyPbRpcChannel syncChannel(m_peerAddr);
    syncChannel.setMsgReqGenerator([msgReq = context->msgReq]() {
        return msgReq;
    });
    syncChannel.CallMethod(method, controller, request, response, done);
    removePending(context->msgReq);
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
    return m_lastContext;
}

size_t TinyPbRpcAsyncChannel::getPendingCount() const
{
    return m_pendingContexts.size();
}

bool TinyPbRpcAsyncChannel::hasPending(const std::string& msgReq) const
{
    return m_pendingContexts.find(msgReq) != m_pendingContexts.end();
}

bool TinyPbRpcAsyncChannel::handleTinyPbResponse(const TinyPbStruct& response)
{
    auto iter = m_pendingContexts.find(response.m_msgReq);
    if (iter == m_pendingContexts.end()) {
        return false;
    }

    auto context = iter->second;
    m_pendingContexts.erase(iter);

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

    m_pendingContexts[context->msgReq] = context;
}

void TinyPbRpcAsyncChannel::removePending(const std::string& msgReq)
{
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
