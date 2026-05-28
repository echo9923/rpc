#include "net/tinypb/tinypbrpccontroller.h"

namespace tinyrpc {

void TinyPbRpcController::Reset()
{
    m_failed = false;
    m_canceled = false;
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
    m_errorText = reason;
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
