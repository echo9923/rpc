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

    if (pb->m_reqId.empty() || pb->m_serviceFullName.empty()) {
        pb->m_encodeSucc = false;
        return;
    }

    // ---- 回填长度字段 ----
    pb->m_reqIdLen = static_cast<int32_t>(pb->m_reqId.size());
    pb->m_serviceNameLen = static_cast<int32_t>(pb->m_serviceFullName.size());
    pb->m_errInfoLen = static_cast<int32_t>(pb->m_errInfo.size());

    // ---- 计算 pkLen ----
    // pkLen = 完整帧总字节数（从 PB_START 到 PB_END，含起止标记）。
    // 布局：START(1) + pkLen(4) + reqIdLen(4) + reqId(N) + serviceNameLen(4)
    //        + serviceFullName(N) + errCode(4) + errInfoLen(4) + errInfo(N)
    //        + pbData(N) + checkNum(4) + END(1)
    int32_t pkLen = 1                          // PB_START
        + 4                                    // pkLen
        + 4 + static_cast<int32_t>(pb->m_reqId.size())
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
    appendInt32(buffer, pb->m_reqIdLen);
    buffer->append(pb->m_reqId);
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

    // ---- 循环扫描候选起始符 ----
    // 遇到非法候选时跳过，从下一个 0x02 继续；
    // 遇到合法候选但数据不完整时视为半包，直接返回失败。
    size_t scanPos = 0;
    while (true) {
        // 从 scanPos 开始查找下一个 kTinyPbStart
        size_t startOffset = 0;
        if (!findFrameStart(raw, readable, scanPos, &startOffset)) {
            // 没有找到任何起始符，buffer 中无合法帧起点
            pb->m_decodeSucc = false;
            return;
        }

        const char *frameRaw = raw + startOffset;
        size_t frameReadable = readable - startOffset;

        // 数据不足以读取 pkLen（至少需要 START(1) + pkLen(4) = 5 字节）
        if (frameReadable < 5) {
            pb->m_decodeSucc = false;
            return;
        }

        // 读取 pkLen 字段
        int32_t pkLen = 0;
        if (!readInt32(frameRaw, frameReadable, 1, &pkLen)) {
            // 理论上 frameReadable >= 5 时不会失败，防御性处理
            scanPos = startOffset + 1;
            continue;
        }

        // 包长合法性检查：非法则跳过此候选
        if (!isValidPackageLength(pkLen)) {
            scanPos = startOffset + 1;
            continue;
        }

        // 半包检查：包长合法但数据不完整，视为合法半包，等待更多数据
        if (static_cast<size_t>(pkLen) > frameReadable) {
            pb->m_decodeSucc = false;
            return;
        }

        // 结束符校验：不匹配则跳过此候选
        if (static_cast<unsigned char>(frameRaw[pkLen - 1]) != kTinyPbEnd) {
            scanPos = startOffset + 1;
            continue;
        }

        // ---- 依次解析字段 ----
        // 所有解析变量在此声明，避免 goto 跨越初始化问题。
        bool parseFailed = false;
        size_t offset = 5; // 跳过 START(1) + pkLen(4)
        int32_t reqIdLen = 0;
        int32_t serviceNameLen = 0;
        int32_t errCode = 0;
        int32_t errInfoLen = 0;

        // reqIdLen
        if (!readInt32(frameRaw, pkLen, offset, &reqIdLen) || reqIdLen < 0) {
            parseFailed = true;
            goto parse_done;
        }
        offset += 4;
        pb->m_reqIdLen = reqIdLen;

        // reqId
        if (offset + static_cast<size_t>(reqIdLen) > static_cast<size_t>(pkLen)) {
            parseFailed = true;
            goto parse_done;
        }
        pb->m_reqId = std::string(frameRaw + offset, static_cast<size_t>(reqIdLen));
        offset += static_cast<size_t>(reqIdLen);

        // serviceNameLen
        if (!readInt32(frameRaw, pkLen, offset, &serviceNameLen) || serviceNameLen < 0) {
            parseFailed = true;
            goto parse_done;
        }
        offset += 4;
        pb->m_serviceNameLen = serviceNameLen;

        // serviceFullName
        if (offset + static_cast<size_t>(serviceNameLen) > static_cast<size_t>(pkLen)) {
            parseFailed = true;
            goto parse_done;
        }
        pb->m_serviceFullName = std::string(frameRaw + offset, static_cast<size_t>(serviceNameLen));
        offset += static_cast<size_t>(serviceNameLen);

        // errCode
        if (!readInt32(frameRaw, pkLen, offset, &errCode)) {
            parseFailed = true;
            goto parse_done;
        }
        offset += 4;
        pb->m_errCode = errCode;

        // errInfoLen
        if (!readInt32(frameRaw, pkLen, offset, &errInfoLen) || errInfoLen < 0) {
            parseFailed = true;
            goto parse_done;
        }
        offset += 4;
        pb->m_errInfoLen = errInfoLen;

        // errInfo
        if (offset + static_cast<size_t>(errInfoLen) > static_cast<size_t>(pkLen)) {
            parseFailed = true;
            goto parse_done;
        }
        pb->m_errInfo = std::string(frameRaw + offset, static_cast<size_t>(errInfoLen));
        offset += static_cast<size_t>(errInfoLen);

        // pbData：没有独立的长度字段，剩余字节 = pkLen - offset - 4(checkNum) - 1(PB_END)
        {
            size_t pbDataLen = static_cast<size_t>(pkLen) - offset - 4 - 1;
            pb->m_pbData = std::string(frameRaw + offset, pbDataLen);
            offset += pbDataLen;
        }

        // checkNum
        {
            int32_t checkNum = 0;
            if (!readInt32(frameRaw, pkLen, offset, &checkNum)) {
                parseFailed = true;
                goto parse_done;
            }
            pb->m_checkNum = checkNum;
        }

parse_done:
        if (parseFailed) {
            // 字段解析失败，跳过此候选，继续向后扫描
            scanPos = startOffset + 1;
            continue;
        }

        // ---- 成功：回填 pkLen，消费 buffer，设置状态 ----
        // 一次性消费 startOffset（前置无效字节+被跳过的坏候选）+ pkLen（合法帧）
        pb->m_pkLen = pkLen;
        pb->m_decodeSucc = true;
        buffer->retrieve(startOffset + static_cast<size_t>(pkLen));
        return;
    }
}

bool TinyPbCodec::findFrameStart(const char *base, size_t readable, size_t from, size_t *start)
{
    // 从 base[from..readable-1] 中查找第一个 kTinyPbStart (0x02)。
    // *start 返回相对于 base 起始的绝对偏移。
    for (size_t i = from; i < readable; ++i) {
        if (static_cast<unsigned char>(base[i]) == kTinyPbStart) {
            *start = i;
            return true;
        }
    }
    return false;
}

bool TinyPbCodec::isValidPackageLength(int32_t pkLen)
{
    return pkLen >= kTinyPbMinPackageLength && pkLen <= kTinyPbMaxPackageLength;
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
