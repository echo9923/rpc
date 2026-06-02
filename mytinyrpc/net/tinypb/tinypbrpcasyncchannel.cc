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

    // 任务七十四只建立异步接口外壳。这里临时复用同步 Channel：
    // 同步 Channel 负责序列化、TinyPB 收发和 response 反序列化；
    // 外壳负责保存调用上下文，并保证 done 在成功/失败路径都执行。
    TinyPbRpcChannel syncChannel(m_peerAddr);
    syncChannel.setMsgReqGenerator([msgReq = context->msgReq]() {
        return msgReq;
    });
    syncChannel.CallMethod(method, controller, request, response, done);
}

void TinyPbRpcAsyncChannel::setMsgReqGenerator(std::function<std::string()> generator)
{
    m_msgReqGenerator = std::move(generator);
}

std::shared_ptr<AsyncCallContext> TinyPbRpcAsyncChannel::getLastContext() const
{
    return m_lastContext;
}

std::string TinyPbRpcAsyncChannel::genMsgReq() const
{
    if (m_msgReqGenerator != nullptr) {
        return m_msgReqGenerator();
    }

    return MsgReqUtil::genMsgNumber();
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
