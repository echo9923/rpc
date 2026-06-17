/*
 * tinypb_client.cc -- mytinyrpc 使用示例：TinyPB（TinyRPC）协议客户端。
 *
 * 演示框架的两种 RPC 调用方式：
 *   同步：TinyPbRpcChannel + HelloService_Stub，stub.hello() 调用即同步返回结果。
 *   异步：TinyPbRpcAsyncChannel + Stub + Closure，调用立即返回，
 *         响应到达后在 done 回调里处理（内部由 reqId pending 表匹配）。
 *
 * 运行方式：
 *   hello_tinypb_client <port>           ：依次执行同步、异步调用
 *   hello_tinypb_client --sync <port>    ：仅同步调用
 *   hello_tinypb_client --async <port>   ：仅异步调用
 */

#include "net/netaddress.h"
#include "net/tinypb/tinypbrpcasyncchannel.h"
#include "net/tinypb/tinypbrpcchannel.h"
#include "net/tinypb/tinypbrpccontroller.h"

#include "hello_rpc.pb.h"

#include <google/protobuf/service.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

namespace {

constexpr const char *kHost = "127.0.0.1";

/*
 * parsePort -- 将字符串解析为有效的 TCP 端口号（1~65535）。成功返回 true。
 */
bool parsePort(const char *text, uint16_t *port)
{
    if (text == nullptr || port == nullptr) {
        return false;
    }

    char *end = nullptr;
    long value = std::strtol(text, &end, 10);
    if (*text == '\0' || *end != '\0' || value <= 0 || value > 65535) {
        return false;
    }

    *port = static_cast<uint16_t>(value);
    return true;
}

/*
 * FlagClosure -- 记录 done 回调是否被执行的简单 Closure 实现（栈对象）。
 */
class FlagClosure : public google::protobuf::Closure {
 public:
    explicit FlagClosure(std::atomic<bool> *flag)
        : m_flag(flag)
    {
    }

    void Run() override
    {
        m_flag->store(true);
    }

 private:
    std::atomic<bool> *m_flag {nullptr};
};

/*
 * waitUntil -- 轮询等待谓词为 true，超时返回 false。
 */
bool waitUntil(const std::function<bool()> &pred, int timeoutMs)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

/*
 * testSyncHello -- 使用同步 Channel 调用 HelloService::hello。
 * stub.hello() 返回即拿到响应，校验 message 字段。
 */
bool testSyncHello(uint16_t port)
{
    tinyrpc::TinyPbRpcChannel channel(tinyrpc::IPAddress(kHost, port));
    HelloService_Stub stub(&channel);

    HelloReq request;
    request.set_id(10086);
    request.set_name("sync rpc");

    HelloRes response;
    tinyrpc::TinyPbRpcController controller;

    stub.hello(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        std::cerr << "[hello-tinypb] sync hello failed: " << controller.ErrorText() << std::endl;
        return false;
    }

    if (response.ret_code() != 0 || response.message() != "hello sync rpc (id=10086)") {
        std::cerr << "[hello-tinypb] sync hello unexpected: " << response.ShortDebugString() << std::endl;
        return false;
    }

    std::cout << "[hello-tinypb] sync hello PASS: " << response.message() << std::endl;
    return true;
}

/*
 * testAsyncHello -- 使用异步 Channel 调用 HelloService::hello。
 * 投递请求后通过 waitUntil 等待 done 回调执行，再读取 response。
 */
bool testAsyncHello(uint16_t port)
{
    tinyrpc::TinyPbRpcAsyncChannel channel(tinyrpc::IPAddress(kHost, port));
    HelloService_Stub stub(&channel);

    HelloReq request;
    request.set_id(20020);
    request.set_name("async rpc");

    HelloRes response;
    tinyrpc::TinyPbRpcController controller;
    controller.setTimeout(3000);

    std::atomic<bool> doneCalled {false};
    FlagClosure done(&doneCalled);

    stub.hello(&controller, &request, &response, &done);

    if (!waitUntil([&]() { return doneCalled.load(); }, 5000)) {
        std::cerr << "[hello-tinypb] async hello timeout" << std::endl;
        return false;
    }

    if (controller.Failed()) {
        std::cerr << "[hello-tinypb] async hello failed: " << controller.ErrorText() << std::endl;
        return false;
    }

    if (response.ret_code() != 0 || response.message() != "hello async rpc (id=20020)") {
        std::cerr << "[hello-tinypb] async hello unexpected: " << response.ShortDebugString() << std::endl;
        return false;
    }

    std::cout << "[hello-tinypb] async hello PASS: " << response.message() << std::endl;
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3) {
        std::cerr << "usage: " << argv[0] << " [--sync|--async] <port>" << std::endl;
        return 1;
    }

    std::string mode = "all";
    int portArg = 1;
    if (argc == 3) {
        mode = argv[1];
        portArg = 2;
    }

    uint16_t port = 0;
    if (!parsePort(argv[portArg], &port)) {
        std::cerr << "invalid port: " << argv[portArg] << std::endl;
        return 1;
    }

    if (mode == "all") {
        if (!testSyncHello(port)) return 1;
        if (!testAsyncHello(port)) return 1;
    } else if (mode == "--sync") {
        if (!testSyncHello(port)) return 1;
    } else if (mode == "--async") {
        if (!testAsyncHello(port)) return 1;
    } else {
        std::cerr << "unknown mode: " << mode << std::endl;
        return 1;
    }

    std::cout << "[hello-tinypb] client PASS" << std::endl;
    return 0;
}
