#include "net/tinypb/tinypbrpcchannel.h"

#include "comm/errorcode.h"
#include "comm/reqid.h"
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

void TinyPbRpcChannel::setReqIdGenerator(std::function<std::string()> generator)
{
    m_reqIdGenerator = std::move(generator);
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
    if (tinyController != nullptr && !tinyController->getReqId().empty()) {
        tinyRequest.m_reqId = tinyController->getReqId();
    } else {
        tinyRequest.m_reqId = genReqId();
    }
    tinyRequest.m_serviceFullName = method->full_name();

    if (tinyController != nullptr) {
        tinyController->setReqId(tinyRequest.m_reqId);
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
    if (tinyController != nullptr && tinyController->getTimeout() > 0) {
        client.setTimeout(tinyController->getTimeout());
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

    if (tinyResponse.m_reqId != tinyRequest.m_reqId) {
        setControllerError(
            controller,
            ERROR_RPC_REQID_MISMATCH,
            "response reqId mismatch, request = " + tinyRequest.m_reqId
                + ", response = " + tinyResponse.m_reqId);
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

std::string TinyPbRpcChannel::genReqId() const
{
    if (m_reqIdGenerator != nullptr) {
        return m_reqIdGenerator();
    }

    return ReqIdUtil::genReqId();
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
        tinyController->setError(errorCode, errorInfo);
        return;
    }

    controller->SetFailed(errorInfo);
}

}
