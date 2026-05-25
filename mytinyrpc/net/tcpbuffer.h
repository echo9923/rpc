#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace tinyrpc {

// TcpBuffer 是一个带读写下标管理的缓冲区类。
// 内部使用 std::vector<char> 存储数据，通过 m_readIndex 和 m_writeIndex
// 追踪已读/未读数据的边界，避免频繁的数据搬移。
// 为后续 HTTP/TinyPB 协议拆包和 RPC 编解码提供基础。
class TcpBuffer {
 public:
    // 构造缓冲区，指定初始容量。
    // 参数 initialSize 控制内部 vector 的初始大小，默认 1024 字节。
    explicit TcpBuffer(size_t initialSize = 1024);

    // 返回当前可读字节数（m_writeIndex - m_readIndex）。
    size_t getReadableBytes() const;

    // 返回当前尾部可写字节数（m_buffer.size() - m_writeIndex）。
    size_t getWritableBytes() const;

    // 返回已被消费的头部空间（m_readIndex），可被 memmove 回收利用。
    size_t getPrependableBytes() const;

    // 返回指向可读数据起始位置的只读指针。
    // 调用方不应通过该指针修改缓冲区内容。
    const char* getReadPtr() const;

    // 向缓冲区追加原始字节数据。
    // 内部自动调用 ensureWritableBytes(len) 确保有足够空间。
    void append(const char* data, size_t len);

    // 向缓冲区追加 std::string 数据，委托到 append(const char*, size_t)。
    void append(const std::string& data);

    // 消费（丢弃）缓冲区前 len 个可读字节。
    // 若 len >= getReadableBytes()，等价于 retrieveAll()。
    void retrieve(size_t len);

    // 消费全部可读数据，将读写指针均重置为 0。
    void retrieveAll();

    // 从可读数据中取出前 len 字节作为 std::string，并消费这些字节。
    // 返回的 string 是独立拷贝，不持有缓冲区内部指针。
    std::string retrieveAsString(size_t len);

    // 取出全部可读数据作为 std::string，并重置缓冲区。
    std::string retrieveAllAsString();

    // 确保尾部可写空间不少于 len 字节。
    // 优先通过 memmove 回收已消费的头部空间；
    // 若仍不足，则通过 vector::resize 扩容。
    void ensureWritableBytes(size_t len);

 private:
    std::vector<char> m_buffer;
    size_t m_readIndex {0};
    size_t m_writeIndex {0};
};

}
