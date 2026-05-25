#include "net/tcpbuffer.h"

#include <iostream>
#include <string>

int main()
{
    // 测试 1: 构造 TcpBuffer(8)，初始 getReadableBytes() == 0
    tinyrpc::TcpBuffer buffer(8);
    if (buffer.getReadableBytes() != 0) {
        std::cerr << "[tcp_buffer] FAIL: initial getReadableBytes() != 0, got "
                  << buffer.getReadableBytes() << std::endl;
        return 1;
    }

    // 测试 2: append("hello") 后 getReadableBytes() == 5
    buffer.append("hello");
    if (buffer.getReadableBytes() != 5) {
        std::cerr << "[tcp_buffer] FAIL: after append(\"hello\") getReadableBytes() != 5, got "
                  << buffer.getReadableBytes() << std::endl;
        return 1;
    }

    // 测试 3: retrieveAsString(2) 返回 "he"，剩余内容为 "llo"
    std::string part = buffer.retrieveAsString(2);
    if (part != "he") {
        std::cerr << "[tcp_buffer] FAIL: retrieveAsString(2) expected \"he\", got \""
                  << part << "\"" << std::endl;
        return 1;
    }
    if (buffer.getReadableBytes() != 3) {
        std::cerr << "[tcp_buffer] FAIL: after retrieve(2) getReadableBytes() != 3, got "
                  << buffer.getReadableBytes() << std::endl;
        return 1;
    }

    // 测试 4: append(" world") 后，retrieveAllAsString() 返回 "llo world"
    buffer.append(" world");
    std::string all = buffer.retrieveAllAsString();
    if (all != "llo world") {
        std::cerr << "[tcp_buffer] FAIL: retrieveAllAsString() expected \"llo world\", got \""
                  << all << "\"" << std::endl;
        return 1;
    }
    if (buffer.getReadableBytes() != 0) {
        std::cerr << "[tcp_buffer] FAIL: after retrieveAllAsString() getReadableBytes() != 0, got "
                  << buffer.getReadableBytes() << std::endl;
        return 1;
    }

    // 测试 5: 小容量 buffer 追加超过初始容量的数据，能自动扩容并完整取回
    tinyrpc::TcpBuffer smallBuffer(4);
    smallBuffer.append("hello world");
    std::string expanded = smallBuffer.retrieveAllAsString();
    if (expanded != "hello world") {
        std::cerr << "[tcp_buffer] FAIL: auto-expand expected \"hello world\", got \""
                  << expanded << "\"" << std::endl;
        return 1;
    }

    // 测试 6: 分段追加数据后，一次性取出全部可读内容
    tinyrpc::TcpBuffer inputBuffer(4);
    inputBuffer.append("hello");
    inputBuffer.append(" ");
    inputBuffer.append("tinyrpc");
    std::string inputData = inputBuffer.retrieveAllAsString();
    if (inputData != "hello tinyrpc") {
        std::cerr << "[tcp_buffer] FAIL: segmented append expected \"hello tinyrpc\", got \""
                  << inputData << "\"" << std::endl;
        return 1;
    }
    if (inputBuffer.getReadableBytes() != 0) {
        std::cerr << "[tcp_buffer] FAIL: after segmented retrieveAllAsString() getReadableBytes() != 0, got "
                  << inputBuffer.getReadableBytes() << std::endl;
        return 1;
    }

    std::cout << "[tcp_buffer] PASS" << std::endl;
    return 0;
}
