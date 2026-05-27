#pragma once

#include "net/abstractdata.h"
#include "net/tcpbuffer.h"

#include <memory>

namespace tinyrpc {

// AbstractCodec 是所有协议编解码器的抽象基类。
// 具体协议编解码器（如 HttpCodec、TinyPbCodec）继承此类，
// 实现 encode / decode 方法以完成"协议数据对象 ↔ TcpBuffer 字节流"的转换。
//
// 典型调用流程：
//   encode：将 AbstractData 中的结构化字段序列化并追加到 TcpBuffer。
//   decode：从 TcpBuffer 中取出字节流，反序列化并填充到 AbstractData。
class AbstractCodec {
 public:
    using Ptr = std::shared_ptr<AbstractCodec>;

    virtual ~AbstractCodec() = default;

    // 将 data 序列化并写入 buffer。
    // 成功后应将 data->encodeSucc 置为 true。
    virtual void encode(TcpBuffer *buffer, AbstractData *data) = 0;

    // 从 buffer 中读取字节流并反序列化到 data。
    // 成功后应将 data->decodeSucc 置为 true。
    virtual void decode(TcpBuffer *buffer, AbstractData *data) = 0;

    // 返回当前 Codec 对应的协议类型。
    virtual ProtocolType getProtocolType() const = 0;
};

}
