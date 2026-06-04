/*
 * client.cc -- E2E 联动测试客户端。
 *
 * 从客户端角度验证 RPC 框架端到端链路：
 *   1. 同步 echo 调用
 *   2. 同步 add 调用
 *   3. 异步 echo 调用
 *   4. 异步 add 调用
 *
 * 运行方式：
 *   e2e_client <port>
 */

#include "comm/errorcode.h"
#include "net/netaddress.h"
#include "net/tinypb/tinypbrpcasyncchannel.h"
#include "net/tinypb/tinypbrpcchannel.h"
#include "net/tinypb/tinypbrpccontroller.h"

#include "e2e_rpc.pb.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include <atomic>
#include <chrono>
#include <thread>

namespace {

constexpr const char *kHost = "127.0.0.1";

/*
 * parsePort -- 将字符串解析为有效的 TCP 端口号（1~65535）。
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
 * FlagClosure -- 记录 done 回调是否被执行的简单 Closure 实现。
 */
class FlagClosure : public google::protobuf::Closure {
 public:
    explicit FlagClosure(bool *flag)
        : m_flag(flag)
    {
    }

    void Run() override
    {
        *m_flag = true;
    }

 private:
    bool *m_flag {nullptr};
};

/*
 * waitUntil -- 轮询等待谓词为 true，超时返回 false。
 * timeoutMs：超时毫秒数。
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
 * testSyncEcho -- 使用同步 Channel 调用 E2eService::echo。
 * 验证响应字段 ret_code、seq、echo 回显内容。
 */
bool testSyncEcho(uint16_t port)
{
    tinyrpc::TinyPbRpcChannel channel(tinyrpc::IPAddress(kHost, port));
    E2eService_Stub stub(&channel);

    E2eEchoRequest request;
    request.set_seq(1);
    request.set_payload("hello e2e");

    E2eEchoResponse response;
    tinyrpc::TinyPbRpcController controller;

    stub.echo(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        std::cerr << "[e2e-client] sync echo failed: " << controller.ErrorText() << std::endl;
        return false;
    }

    if (response.ret_code() != 0
        || response.seq() != 1
        || response.echo() != "server: hello e2e") {
        std::cerr << "[e2e-client] sync echo unexpected: " << response.ShortDebugString() << std::endl;
        return false;
    }

    std::cout << "[e2e-client] sync echo PASS" << std::endl;
    return true;
}

/*
 * testSyncAdd -- 使用同步 Channel 调用 E2eService::add。
 * 验证响应字段 ret_code、seq、result = lhs + rhs。
 */
bool testSyncAdd(uint16_t port)
{
    tinyrpc::TinyPbRpcChannel channel(tinyrpc::IPAddress(kHost, port));
    E2eService_Stub stub(&channel);

    E2eAddRequest request;
    request.set_seq(2);
    request.set_lhs(42);
    request.set_rhs(58);

    E2eAddResponse response;
    tinyrpc::TinyPbRpcController controller;

    stub.add(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        std::cerr << "[e2e-client] sync add failed: " << controller.ErrorText() << std::endl;
        return false;
    }

    if (response.ret_code() != 0
        || response.seq() != 2
        || response.result() != 100) {
        std::cerr << "[e2e-client] sync add unexpected: " << response.ShortDebugString() << std::endl;
        return false;
    }

    std::cout << "[e2e-client] sync add PASS" << std::endl;
    return true;
}

/*
 * testAsyncEcho -- 使用异步 Channel 调用 E2eService::echo。
 * 验证 done 回调被执行，响应字段正确。
 */
bool testAsyncEcho(uint16_t port)
{
    tinyrpc::TinyPbRpcAsyncChannel channel(tinyrpc::IPAddress(kHost, port));
    E2eService_Stub stub(&channel);

    E2eEchoRequest request;
    request.set_seq(3);
    request.set_payload("async hello");

    E2eEchoResponse response;
    tinyrpc::TinyPbRpcController controller;
    controller.setTimeout(3000);

    bool doneCalled = false;
    FlagClosure done(&doneCalled);

    stub.echo(&controller, &request, &response, &done);

    if (!waitUntil([&]() { return doneCalled; }, 5000)) {
        std::cerr << "[e2e-client] async echo timeout" << std::endl;
        return false;
    }

    if (controller.Failed()) {
        std::cerr << "[e2e-client] async echo failed: " << controller.ErrorText() << std::endl;
        return false;
    }

    if (response.ret_code() != 0
        || response.seq() != 3
        || response.echo() != "server: async hello") {
        std::cerr << "[e2e-client] async echo unexpected: " << response.ShortDebugString() << std::endl;
        return false;
    }

    std::cout << "[e2e-client] async echo PASS" << std::endl;
    return true;
}

/*
 * testAsyncAdd -- 使用异步 Channel 调用 E2eService::add。
 * 验证 done 回调被执行，result = lhs + rhs。
 */
bool testAsyncAdd(uint16_t port)
{
    tinyrpc::TinyPbRpcAsyncChannel channel(tinyrpc::IPAddress(kHost, port));
    E2eService_Stub stub(&channel);

    E2eAddRequest request;
    request.set_seq(4);
    request.set_lhs(123);
    request.set_rhs(456);

    E2eAddResponse response;
    tinyrpc::TinyPbRpcController controller;
    controller.setTimeout(3000);

    bool doneCalled = false;
    FlagClosure done(&doneCalled);

    stub.add(&controller, &request, &response, &done);

    if (!waitUntil([&]() { return doneCalled; }, 5000)) {
        std::cerr << "[e2e-client] async add timeout" << std::endl;
        return false;
    }

    if (controller.Failed()) {
        std::cerr << "[e2e-client] async add failed: " << controller.ErrorText() << std::endl;
        return false;
    }

    if (response.ret_code() != 0
        || response.seq() != 4
        || response.result() != 579) {
        std::cerr << "[e2e-client] async add unexpected: " << response.ShortDebugString() << std::endl;
        return false;
    }

    std::cout << "[e2e-client] async add PASS" << std::endl;
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <port>" << std::endl;
        return 1;
    }

    uint16_t port = 0;
    if (!parsePort(argv[1], &port)) {
        std::cerr << "invalid port: " << argv[1] << std::endl;
        return 1;
    }

    if (!testSyncEcho(port)) return 1;
    if (!testSyncAdd(port)) return 1;
    if (!testAsyncEcho(port)) return 1;
    if (!testAsyncAdd(port)) return 1;

    std::cout << "[e2e-client] PASS" << std::endl;
    return 0;
}
