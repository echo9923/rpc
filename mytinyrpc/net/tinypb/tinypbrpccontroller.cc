#include "net/tinypb/tinypbrpccontroller.h"

#include <utility>

namespace tinyrpc {

void TinyPbRpcController::Reset()
{
    m_failed = false;
    m_canceled = false;
    m_errorCode = 0;
    m_timeoutMs = 0;
    m_reqId.clear();
    m_errorText.clear();
    m_cancelCallback = nullptr;
    m_notifyCancelCallbacks.clear();
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

void TinyPbRpcController::setError(int code, const std::string& info)
{
    m_failed = true;
    m_errorCode = code;
    m_errorText = info;
}

int TinyPbRpcController::getErrorCode() const
{
    return m_errorCode;
}

void TinyPbRpcController::setReqId(const std::string& reqId)
{
    m_reqId = reqId;
}

const std::string& TinyPbRpcController::getReqId() const
{
    return m_reqId;
}

void TinyPbRpcController::setTimeout(int timeoutMs)
{
    m_timeoutMs = timeoutMs;
}

int TinyPbRpcController::getTimeout() const
{
    return m_timeoutMs;
}

void TinyPbRpcController::setCancelCallback(std::function<void()> callback)
{
    m_cancelCallback = std::move(callback);
}

void TinyPbRpcController::clearCancelCallback()
{
    m_cancelCallback = nullptr;
}

void TinyPbRpcController::StartCancel()
{
    if (m_canceled) {
        return;
    }

    m_canceled = true;
    auto cancelCallback = m_cancelCallback;
    if (cancelCallback) {
        cancelCallback();
    }

    for (auto *callback : m_notifyCancelCallbacks) {
        if (callback != nullptr) {
            callback->Run();
        }
    }
}

bool TinyPbRpcController::IsCanceled() const
{
    return m_canceled;
}

void TinyPbRpcController::NotifyOnCancel(google::protobuf::Closure *callback)
{
    if (callback == nullptr) {
        return;
    }
    if (m_canceled) {
        callback->Run();
        return;
    }
    m_notifyCancelCallbacks.push_back(callback);
}

}
