#include "comm/log.h"
#include "net/tcpbuffer.h"
#include "net/tinypb/tinypbcodec.h"
#include "net/tinypb/tinypbdata.h"
#include "net/tinypb/tinypbrpcchannel.h"
#include "net/tinypb/tinypbrpccontroller.h"
#include "test_tinypb_server.pb.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cerrno>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>

namespace {

constexpr int kRequestCount = 32;

void closeIfValid(int *fd)
{
    if (fd != nullptr && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

bool writeAllToFd(int fd, const char *data, size_t len, std::string *errorInfo)
{
    size_t written = 0;
    while (written < len) {
        // write(2) 参数依次为 socket fd、待写缓冲区地址、待写字节数。
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
    tinyrpc::TcpBuffer buffer(1024);
    char data[1024];

    while (true) {
        codec.decode(&buffer, pb);
        if (pb->m_decodeSucc) {
            return true;
        }

        // read(2) 参数依次为 socket fd、接收缓冲区地址、最多读取字节数。
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
    tinyrpc::TcpBuffer buffer(1024);
    codec.encode(&buffer, pb);
    if (!pb->m_encodeSucc) {
        return false;
    }
    *frame = buffer.retrieveAllAsString();
    return true;
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
    // setsockopt(2) 参数依次为 fd、协议层、选项名、选项值地址、选项长度。
    setsockopt(*listenFd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);

    // bind(2) 将监听 fd 绑定到 loopback 的临时端口。
    if (bind(*listenFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        *errorInfo = std::strerror(errno);
        closeIfValid(listenFd);
        return false;
    }

    // listen(2) 将 socket 切换为监听状态，backlog 允许 benchmark 请求排队。
    if (listen(*listenFd, 64) != 0) {
        *errorInfo = std::strerror(errno);
        closeIfValid(listenFd);
        return false;
    }

    sockaddr_in actualAddr {};
    socklen_t len = sizeof(actualAddr);
    // getsockname(2) 读取内核分配的实际监听端口。
    if (getsockname(*listenFd, reinterpret_cast<sockaddr *>(&actualAddr), &len) != 0) {
        *errorInfo = std::strerror(errno);
        closeIfValid(listenFd);
        return false;
    }
    *port = ntohs(actualAddr.sin_port);
    return true;
}

bool runServer(int listenFd, int requestCount, std::string *errorInfo)
{
    for (int i = 0; i < requestCount; ++i) {
        // accept(2) 参数依次为监听 fd、客户端地址输出缓冲、地址长度指针；这里不需要地址。
        int clientFd = accept(listenFd, nullptr, nullptr);
        if (clientFd < 0) {
            *errorInfo = std::strerror(errno);
            return false;
        }

        tinyrpc::TinyPbStruct decodedRequest;
        if (!readTinyPbFromFd(clientFd, &decodedRequest, errorInfo)) {
            closeIfValid(&clientFd);
            return false;
        }

        queryNameReq pbReq;
        if (!pbReq.ParseFromString(decodedRequest.m_pbData)) {
            *errorInfo = "request parse failed";
            closeIfValid(&clientFd);
            return false;
        }

        queryNameRes pbRes;
        pbRes.set_ret_code(0);
        pbRes.set_res_info("benchmark ok");
        pbRes.set_req_no(pbReq.req_no());
        pbRes.set_id(pbReq.id());
        pbRes.set_name("sync-" + std::to_string(pbReq.id()));

        tinyrpc::TinyPbStruct response;
        response.m_reqId = decodedRequest.m_reqId;
        response.m_serviceFullName = decodedRequest.m_serviceFullName;
        if (!pbRes.SerializeToString(&response.m_pbData)) {
            *errorInfo = "response serialize failed";
            closeIfValid(&clientFd);
            return false;
        }

        std::string frame;
        if (!encodeTinyPbToString(&response, &frame)
            || !writeAllToFd(clientFd, frame.data(), frame.size(), errorInfo)) {
            closeIfValid(&clientFd);
            return false;
        }
        closeIfValid(&clientFd);
    }
    return true;
}

}  // namespace

int main()
{
    tinyrpc::Logger::setEnabled(false);

    int listenFd = -1;
    uint16_t port = 0;
    std::string errorInfo;
    if (!createListenSocket(&listenFd, &port, &errorInfo)) {
        std::cerr << "[benchmark][tinypb_sync] FAIL: " << errorInfo << std::endl;
        return 1;
    }

    bool serverOk = false;
    std::thread serverThread([&]() {
        serverOk = runServer(listenFd, kRequestCount, &errorInfo);
    });

    tinyrpc::TinyPbRpcChannel channel(tinyrpc::IPAddress("127.0.0.1", port));
    int nextReqId = 0;
    channel.setReqIdGenerator([&]() {
        return "bench-sync-" + std::to_string(nextReqId++);
    });
    QueryService_Stub stub(&channel);

    auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < kRequestCount; ++i) {
        queryNameReq request;
        request.set_req_no(1000 + i);
        request.set_id(2000 + i);
        request.set_type(1);

        queryNameRes response;
        tinyrpc::TinyPbRpcController controller;
        controller.setTimeout(1000);
        stub.query_name(&controller, &request, &response, nullptr);
        if (controller.Failed() || response.name() != "sync-" + std::to_string(2000 + i)) {
            errorInfo = controller.Failed() ? controller.ErrorText() : "unexpected sync response";
            break;
        }
    }
    auto end = std::chrono::steady_clock::now();

    serverThread.join();
    closeIfValid(&listenFd);
    tinyrpc::Logger::setEnabled(true);

    if (!serverOk || !errorInfo.empty()) {
        std::cerr << "[benchmark][tinypb_sync] FAIL: " << errorInfo << std::endl;
        return 1;
    }

    double totalMs = std::chrono::duration<double, std::milli>(end - begin).count();
    double avgUs = totalMs * 1000.0 / static_cast<double>(kRequestCount);
    std::cout << std::fixed << std::setprecision(2)
              << "[benchmark][tinypb_sync] requests=" << kRequestCount
              << " total_ms=" << totalMs
              << " avg_us=" << avgUs
              << std::endl;
    return 0;
}
