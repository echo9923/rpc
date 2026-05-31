#pragma once

#include "net/abstractdispatcher.h"
#include "net/http/httpservlet.h"

#include <map>
#include <memory>
#include <string>

namespace tinyrpc {

class HttpRequest;
class HttpResponse;

// HttpDispatcher 按 path 查找 HttpServlet 并生成 HttpResponse。
// 与 TinyPB 的 service.method 分发不同，HTTP 这里只做精确路径匹配。
class HttpDispatcher : public AbstractDispatcher {
 public:
    using Ptr = std::shared_ptr<HttpDispatcher>;

    HttpDispatcher();

    void dispatch(AbstractData *data, TcpConnection *conn) override;
    void dispatch(HttpRequest *request, HttpResponse *response);

    bool registerServlet(const std::string& path, HttpServlet::Ptr servlet);
    HttpServlet* findServlet(const std::string& path) const;

 private:
    std::map<std::string, HttpServlet::Ptr> m_servlets;
    HttpServlet::Ptr m_notFoundServlet;
};

}
