#include "net/tinypb/tinypbrpccontroller.h"

namespace tinyrpc {

void TinyPbRpcController::Reset()
{
    m_failed = false;
    m_canceled = false;
    m_errorCode = 0;
    m_timeoutMs = 0;
    m_msgReq.clear();
    m_errorText.clear();
}

bool TinyPbRpcController::Failed() const
{
    return m_failed;
}

std::string TinyPbRpcController::ErrorText() const
{
    return m_errorText;
}

void TinyPbRpcController::SetFailed(const std::string& reason)
{
    m_failed = true;
    m_errorCode = -1;
    m_errorText = reason;
}

void TinyPbRpcController::SetError(int code, const std::string& info)
{
    m_failed = true;
    m_errorCode = code;
    m_errorText = info;
}

int TinyPbRpcController::ErrorCode() const
{
    return m_errorCode;
}

void TinyPbRpcController::SetMsgReq(const std::string& msgReq)
{
    m_msgReq = msgReq;
}

const std::string& TinyPbRpcController::MsgReq() const
{
    return m_msgReq;
}

void TinyPbRpcController::SetTimeout(int timeoutMs)
{
    m_timeoutMs = timeoutMs;
}

int TinyPbRpcController::Timeout() const
{
    return m_timeoutMs;
}

void TinyPbRpcController::StartCancel()
{
    m_canceled = true;
}

bool TinyPbRpcController::IsCanceled() const
{
    return m_canceled;
}

void TinyPbRpcController::NotifyOnCancel(google::protobuf::Closure * /*callback*/)
{
    // 当前阶段不支持取消回调，留空。
}

}
