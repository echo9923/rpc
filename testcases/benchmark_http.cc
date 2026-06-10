#include "comm/log.h"
#include "net/http/httpcodec.h"
#include "net/http/httpdispatcher.h"
#include "net/http/httprequest.h"
#include "net/http/httpresponse.h"
#include "net/http/httpservlet.h"
#include "net/tcpbuffer.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

namespace {

constexpr int kRequestCount = 1000;

class BenchServlet : public tinyrpc::HttpServlet {
 public:
    bool handle(tinyrpc::HttpRequest *request, tinyrpc::HttpResponse *response) override
    {
        response->setStatusCode(tinyrpc::HttpStatusCode::OK);
        response->setHeader("Content-Type", "text/plain");
        response->setBody("hello " + (request == nullptr ? std::string("http") : request->getQueryParam("name")));
        return true;
    }
};

}  // namespace

int main()
{
    tinyrpc::Logger::setEnabled(false);

    tinyrpc::HttpCodec codec;
    tinyrpc::HttpDispatcher dispatcher;
    if (!dispatcher.registerServlet("/hello", std::make_shared<BenchServlet>())) {
        std::cerr << "[benchmark][http] FAIL: register servlet failed" << std::endl;
        return 1;
    }

    std::string rawRequest =
        "GET /hello?name=bench HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: close\r\n"
        "\r\n";

    size_t encodedBytes = 0;
    auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < kRequestCount; ++i) {
        tinyrpc::TcpBuffer input(256);
        input.append(rawRequest);

        tinyrpc::HttpRequest request;
        codec.decode(&input, &request);
        if (!request.m_decodeSucc) {
            std::cerr << "[benchmark][http] FAIL: request decode failed" << std::endl;
            return 1;
        }

        tinyrpc::HttpResponse response;
        dispatcher.dispatch(&request, &response);
        tinyrpc::TcpBuffer output(256);
        codec.encode(&output, &response);
        if (!response.m_encodeSucc) {
            std::cerr << "[benchmark][http] FAIL: response encode failed" << std::endl;
            return 1;
        }
        encodedBytes += output.getReadableBytes();
    }
    auto end = std::chrono::steady_clock::now();

    tinyrpc::Logger::setEnabled(true);

    double totalMs = std::chrono::duration<double, std::milli>(end - begin).count();
    double throughput = static_cast<double>(kRequestCount) * 1000.0 / totalMs;
    std::cout << std::fixed << std::setprecision(2)
              << "[benchmark][http] requests=" << kRequestCount
              << " total_ms=" << totalMs
              << " throughput_qps=" << throughput
              << " encoded_bytes=" << encodedBytes
              << std::endl;
    return 0;
}
