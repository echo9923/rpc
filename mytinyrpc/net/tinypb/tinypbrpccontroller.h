#pragma once

#include <google/protobuf/service.h>

#include <functional>
#include <string>
#include <vector>

namespace tinyrpc {

// TinyPbRpcController 是 TinyPB 协议的最小 RpcController 实现。
// 继承 google::protobuf::RpcController，供 Service::CallMethod() 使用。
//
// 当前阶段支持基本的错误状态管理和请求号记录：
//   - SetFailed() 设置错误信息
//   - Failed() 查询是否出错
//   - ErrorText() 获取错误描述
//   - setError()/getErrorCode() 记录框架层错误码
//   - setReqId()/getReqId() 记录本次 RPC 请求号
//
// 取消机制支持记录取消状态、触发 Protobuf 取消回调，并为异步 Channel 提供
// 一个内部取消回调入口，用于把 StartCancel() 转换为 pending 清理。
class TinyPbRpcController : public google::protobuf::RpcController {
 public:
    // 重置控制器状态，清除错误信息和取消标记。
    void Reset() override;

    // 返回是否发生过错误。
    bool Failed() const override;

    // 返回错误描述文本。
    std::string ErrorText() const override;

    // 设置失败原因，将 m_failed 置为 true 并记录错误文本。
    void SetFailed(const std::string& reason) override;

    // 设置框架层错误码和错误文本。
    void setError(int code, const std::string& info);

    // 返回框架层错误码，0 表示未记录错误。
    int getErrorCode() const;

    // 设置本次 RPC 请求号。
    void setReqId(const std::string& reqId);

    // 返回本次 RPC 请求号。
    const std::string& getReqId() const;

    // 设置同步 RPC 超时时间占位，单位毫秒；0 表示未设置。
    void setTimeout(int timeoutMs);

    // 返回同步 RPC 超时时间占位，单位毫秒。
    int getTimeout() const;

    // 注册 TinyRPC 内部取消回调。异步 Channel 用它把 StartCancel() 转换为 pending 清理。
    void setCancelCallback(std::function<void()> callback);

    // 清理 TinyRPC 内部取消回调，避免请求完成后再次触发旧 pending。
    void clearCancelCallback();

    // 发起取消请求，将 m_canceled 置为 true。
    void StartCancel() override;

    // 返回是否已被取消。
    bool IsCanceled() const override;

    // 注册取消回调。若已经取消则立即执行；否则在 StartCancel() 时执行。
    // [第三方 API] google::protobuf::Closure 是 Protobuf 提供的回调闭包抽象基类，
    // 用户需继承并实现 Run() 方法来定义回调逻辑。
    void NotifyOnCancel(google::protobuf::Closure *callback) override;

 private:
    bool m_failed {false};
    bool m_canceled {false};
    int m_errorCode {0};
    int m_timeoutMs {0};
    std::string m_reqId;
    std::string m_errorText;
    std::function<void()> m_cancelCallback;
    std::vector<google::protobuf::Closure *> m_notifyCancelCallbacks;
};

}
