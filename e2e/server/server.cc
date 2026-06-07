/*
 * server.cc -- E2E 联动测试服务端。
 *
 * 启动 TcpServer，注册 E2eServiceImpl（echo + add），等待客户端连接。
 *
 * 运行方式：
 *   e2e_server <config.xml>    ：按配置启动 RPC 服务端
 *   e2e_server --probe <port>  ：探测端口是否就绪
 */

#include "comm/start.h"
#include "net/netaddress.h"
#include "net/tcpclient.h"

#include "e2e_rpc.pb.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

constexpr const char *kHost = "127.0.0.1";

/*
 * E2eServiceImpl -- 实现 E2eService 的两个 RPC 方法。
 *
 * echo：返回请求序列号和 payload 回显字符串。
 * add：计算 lhs + rhs，返回 64 位结果。
 */
class E2eServiceImpl : public E2eService {
 public:
    void echo(
        google::protobuf::RpcController * /*controller*/,
        const E2eEchoRequest *request,
        E2eEchoResponse *response,
        google::protobuf::Closure *done) override
    {
        response->set_ret_code(0);
        response->set_ret_msg("e2e ok");
        response->set_seq(request->seq());
        response->set_echo("server: " + request->payload());

        if (done != nullptr) {
            done->Run();
        }
    }

    void add(
        google::protobuf::RpcController * /*controller*/,
        const E2eAddRequest *request,
        E2eAddResponse *response,
        google::protobuf::Closure *done) override
    {
        response->set_ret_code(0);
        response->set_ret_msg("e2e ok");
        response->set_seq(request->seq());
        response->set_result(static_cast<int64_t>(request->lhs()) + request->rhs());

        if (done != nullptr) {
            done->Run();
        }
    }
};

/*
 * parsePort -- 将字符串解析为有效的 TCP 端口号（1~65535）。
 * text：待解析字符串；port：输出参数。成功返回 true。
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
 * runServer -- 使用阶段 18 启动门面创建 TcpServer，注册 E2eServiceImpl 后启动事件循环。
 * configPath：分组式 XML 配置路径。
 */
int runServer(const std::string& configPath)
{
    if (!tinyrpc::InitConfig(configPath)) {
        std::cerr << "[e2e-server] load config failed: " << configPath
                  << ", error: " << tinyrpc::GetConstConfig().getLastError() << std::endl;
        return 1;
    }

    if (!tinyrpc::StartRpcServer()) {
        std::cerr << "[e2e-server] start rpc server failed" << std::endl;
        return 1;
    }

    if (!REGISTER_SERVICE(E2eServiceImpl)) {
        std::cerr << "[e2e-server] register service failed" << std::endl;
        return 1;
    }

    auto server = tinyrpc::GetServer();
    if (server == nullptr) {
        std::cerr << "[e2e-server] server is null" << std::endl;
        return 1;
    }

    std::cout << "[e2e-server] listen " << server->getLocalAddress().getPort() << std::endl;
    server->start();
    return 0;
}

/*
 * runProbe -- 尝试建立 TCP 连接，用于脚本等待端口就绪。
 * port：目标端口。
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
