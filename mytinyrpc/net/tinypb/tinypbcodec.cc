#include "net/tinypb/tinypbcodec.h"

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>

namespace tinyrpc {

void TinyPbCodec::appendInt32(TcpBuffer *buffer, int32_t value)
{
    // htonl: 将 uint32_t 从主机字节序转为网络字节序（大端）。
    // 对于 x86（小端）机器，会反转字节顺序；对于大端机器，不做改变。
    uint32_t netValue = htonl(static_cast<uint32_t>(value));
    char buf[4];
    // std::memcpy: 将 netValue 的 4 字节内存表示逐字节复制到 buf。
    std::memcpy(buf, &netValue, 4);
    buffer->append(buf, 4);
}

void TinyPbCodec::encode(TcpBuffer *buffer, AbstractData *data)
{
    // ---- 前置校验：任一失败则 m_encodeSucc = false，不污染 buffer ----

    if (buffer == nullptr || data == nullptr) {
        if (data != nullptr) {
            data->m_encodeSucc = false;
        }
        return;
    }

    // dynamic_cast 将 AbstractData* 安全转为 TinyPbStruct*；
    // 若 data 实际类型不是 TinyPbStruct 则返回 nullptr。
    auto *pb = dynamic_cast<TinyPbStruct *>(data);
    if (pb == nullptr) {
        data->m_encodeSucc = false;
        return;
    }

    if (pb->m_msgReq.empty() || pb->m_serviceFullName.empty()) {
        pb->m_encodeSucc = false;
        return;
    }

    // ---- 回填长度字段 ----
    pb->m_msgReqLen = static_cast<int32_t>(pb->m_msgReq.size());
    pb->m_serviceNameLen = static_cast<int32_t>(pb->m_serviceFullName.size());
    pb->m_errInfoLen = static_cast<int32_t>(pb->m_errInfo.size());

    // ---- 计算 pkLen ----
    // pkLen = 完整帧总字节数（从 PB_START 到 PB_END，含起止标记）。
    // 布局：START(1) + pkLen(4) + msgReqLen(4) + msgReq(N) + serviceNameLen(4)
    //        + serviceFullName(N) + errCode(4) + errInfoLen(4) + errInfo(N)
    //        + pbData(N) + checkNum(4) + END(1)
    int32_t pkLen = 1                          // PB_START
        + 4                                    // pkLen
        + 4 + static_cast<int32_t>(pb->m_msgReq.size())
        + 4 + static_cast<int32_t>(pb->m_serviceFullName.size())
        + 4                                    // errCode
        + 4 + static_cast<int32_t>(pb->m_errInfo.size())
        + static_cast<int32_t>(pb->m_pbData.size())
        + 4                                    // checkNum
        + 1;                                   // PB_END
    pb->m_pkLen = pkLen;

    // ---- 校验字段固定为 1 ----
    pb->m_checkNum = 1;

    // ---- 按顺序写入 TcpBuffer ----
    char start = kTinyPbStart;
    buffer->append(&start, 1);

    appendInt32(buffer, pb->m_pkLen);
    appendInt32(buffer, pb->m_msgReqLen);
    buffer->append(pb->m_msgReq);
    appendInt32(buffer, pb->m_serviceNameLen);
    buffer->append(pb->m_serviceFullName);
    appendInt32(buffer, pb->m_errCode);
    appendInt32(buffer, pb->m_errInfoLen);
    buffer->append(pb->m_errInfo);
    buffer->append(pb->m_pbData);
    appendInt32(buffer, pb->m_checkNum);

    char end = kTinyPbEnd;
    buffer->append(&end, 1);

    pb->m_encodeSucc = true;
}

void TinyPbCodec::decode(TcpBuffer *buffer, AbstractData *data)
{
    // ---- 前置校验 ----

    if (buffer == nullptr) {
        if (data != nullptr) {
            data->m_decodeSucc = false;
        }
        return;
    }

    if (data == nullptr) {
        return;
    }

    // dynamic_cast 将 AbstractData* 安全转为 TinyPbStruct*；
    // 若 data 实际类型不是 TinyPbStruct 则返回 nullptr。
    auto *pb = dynamic_cast<TinyPbStruct *>(data);
    if (pb == nullptr) {
        data->m_decodeSucc = false;
        return;
    }

    const char *raw = buffer->getReadPtr();
    size_t readable = buffer->getReadableBytes();

    // ---- 最小帧长度检查 ----
    // 空帧（所有字符串为空）最少需要：
    //   START(1) + pkLen(4) + msgReqLen(4) + serviceNameLen(4)
    //   + errCode(4) + errInfoLen(4) + checkNum(4) + END(1) = 26
    if (readable < 26) {
        pb->m_decodeSucc = false;
        return;
    }

    // ---- 起始符校验 ----
    if (static_cast<unsigned char>(raw[0]) != kTinyPbStart) {
        pb->m_decodeSucc = false;
        return;
    }

    // ---- 读取 pkLen ----
    int32_t pkLen = 0;
    if (!readInt32(raw, readable, 1, &pkLen)) {
        pb->m_decodeSucc = false;
        return;
    }

    // pkLen 应大于最小帧长度
    if (pkLen < 26) {
        pb->m_decodeSucc = false;
        return;
    }

    // ---- 半包检查：buffer 中数据不足一个完整帧 ----
    if (static_cast<size_t>(pkLen) > readable) {
        pb->m_decodeSucc = false;
        return;
    }

    // ---- 结束符校验 ----
    if (static_cast<unsigned char>(raw[pkLen - 1]) != kTinyPbEnd) {
        pb->m_decodeSucc = false;
        return;
    }

    // ---- 依次解析字段 ----
    size_t offset = 5; // 跳过 START(1) + pkLen(4)

    // msgReqLen
    int32_t msgReqLen = 0;
    if (!readInt32(raw, pkLen, offset, &msgReqLen) || msgReqLen < 0) {
        pb->m_decodeSucc = false;
        return;
    }
    offset += 4;
    pb->m_msgReqLen = msgReqLen;

    // msgReq
    if (offset + static_cast<size_t>(msgReqLen) > static_cast<size_t>(pkLen)) {
        pb->m_decodeSucc = false;
        return;
    }
    pb->m_msgReq = std::string(raw + offset, static_cast<size_t>(msgReqLen));
    offset += static_cast<size_t>(msgReqLen);

    // serviceNameLen
    int32_t serviceNameLen = 0;
    if (!readInt32(raw, pkLen, offset, &serviceNameLen) || serviceNameLen < 0) {
        pb->m_decodeSucc = false;
        return;
    }
    offset += 4;
    pb->m_serviceNameLen = serviceNameLen;

    // serviceFullName
    if (offset + static_cast<size_t>(serviceNameLen) > static_cast<size_t>(pkLen)) {
        pb->m_decodeSucc = false;
        return;
    }
    pb->m_serviceFullName = std::string(raw + offset, static_cast<size_t>(serviceNameLen));
    offset += static_cast<size_t>(serviceNameLen);

    // errCode
    int32_t errCode = 0;
    if (!readInt32(raw, pkLen, offset, &errCode)) {
        pb->m_decodeSucc = false;
        return;
    }
    offset += 4;
    pb->m_errCode = errCode;

    // errInfoLen
    int32_t errInfoLen = 0;
    if (!readInt32(raw, pkLen, offset, &errInfoLen) || errInfoLen < 0) {
        pb->m_decodeSucc = false;
        return;
    }
    offset += 4;
    pb->m_errInfoLen = errInfoLen;

    // errInfo
    if (offset + static_cast<size_t>(errInfoLen) > static_cast<size_t>(pkLen)) {
        pb->m_decodeSucc = false;
        return;
    }
    pb->m_errInfo = std::string(raw + offset, static_cast<size_t>(errInfoLen));
    offset += static_cast<size_t>(errInfoLen);

    // pbData：没有独立的长度字段，剩余字节 = pkLen - offset - 4(checkNum) - 1(PB_END)
    size_t pbDataLen = static_cast<size_t>(pkLen) - offset - 4 - 1;
    pb->m_pbData = std::string(raw + offset, pbDataLen);
    offset += pbDataLen;

    // checkNum
    int32_t checkNum = 0;
    if (!readInt32(raw, pkLen, offset, &checkNum)) {
        pb->m_decodeSucc = false;
        return;
    }
    pb->m_checkNum = checkNum;

    // ---- 成功：回填 pkLen，消费 buffer，设置状态 ----
    pb->m_pkLen = pkLen;
    pb->m_decodeSucc = true;
    buffer->retrieve(static_cast<size_t>(pkLen));
}

bool TinyPbCodec::readInt32(const char *base, size_t readable, size_t offset, int32_t *value)
{
    // 边界检查：offset + 4 不能超过可读范围
    if (offset + 4 > readable) {
        return false;
    }
    // ntohl: 将网络字节序（大端）的 uint32_t 转为主机字节序。
    uint32_t netValue;
    std::memcpy(&netValue, base + offset, 4);
    *value = static_cast<int32_t>(ntohl(netValue));
    return true;
}

ProtocolType TinyPbCodec::getProtocolType() const
{
    return ProtocolType::TinyPb;
}

}
