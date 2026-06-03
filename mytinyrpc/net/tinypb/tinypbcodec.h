#pragma once

#include "net/abstractcodec.h"
#include "net/tinypb/tinypbdata.h"

#include <cstdint>
#include <memory>

namespace tinyrpc {

// TinyPB 协议帧起止标记
constexpr char kTinyPbStart = 0x02;
constexpr char kTinyPbEnd = 0x03;

// TinyPB 协议帧长度上下限
// 最小帧：START(1) + pkLen(4) + reqIdLen(4) + serviceNameLen(4)
//         + errCode(4) + errInfoLen(4) + checkNum(4) + END(1) = 26
constexpr int32_t kTinyPbMinPackageLength = 26;

// 最大帧：1MB，防止恶意超大 pkLen 导致解码器误等待
constexpr int32_t kTinyPbMaxPackageLength = 1024 * 1024;

// TinyPbCodec 负责 TinyPB 协议的编码和解码。
// 编码布局（按网络传输顺序）：
//   PB_START(1) | pkLen(4) | reqIdLen(4) | reqId(N) | serviceNameLen(4)
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
    // m_reqId 和 m_serviceFullName 非空。
    // 校验失败时 data->m_encodeSucc 置 false，不向 buffer 写入任何数据。
    // 成功时回填 m_pkLen / m_reqIdLen / m_serviceNameLen / m_errInfoLen / m_checkNum，
    // 并将 data->m_encodeSucc 置 true。
    void encode(TcpBuffer *buffer, AbstractData *data) override;

    // 从 buffer 可读区间中扫描 kTinyPbStart 候选，尝试解析一个完整的 TinyPB 帧。
    // 遇到非法候选（包长越界、尾字节错误、字段越界）时跳过，继续向后扫描。
    // 遇到合法候选但数据不完整时视为半包，失败且不消费 buffer。
    // 成功时消费从原始读位置到合法帧结束的所有字节（含被跳过的坏候选）。
    // 一次 decode 只解析第一个合法帧，后续帧保留在 buffer 中。
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

    // 在 base[0..readable-1] 中从 from 位置开始查找第一个 kTinyPbStart (0x02)。
    // 找到时返回 true 并将 *start 设为其下标（相对于 base 起始的绝对偏移）；
    // 否则返回 false。
    static bool findFrameStart(const char *base, size_t readable, size_t from, size_t *start);

    // 检查 pkLen 是否在合法范围 [kTinyPbMinPackageLength, kTinyPbMaxPackageLength] 内。
    static bool isValidPackageLength(int32_t pkLen);
};

}
