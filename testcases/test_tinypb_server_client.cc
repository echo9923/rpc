/*
 * test_tinypb_server_client.cc -- 任务三十九：真实 Stub 到 TcpServer 端到端验收程序。
 *
 * 运行模式：
 *   --server <port>                 ：启动单 Reactor TcpServer，注册 QueryServiceImpl。
 *   --server-multi <port> <threads> ：启动 Main Reactor + Sub Reactor TcpServer。
 *   --client <port>                 ：使用 QueryService_Stub + TinyPbRpcChannel 发起一次真实 RPC。
 *   --probe <port>                  ：尝试建立 TCP 连接，用于脚本等待端口就绪。
 */

#include "net/netaddress.h"
#include "net/tcpclient.h"
#include "net/tcpserver.h"
#include "net/tinypb/tinypbcodec.h"
#include "net/tinypb/tinypbdispatcher.h"
#include "net/tinypb/tinypbrpcchannel.h"
#include "net/tinypb/tinypbrpccontroller.h"
#include "test_tinypb_server.pb.h"

#include <google/protobuf/service.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {

constexpr const char *kHost = "127.0.0.1";

class QueryServiceImpl : public QueryService {
 public:
    void query_name(
        google::protobuf::RpcController * /*controller*/,
        const queryNameReq *request,
        queryNameRes *response,
        google::protobuf::Closure *done) override
    {
        response->set_ret_code(0);
        response->set_res_info("stage8 rpc ok");
        response->set_req_no(request->req_no());
        response->set_id(request->id());
        response->set_name("Alice");

        if (done != nullptr) {
            done->Run();
        }
    }
};

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

int runServer(uint16_t port)
{
    auto codec = std::make_shared<tinyrpc::TinyPbCodec>();
    auto dispatcher = std::make_shared<tinyrpc::TinyPbDispatcher>();
    tinyrpc::TcpServer server(tinyrpc::IPAddress(kHost, port), codec, dispatcher);

    if (!server.init()) {
        std::cerr << "[stage8-server] init failed" << std::endl;
        return 1;
    }

    if (!server.registerService(std::make_shared<QueryServiceImpl>())) {
        std::cerr << "[stage8-server] register service failed" << std::endl;
        return 1;
    }

    std::cout << "[stage8-server] listen " << port << std::endl;
    server.start();
    return 0;
}

int runMultiServer(uint16_t port, int ioThreadNum)
{
    auto codec = std::make_shared<tinyrpc::TinyPbCodec>();
    auto dispatcher = std::make_shared<tinyrpc::TinyPbDispatcher>();
    tinyrpc::TcpServer server(tinyrpc::IPAddress(kHost, port), codec, dispatcher);
    server.setIOThreadNum(ioThreadNum);

    if (!server.init()) {
        std::cerr << "[stage11-server] init failed" << std::endl;
        return 1;
    }

    if (!server.registerService(std::make_shared<QueryServiceImpl>())) {
        std::cerr << "[stage11-server] register service failed" << std::endl;
        return 1;
    }

    std::cout << "[stage11-server] listen " << port
              << ", io_threads = " << ioThreadNum << std::endl;
    server.start();
    return 0;
}

int runClient(uint16_t port)
{
    tinyrpc::TinyPbRpcChannel channel(tinyrpc::IPAddress(kHost, port));
    QueryService_Stub stub(&channel);

    queryNameReq request;
    request.set_req_no(3901);
    request.set_id(10086);
    request.set_type(1);

    queryNameRes response;
    tinyrpc::TinyPbRpcController controller;

    stub.query_name(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        std::cerr << "[stage8-client] rpc failed, code = " << controller.getErrorCode()
                  << ", error = " << controller.ErrorText() << std::endl;
        return 1;
    }

    if (response.ret_code() != 0
        || response.res_info() != "stage8 rpc ok"
        || response.req_no() != request.req_no()
        || response.id() != request.id()
        || response.name() != "Alice") {
        std::cerr << "[stage8-client] unexpected response: "
                  << response.ShortDebugString() << std::endl;
        return 1;
    }

    std::cout << "[stage8-client] PASS" << std::endl;
    return 0;
}

int runProbe(uint16_t port)
{
    tinyrpc::TcpClient client(tinyrpc::IPAddress(kHost, port));
    if (!client.connectServer()) {
        return 1;
    }
    client.closeConnection();
    return 0;
}

void printUsage(const char *program)
{
    std::cerr << "usage: " << program
              << " --server|--client|--probe <port> | --server-multi <port> <threads>"
              << std::endl;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 3 && argc != 4) {
        printUsage(argv[0]);
        return 1;
    }

    uint16_t port = 0;
    if (!parsePort(argv[2], &port)) {
        std::cerr << "invalid port: " << argv[2] << std::endl;
        return 1;
    }

    std::string mode = argv[1];
    if (mode == "--server") {
        if (argc != 3) {
            printUsage(argv[0]);
            return 1;
        }
        return runServer(port);
    }
    if (mode == "--server-multi") {
        if (argc != 4) {
            printUsage(argv[0]);
            return 1;
        }
        char *end = nullptr;
        long value = std::strtol(argv[3], &end, 10);
        if (*argv[3] == '\0' || *end != '\0' || value <= 0 || value > 64) {
            std::cerr << "invalid io thread num: " << argv[3] << std::endl;
            return 1;
        }
        return runMultiServer(port, static_cast<int>(value));
    }
    if (mode == "--client") {
        if (argc != 3) {
            printUsage(argv[0]);
            return 1;
        }
        return runClient(port);
    }
    if (mode == "--probe") {
        if (argc != 3) {
            printUsage(argv[0]);
            return 1;
        }
        return runProbe(port);
    }

    printUsage(argv[0]);
    return 1;
}
