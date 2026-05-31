#include "net/tinypb/tinypbrpcchannel.h"

#include "comm/errorcode.h"
#include "comm/msgreq.h"
#include "net/tcpclient.h"
#include "net/tinypb/tinypbdata.h"
#include "net/tinypb/tinypbrpccontroller.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include <string>
#include <utility>

namespace tinyrpc {

TinyPbRpcChannel::TinyPbRpcChannel(const IPAddress& peerAddr)
    : m_peerAddr(peerAddr)
{
}

void TinyPbRpcChannel::setMsgReqGenerator(std::function<std::string()> generator)
{
    m_msgReqGenerator = std::move(generator);
}

void TinyPbRpcChannel::CallMethod(
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

    if (method == nullptr || request == nullptr || response == nullptr) {
        setControllerError(
            controller,
            ERROR_RPC_CHANNEL_INVALID_ARGUMENT,
            "TinyPbRpcChannel CallMethod argument is null");
        finish();
        return;
    }

    TinyPbStruct tinyRequest;
    auto *tinyController = dynamic_cast<TinyPbRpcController *>(controller);
    if (tinyController != nullptr && !tinyController->MsgReq().empty()) {
        tinyRequest.m_msgReq = tinyController->MsgReq();
    } else {
        tinyRequest.m_msgReq = genMsgReq();
    }
    tinyRequest.m_serviceFullName = method->full_name();

    if (tinyController != nullptr) {
        tinyController->SetMsgReq(tinyRequest.m_msgReq);
    }

    // [第三方 API] SerializeToString 将业务 request 编码为 Protobuf 二进制串，
    // 该二进制串作为 TinyPB envelope 中的 pbData 字段传输。
    if (!request->SerializeToString(&tinyRequest.m_pbData)) {
        setControllerError(controller, ERROR_FAILED_SERIALIZE, "failed to serialize request pbData");
        finish();
        return;
    }

    TinyPbStruct tinyResponse;
    TcpClient client(m_peerAddr);
    if (tinyController != nullptr && tinyController->Timeout() > 0) {
        client.setTimeout(tinyController->Timeout());
    }
    if (!client.sendAndRecvTinyPb(&tinyRequest, &tinyResponse)) {
        std::string errorInfo = client.getErrorInfo();
        if (errorInfo.empty()) {
            errorInfo = "TinyPB network request failed";
        }
        int errorCode = client.getErrorCode() == 0 ? ERROR_RPC_CHANNEL_NETWORK : client.getErrorCode();
        setControllerError(controller, errorCode, errorInfo);
        finish();
        return;
    }

    if (tinyResponse.m_msgReq != tinyRequest.m_msgReq) {
        setControllerError(
            controller,
            ERROR_RPC_MSGREQ_MISMATCH,
            "response msgReq mismatch, request = " + tinyRequest.m_msgReq
                + ", response = " + tinyResponse.m_msgReq);
        finish();
        return;
    }

    if (tinyResponse.m_errCode != 0) {
        setControllerError(controller, tinyResponse.m_errCode, tinyResponse.m_errInfo);
        finish();
        return;
    }

    // [第三方 API] ParseFromString 将 TinyPB response 的业务 payload
    // 反序列化到 Stub 传入的 response 对象中。
    if (!response->ParseFromString(tinyResponse.m_pbData)) {
        setControllerError(controller, ERROR_FAILED_DESERIALIZE, "failed to deserialize response pbData");
        finish();
        return;
    }

    finish();
}

std::string TinyPbRpcChannel::genMsgReq() const
{
    if (m_msgReqGenerator != nullptr) {
        return m_msgReqGenerator();
    }

    return MsgReqUtil::genMsgNumber();
}

void TinyPbRpcChannel::setControllerError(
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

}
