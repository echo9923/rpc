#include "net/tcpbuffer.h"

#include <cstring>

namespace tinyrpc {

TcpBuffer::TcpBuffer(size_t initialSize)
    : m_buffer(initialSize)
{
}

size_t TcpBuffer::getReadableBytes() const
{
    return m_writeIndex - m_readIndex;
}

size_t TcpBuffer::getWritableBytes() const
{
    return m_buffer.size() - m_writeIndex;
}

size_t TcpBuffer::getPrependableBytes() const
{
    return m_readIndex;
}

const char* TcpBuffer::getReadPtr() const
{
    return m_buffer.data() + m_readIndex;
}

void TcpBuffer::append(const char* data, size_t len)
{
    if (len == 0) {
        return;
    }

    ensureWritableBytes(len);

    // 将数据拷贝到 m_writeIndex 位置
    // std::memcpy: void* memcpy(void* dest, const void* src, size_t n)
    // 将 src 指向的前 n 个字节复制到 dest，dest 和 src 不能重叠
    std::memcpy(m_buffer.data() + m_writeIndex, data, len);
    m_writeIndex += len;
}

void TcpBuffer::append(const std::string& data)
{
    append(data.data(), data.size());
}

void TcpBuffer::retrieve(size_t len)
{
    if (len >= getReadableBytes()) {
        retrieveAll();
    } else {
        m_readIndex += len;
    }
}

void TcpBuffer::retrieveAll()
{
    m_readIndex = 0;
    m_writeIndex = 0;
}

std::string TcpBuffer::retrieveAsString(size_t len)
{
    if (len > getReadableBytes()) {
        len = getReadableBytes();
    }

    std::string result(getReadPtr(), len);
    retrieve(len);
    return result;
}

std::string TcpBuffer::retrieveAllAsString()
{
    std::string result(getReadPtr(), getReadableBytes());
    retrieveAll();
    return result;
}

void TcpBuffer::ensureWritableBytes(size_t len)
{
    if (getWritableBytes() >= len) {
        return;
    }

    // 尾部空间不足，尝试回收已被消费的头部空间
    if (getPrependableBytes() + getWritableBytes() >= len) {
        // 将可读数据搬到缓冲区开头
        size_t readable = getReadableBytes();
        // std::memmove: void* memmove(void* dest, const void* src, size_t n)
        // 将 src 指向的前 n 个字节复制到 dest，即使 src 和 dest 重叠也能正确处理
        std::memmove(m_buffer.data(), getReadPtr(), readable);
        m_readIndex = 0;
        m_writeIndex = readable;
        return;
    }

    // 回收头部空间后仍不足，需要扩容
    m_buffer.resize(m_writeIndex + len);
}

}
