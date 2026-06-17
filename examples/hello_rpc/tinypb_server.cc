/*
 * tinypb_server.cc -- mytinyrpc 使用示例：TinyPB（TinyRPC）协议服务端。
 *
 * 演示如何用框架的启动门面（mytinyrpc/comm/start.h）驱动一个 RPC 服务端：
 *   InitConfig(配置) -> StartRpcServer() -> REGISTER_SERVICE(实现) -> GetServer()->start()
 *
 * 注意：一个进程只能承载一种协议（由配置里的 <protocol> 决定）。
 * 本示例使用 tinypb 协议；HTTP 协议见同目录的 http_server.cc。
 *
 * 运行方式：
 *   hello_tinypb_server <config.xml>    ：按配置启动 TinyPB RPC 服务端
 *   hello_tinypb_server --probe <port>  ：探测端口是否就绪（供脚本轮询）
 */

#include "comm/start.h"
#include "net/netaddress.h"
#include "net/tcpclient.h"

#include "hello_rpc.pb.h"

#include <google/protobuf/service.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

constexpr const char *kHost = "127.0.0.1";

/*
 * HelloServiceImpl -- 实现 HelloService::hello。
 * 把请求的 name 拼成问候语回填到 response，完成后调用 done->Run()。
 */
class HelloServiceImpl : public HelloService {
 public:
    void hello(
        google::protobuf::RpcController * /*controller*/,
        const HelloReq *request,
        HelloRes *response,
        google::protobuf::Closure *done) override
    {
        response->set_ret_code(0);
        response->set_res_info("hello rpc ok");
        response->set_message("hello " + request->name()
                              + " (id=" + std::to_string(request->id()) + ")");

        if (done != nullptr) {
            done->Run();
        }
    }
};

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
 * runServer -- 用启动门面创建 TcpServer，注册 HelloServiceImpl 后进入事件循环。
 */
int runServer(const std::string& configPath)
{
    if (!tinyrpc::InitConfig(configPath)) {
        std::cerr << "[hello-tinypb] load config failed: " << configPath
                  << ", error: " << tinyrpc::GetConstConfig().getLastError() << std::endl;
        return 1;
    }

    if (!tinyrpc::StartRpcServer()) {
        std::cerr << "[hello-tinypb] start rpc server failed" << std::endl;
        return 1;
    }

    if (!REGISTER_SERVICE(HelloServiceImpl)) {
        std::cerr << "[hello-tinypb] register service failed" << std::endl;
        return 1;
    }

    auto server = tinyrpc::GetServer();
    if (server == nullptr) {
        std::cerr << "[hello-tinypb] server is null" << std::endl;
        return 1;
    }

    std::cout << "[hello-tinypb] listen " << server->getLocalAddress().getPort() << std::endl;
    server->start();
    return 0;
}

/*
 * runProbe -- 尝试建立一次 TCP 连接，用于脚本等待端口就绪。
 */
int runProbe(uint16_t port)
{
    tinyrpc::TcpClient client(tinyrpc::IPAddress(kHost, port));
    if (!client.connectServer()) {
        return 1;
    }
    client.closeConnection();
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <config.xml> | --probe <port>" << std::endl;
        return 1;
    }

    std::string mode = argv[1];
    if (mode == "--probe") {
        if (argc < 3) {
            std::cerr << "usage: " << argv[0] << " --probe <port>" << std::endl;
            return 1;
        }
        uint16_t port = 0;
        if (!parsePort(argv[2], &port)) {
            std::cerr << "invalid port: " << argv[2] << std::endl;
            return 1;
        }
        return runProbe(port);
    }

    return runServer(argv[1]);
}
