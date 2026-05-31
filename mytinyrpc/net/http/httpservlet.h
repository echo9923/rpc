#pragma once

#include <memory>

namespace tinyrpc {

class HttpRequest;
class HttpResponse;

// HttpServlet 是 HTTP 路由处理器抽象。
// 每个 servlet 只负责把一个 HttpRequest 填充为 HttpResponse。
class HttpServlet {
 public:
    using Ptr = std::shared_ptr<HttpServlet>;

    virtual ~HttpServlet() = default;

    virtual void handle(HttpRequest *request, HttpResponse *response) = 0;
};

// NotFoundHttpServlet 是默认兜底路由，统一生成 404 响应。
class NotFoundHttpServlet : public HttpServlet {
 public:
    void handle(HttpRequest *request, HttpResponse *response) override;
};

}
