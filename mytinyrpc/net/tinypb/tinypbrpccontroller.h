#pragma once

#include <google/protobuf/service.h>

#include <string>

namespace tinyrpc {

// TinyPbRpcController 是 TinyPB 协议的最小 RpcController 实现。
// 继承 google::protobuf::RpcController，供 Service::CallMethod() 使用。
//
// 当前阶段支持基本的错误状态管理和请求号记录：
//   - SetFailed() 设置错误信息
//   - Failed() 查询是否出错
//   - ErrorText() 获取错误描述
//   - SetError()/ErrorCode() 记录框架层错误码
//   - SetMsgReq()/MsgReq() 记录本次 RPC 请求号
//
// 取消机制（StartCancel / IsCanceled / NotifyOnCancel）为空实现，
// 后续接入异步调用时再扩展。
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
    void SetError(int code, const std::string& info);

    // 返回框架层错误码，0 表示未记录错误。
    int ErrorCode() const;

    // 设置本次 RPC 请求号。
    void SetMsgReq(const std::string& msgReq);

    // 返回本次 RPC 请求号。
    const std::string& MsgReq() const;

    // 发起取消请求，将 m_canceled 置为 true。
    void StartCancel() override;

    // 返回是否已被取消。
    bool IsCanceled() const override;

    // 注册取消回调，当前为空实现。
    // [第三方 API] google::protobuf::Closure 是 Protobuf 提供的回调闭包抽象基类，
    // 用户需继承并实现 Run() 方法来定义回调逻辑。
    void NotifyOnCancel(google::protobuf::Closure *callback) override;

 private:
    bool m_failed {false};
    bool m_canceled {false};
    int m_errorCode {0};
    std::string m_msgReq;
    std::string m_errorText;
};

}
