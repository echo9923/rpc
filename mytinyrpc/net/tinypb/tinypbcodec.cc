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
    // 本任务 decode 只保留空实现，设置 m_decodeSucc = false。
    if (data != nullptr) {
        data->m_decodeSucc = false;
    }
}

ProtocolType TinyPbCodec::getProtocolType() const
{
    return ProtocolType::TinyPb;
}

}
