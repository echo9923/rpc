/*
 * http_server.cc -- mytinyrpc 使用示例：HTTP 协议服务端。
 *
 * 演示 HTTP 协议的手工装配方式（区别于 tinypb_server.cc 的配置驱动）：
 *   HttpCodec + HttpDispatcher -> registerServlet 注册多个路由
 *   -> TcpServer(addr, codec, dispatcher) -> init() -> start()
 *
 * 每个 HttpServlet 只需重写 handle()：填充 response 并返回 true；
 * 返回 false 或抛异常时，dispatcher 会自动回 500。
 *
 * 注意：一个进程只能承载一种协议；本示例使用 http 协议，
 * 且不依赖 XML 配置（端口、IO 线程数在代码里设置）。
 *
 * 运行方式：
 *   hello_http_server --server <port>  ：在指定端口启动 HTTP 服务端
 *
 * 验证（框架无 HTTP 客户端封装，用 curl）：
 *   curl -i "http://127.0.0.1:<port>/hello?name=rpc"
 *   curl -i "http://127.0.0.1:<port>/api/json"
 *   curl -i -d 'ping' "http://127.0.0.1:<port>/echo"
 *   curl -i "http://127.0.0.1:<port>/error"
 */

#include "net/http/httpcodec.h"
#include "net/http/httpdefine.h"
#include "net/http/httpdispatcher.h"
#include "net/http/httprequest.h"
#include "net/http/httpresponse.h"
#include "net/http/httpservlet.h"
#include "net/netaddress.h"
#include "net/tcpserver.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {

constexpr const char *kHost = "0.0.0.0";
constexpr int kIoThreadNum = 2;

/*
 * HelloServlet -- GET /hello?name=<name>，返回纯文本问候。
 */
class HelloServlet : public tinyrpc::HttpServlet {
 public:
    bool handle(tinyrpc::HttpRequest *request, tinyrpc::HttpResponse *response) override
    {
        response->setStatusCode(tinyrpc::HttpStatusCode::OK);
        response->setHeader("Content-Type", "text/plain");
        if (request != nullptr && request->hasQueryParam("name")) {
            response->setBody("hello " + request->getQueryParam("name"));
        } else {
            response->setBody("hello http");
        }
        return true;
    }
};

/*
 * JsonServlet -- GET /api/json，返回 JSON。演示设置 JSON Content-Type。
 */
class JsonServlet : public tinyrpc::HttpServlet {
 public:
    bool handle(tinyrpc::HttpRequest *request, tinyrpc::HttpResponse *response) override
    {
        std::string name = "world";
        std::string method = "GET";
        if (request != nullptr) {
            if (request->hasQueryParam("name")) {
                name = request->getQueryParam("name");
            }
            method = tinyrpc::httpMethodToString(request->getMethod());
        }

        response->setStatusCode(tinyrpc::HttpStatusCode::OK);
        response->setHeader("Content-Type", "application/json");
        // 示例代码直接拼接 JSON；生产环境应使用 JSON 库并转义特殊字符。
        response->setBody("{\"service\":\"hello_rpc\",\"name\":\"" + name
                          + "\",\"method\":\"" + method + "\"}");
        return true;
    }
};

/*
 * EchoServlet -- POST /echo，回显请求 body。演示读取 POST body。
 */
class EchoServlet : public tinyrpc::HttpServlet {
 public:
    bool handle(tinyrpc::HttpRequest *request, tinyrpc::HttpResponse *response) override
    {
        response->setStatusCode(tinyrpc::HttpStatusCode::OK);
        response->setHeader("Content-Type", "text/plain");
        response->setBody(request == nullptr ? std::string() : request->getBody());
        return true;
    }
};

/*
 * ErrorServlet -- 返回 false，演示 dispatcher 自动回 500。
 */
class ErrorServlet : public tinyrpc::HttpServlet {
 public:
    bool handle(tinyrpc::HttpRequest *request, tinyrpc::HttpResponse *response) override
    {
        (void)request;
        (void)response;
        return false;
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
 * runServer -- 装配 HttpCodec + HttpDispatcher，注册路由后启动 TcpServer。
 */
int runServer(uint16_t port)
{
    auto codec = std::make_shared<tinyrpc::HttpCodec>();
    auto dispatcher = std::make_shared<tinyrpc::HttpDispatcher>();
    if (!dispatcher->registerServlet("/hello", std::make_shared<HelloServlet>())) {
        std::cerr << "[hello-http] register /hello failed" << std::endl;
        return 1;
    }
    if (!dispatcher->registerServlet("/api/json", std::make_shared<JsonServlet>())) {
        std::cerr << "[hello-http] register /api/json failed" << std::endl;
        return 1;
    }
    if (!dispatcher->registerServlet("/echo", std::make_shared<EchoServlet>())) {
        std::cerr << "[hello-http] register /echo failed" << std::endl;
        return 1;
    }
    if (!dispatcher->registerServlet("/error", std::make_shared<ErrorServlet>())) {
        std::cerr << "[hello-http] register /error failed" << std::endl;
        return 1;
    }

    tinyrpc::TcpServer server(tinyrpc::IPAddress(kHost, port), codec, dispatcher);
    server.setIOThreadNum(kIoThreadNum);
    if (!server.init()) {
        std::cerr << "[hello-http] init failed" << std::endl;
        return 1;
    }

    std::cout << "[hello-http] listen " << port
              << ", io_threads = " << kIoThreadNum << std::endl;
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
