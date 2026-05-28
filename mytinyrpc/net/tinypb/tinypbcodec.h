#pragma once

#include "net/abstractcodec.h"
#include "net/tinypb/tinypbdata.h"

#include <cstdint>
#include <memory>

namespace tinyrpc {

// TinyPB 协议帧起止标记
constexpr char kTinyPbStart = 0x02;
constexpr char kTinyPbEnd = 0x03;

// TinyPbCodec 负责 TinyPB 协议的编码和解码。
// 编码布局（按网络传输顺序）：
//   PB_START(1) | pkLen(4) | msgReqLen(4) | msgReq(N) | serviceNameLen(4)
//   | serviceFullName(N) | errCode(4) | errInfoLen(4) | errInfo(N)
//   | pbData(N) | checkNum(4) | PB_END(1)
//
// 其中所有 int32_t 字段以网络字节序（大端）写入，
// pkLen 为完整帧总字节数（含起止标记），checkNum 当前固定为 1。
class TinyPbCodec : public AbstractCodec {
 public:
    using Ptr = std::shared_ptr<TinyPbCodec>;

    ~TinyPbCodec() override = default;

    // 将 TinyPbStruct 编码为字节流并追加到 buffer。
    // 前置校验：buffer/data 非 nullptr、data 可转为 TinyPbStruct、
    // m_msgReq 和 m_serviceFullName 非空。
    // 校验失败时 data->m_encodeSucc 置 false，不向 buffer 写入任何数据。
    // 成功时回填 m_pkLen / m_msgReqLen / m_serviceNameLen / m_errInfoLen / m_checkNum，
    // 并将 data->m_encodeSucc 置 true。
    void encode(TcpBuffer *buffer, AbstractData *data) override;

    // 从 buffer 可读区间中查找第一个 kTinyPbStart，从该位置解析一个完整的 TinyPB 帧。
    // 起始符前的无效字节（如有）会在解析成功时一并消费。
    // 半包或无起始符时 decode 失败且不消费 buffer。
    // 一次 decode 只解析第一个完整帧，后续帧保留在 buffer 中。
    // 成功时设置 m_decodeSucc = true 并消费（前置无效字节 + pkLen）字节。
    // 失败时设置 m_decodeSucc = false，不消费 buffer。
    void decode(TcpBuffer *buffer, AbstractData *data) override;

    // 返回协议类型 TinyPb。
    ProtocolType getProtocolType() const override;

 private:
    // 将 int32_t 以网络字节序写入 buffer（4 字节大端）。
    // htonl 将主机字节序的 uint32_t 转换为网络字节序（大端），
    // 然后逐字节写入 buffer。
    static void appendInt32(TcpBuffer *buffer, int32_t value);

    // 从 base + offset 处读取 4 字节网络字节序 int32_t，转为主机序写入 *value。
    // readable 为可用总字节数，用于边界检查。
    // offset + 4 <= readable 时返回 true，否则返回 false（不修改 *value）。
    static bool readInt32(const char *base, size_t readable, size_t offset, int32_t *value);

    // 在 base[0..readable-1] 中查找第一个 kTinyPbStart (0x02)。
    // 找到时返回 true 并将 *start 设为其下标偏移；否则返回 false。
    static bool findFrameStart(const char *base, size_t readable, size_t *start);
};

}
