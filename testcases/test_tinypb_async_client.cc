/*
 * test_tinypb_async_client.cc -- 任务七十八：异步 RPC 客户端脚本验收程序。
 */

#include "comm/errorcode.h"
#include "net/tcpbuffer.h"
#include "net/tinypb/tinypbcodec.h"
#include "net/tinypb/tinypbdata.h"
#include "net/tinypb/tinypbrpcasyncchannel.h"
#include "net/tinypb/tinypbrpccontroller.h"
#include "test_tinypb_server.pb.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

class CountClosure : public google::protobuf::Closure {
 public:
    explicit CountClosure(std::atomic<int> *runCount)
        : m_runCount(runCount)
    {
    }

    void Run() override
    {
        m_runCount->fetch_add(1);
    }

 private:
    std::atomic<int> *m_runCount {nullptr};
};

void closeIfValid(int *fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

bool writeAllToFd(int fd, const char *data, size_t len, std::string *errorInfo)
{
    size_t written = 0;
    while (written < len) {
        // write(2) 参数依次为 socket fd、待写缓冲区、待写字节数。
        ssize_t n = write(fd, data + written, len - written);
        if (n > 0) {
            written += static_cast<size_t>(n);
            continue;
        }
        if (n == 0) {
            *errorInfo = "write returned zero";
            return false;
        }
        if (errno == EINTR) {
            continue;
        }

        *errorInfo = std::strerror(errno);
        return false;
    }
    return true;
}

bool readTinyPbFromFd(int fd, tinyrpc::TinyPbStruct *pb, std::string *errorInfo)
{
    tinyrpc::TinyPbCodec codec;
    tinyrpc::TcpBuffer buffer(256);
    char data[1024];

    while (true) {
        codec.decode(&buffer, pb);
        if (pb->m_decodeSucc) {
            return true;
        }

        // read(2) 参数依次为 socket fd、接收缓冲区、最多读取字节数。
        ssize_t n = read(fd, data, sizeof(data));
        if (n > 0) {
            buffer.append(data, static_cast<size_t>(n));
            continue;
        }
        if (n == 0) {
            *errorInfo = "peer closed before TinyPB frame was complete";
            return false;
        }
        if (errno == EINTR) {
            continue;
        }

        *errorInfo = std::strerror(errno);
        return false;
    }
}

bool encodeTinyPbToString(tinyrpc::TinyPbStruct *pb, std::string *frame)
{
    tinyrpc::TinyPbCodec codec;
    tinyrpc::TcpBuffer buffer(256);
    codec.encode(&buffer, pb);
    if (!pb->m_encodeSucc) {
        return false;
    }
    *frame = buffer.retrieveAllAsString();
    return true;
}

bool waitUntil(const std::function<bool()>& pred, int timeoutMs)
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

bool createListenSocket(int *listenFd, uint16_t *port, std::string *errorInfo)
{
    // socket(2) 参数依次为地址族、socket 类型、协议号；AF_INET + SOCK_STREAM + 0 创建 TCP socket。
    *listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (*listenFd < 0) {
        *errorInfo = std::strerror(errno);
        return false;
    }

    int on = 1;
    setsockopt(*listenFd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);

    // bind(2) 将监听 fd 绑定到 loopback 临时端口。
    if (bind(*listenFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        *errorInfo = std::strerror(errno);
        closeIfValid(listenFd);
        return false;
    }

    // listen(2) 进入 TCP 监听状态，backlog 允许脚本场景中的多个异步连接排队。
    if (listen(*listenFd, 16) != 0) {
        *errorInfo = std::strerror(errno);
        closeIfValid(listenFd);
        return false;
    }

    sockaddr_in actualAddr {};
    socklen_t len = sizeof(actualAddr);
    if (getsockname(*listenFd, reinterpret_cast<sockaddr *>(&actualAddr), &len) != 0) {
        *errorInfo = std::strerror(errno);
        closeIfValid(listenFd);
        return false;
    }

    *port = ntohs(actualAddr.sin_port);
    return true;
}

bool runSuccessAndErrorScenario(std::string *errorInfo)
{
    constexpr int kRequestCount = 6;
    int listenFd = -1;
    uint16_t port = 0;
    if (!createListenSocket(&listenFd, &port, errorInfo)) {
        return false;
    }

    std::thread serverThread([&]() {
        for (int i = 0; i < kRequestCount; ++i) {
            int clientFd = accept(listenFd, nullptr, nullptr);
            if (clientFd < 0) {
                *errorInfo = std::strerror(errno);
                return;
            }

            tinyrpc::TinyPbStruct decodedRequest;
            if (!readTinyPbFromFd(clientFd, &decodedRequest, errorInfo)) {
                closeIfValid(&clientFd);
                return;
            }

            queryNameReq pbReq;
            if (!pbReq.ParseFromString(decodedRequest.m_pbData)) {
                *errorInfo = "request parse failed";
                closeIfValid(&clientFd);
                return;
            }

            tinyrpc::TinyPbStruct response;
            response.m_reqId = decodedRequest.m_reqId;
            response.m_serviceFullName = decodedRequest.m_serviceFullName;
            if (i == kRequestCount - 1) {
                response.m_errCode = tinyrpc::ERROR_SERVICE_NOT_FOUND;
                response.m_errInfo = "async service missing";
            } else {
                queryNameRes pbRes;
                pbRes.set_ret_code(0);
                pbRes.set_res_info("ok");
                pbRes.set_req_no(pbReq.req_no());
                pbRes.set_id(pbReq.id());
                pbRes.set_name("async-client-" + std::to_string(pbReq.id()));
                if (!pbRes.SerializeToString(&response.m_pbData)) {
                    *errorInfo = "response serialize failed";
                    closeIfValid(&clientFd);
                    return;
                }
            }

            std::string frame;
            if (!encodeTinyPbToString(&response, &frame)) {
                *errorInfo = "response encode failed";
                closeIfValid(&clientFd);
                return;
            }
            if (!writeAllToFd(clientFd, frame.data(), frame.size(), errorInfo)) {
                closeIfValid(&clientFd);
                return;
            }
            closeIfValid(&clientFd);
        }
    });

    tinyrpc::TinyPbRpcAsyncChannel channel(tinyrpc::IPAddress("127.0.0.1", port));
    int nextReqId = 0;
    channel.setReqIdGenerator([&]() {
        return "script-async-" + std::to_string(nextReqId++);
    });
    QueryService_Stub stub(&channel);

    std::vector<queryNameReq> requests(kRequestCount);
    std::vector<queryNameRes> responses(kRequestCount);
    std::vector<tinyrpc::TinyPbRpcController> controllers(kRequestCount);
    std::vector<std::atomic<int>> doneCounts(kRequestCount);
    std::vector<std::unique_ptr<CountClosure>> closures;
    closures.reserve(kRequestCount);

    for (int i = 0; i < kRequestCount; ++i) {
        doneCounts[i].store(0);
        requests[i].set_req_no(800 + i);
        requests[i].set_id(2000 + i);
        requests[i].set_type(1);
        controllers[i].setTimeout(1000);
        closures.push_back(std::make_unique<CountClosure>(&doneCounts[i]));
        stub.query_name(&controllers[i], &requests[i], &responses[i], closures.back().get());
    }

    bool allDone = waitUntil([&]() {
        for (int i = 0; i < kRequestCount; ++i) {
            if (doneCounts[i].load() != 1) {
                return false;
            }
        }
        return true;
    }, 3000);

    serverThread.join();
    closeIfValid(&listenFd);

    if (!allDone) {
        *errorInfo = "not all async callbacks finished";
        return false;
    }
    if (channel.getPendingCount() != 0) {
        *errorInfo = "pending map was not empty after async callbacks";
        return false;
    }

    for (int i = 0; i < kRequestCount - 1; ++i) {
        if (controllers[i].Failed()) {
            *errorInfo = controllers[i].ErrorText();
            return false;
        }
        if (responses[i].name() != "async-client-" + std::to_string(2000 + i)) {
            *errorInfo = "unexpected async response name";
            return false;
        }
    }

    if (!controllers.back().Failed() ||
        controllers.back().getErrorCode() != tinyrpc::ERROR_SERVICE_NOT_FOUND) {
        *errorInfo = "server error response was not propagated";
        return false;
    }

    return true;
}

bool runTimeoutScenario(std::string *errorInfo)
{
    tinyrpc::TinyPbRpcAsyncChannel channel(tinyrpc::IPAddress("127.0.0.1", 1));
    channel.setSyncFallbackEnabled(false);
    channel.setReqIdGenerator([]() {
        return "script-timeout";
    });
    QueryService_Stub stub(&channel);

    queryNameReq request;
    request.set_req_no(900);
    request.set_id(3000);
    request.set_type(1);

    queryNameRes response;
    tinyrpc::TinyPbRpcController controller;
    controller.setTimeout(30);
    std::atomic<int> doneCount {0};
    CountClosure done(&doneCount);

    stub.query_name(&controller, &request, &response, &done);
    if (!waitUntil([&]() { return doneCount.load() == 1; }, 1000)) {
        *errorInfo = "timeout callback did not run";
        return false;
    }
    if (!controller.Failed() || controller.getErrorCode() != tinyrpc::ERROR_RPC_ASYNC_TIMEOUT) {
        *errorInfo = "timeout error was not propagated";
        return false;
    }
    if (channel.getPendingCount() != 0) {
        *errorInfo = "pending map was not empty after timeout";
        return false;
    }

    return true;
}

}

int main()
{
    std::string errorInfo;
    if (!runSuccessAndErrorScenario(&errorInfo)) {
        std::cerr << "[tinypb_async_client] FAIL: " << errorInfo << std::endl;
        return 1;
    }
    if (!runTimeoutScenario(&errorInfo)) {
        std::cerr << "[tinypb_async_client] FAIL: " << errorInfo << std::endl;
        return 1;
    }

    std::cout << "[tinypb_async_client] PASS" << std::endl;
    return 0;
}
