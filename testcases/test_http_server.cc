/*
 * test_http_server.cc -- 任务六十二：HTTP server 端到端验收程序。
 *
 * 运行模式：
 *   --server <port> ：启动最小 HTTP TcpServer，注册 /hello servlet。
 */

#include "net/http/httprequest.h"
#include "net/http/httpresponse.h"
#include "net/http/httpcodec.h"
#include "net/http/httpdispatcher.h"
#include "net/http/httpservlet.h"
#include "net/netaddress.h"
#include "net/tcpserver.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {

constexpr const char *kHost = "127.0.0.1";

class HelloServlet : public tinyrpc::HttpServlet {
 public:
    void handle(tinyrpc::HttpRequest *request, tinyrpc::HttpResponse *response) override
    {
        // request 当前只用于确认路由命中，响应内容保持固定便于脚本验收。
        (void)request;
        response->setStatusCode(tinyrpc::HttpStatusCode::OK);
        response->setHeader("Content-Type", "text/plain");
        response->setBody("hello http");
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
    auto codec = std::make_shared<tinyrpc::HttpCodec>();
    auto dispatcher = std::make_shared<tinyrpc::HttpDispatcher>();
    if (!dispatcher->registerServlet("/hello", std::make_shared<HelloServlet>())) {
        std::cerr << "[stage12-server] register /hello failed" << std::endl;
        return 1;
    }

    tinyrpc::TcpServer server(tinyrpc::IPAddress(kHost, port), codec, dispatcher);
    if (!server.init()) {
        std::cerr << "[stage12-server] init failed" << std::endl;
        return 1;
    }

    std::cout << "[stage12-server] listen " << port << std::endl;
    server.start();
    return 0;
}

void printUsage(const char *program)
{
    std::cerr << "usage: " << program << " --server <port>" << std::endl;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 3) {
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
        return runServer(port);
    }

    printUsage(argv[0]);
    return 1;
}
