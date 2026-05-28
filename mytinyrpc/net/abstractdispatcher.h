#pragma once

#include "net/abstractdata.h"

#include <memory>

namespace tinyrpc {

class TcpConnection;

// AbstractDispatcher 是所有协议分发器的抽象基类。
// 具体协议分发器（如 TinyPbDispatcher）继承此类，
// 实现 dispatch 方法以完成"协议数据对象 → 业务处理 → 响应写回"的流程。
//
// 典型调用流程：
//   TcpConnection::execute() 解码出 AbstractData 后，
//   调用 m_dispatcher->dispatch(data, this) 将请求交给分发器处理。
//   分发器内部通过 conn->sendProtocolData() 将响应写回输出缓冲区。
class AbstractDispatcher {
 public:
    using Ptr = std::shared_ptr<AbstractDispatcher>;

    virtual ~AbstractDispatcher() = default;

    // 处理解码后的协议数据对象，并将响应写回连接。
    // data：解码后的协议数据（如 TinyPbStruct）。
    // conn：当前 TCP 连接，用于通过 sendProtocolData() 写回响应。
    virtual void dispatch(AbstractData *data, TcpConnection *conn) = 0;
};

}
