#include "net/http/httpdispatcher.h"
#include "net/http/httprequest.h"
#include "net/http/httpresponse.h"
#include "net/tcpconnection.h"

namespace tinyrpc {

HttpDispatcher::HttpDispatcher()
    : m_notFoundServlet(std::make_shared<NotFoundHttpServlet>())
{
}

bool HttpDispatcher::registerServlet(const std::string& path, HttpServlet::Ptr servlet)
{
    if (path.empty() || servlet == nullptr) {
        return false;
    }

    // 同一路径重复注册时不覆盖，避免路由被意外替换。
    if (m_servlets.find(path) != m_servlets.end()) {
        return false;
    }

    m_servlets[path] = std::move(servlet);
    return true;
}

HttpServlet* HttpDispatcher::findServlet(const std::string& path) const
{
    auto it = m_servlets.find(path);
    if (it == m_servlets.end()) {
        return m_notFoundServlet.get();
    }
    return it->second.get();
}

void HttpDispatcher::dispatch(HttpRequest *request, HttpResponse *response)
{
    if (request == nullptr || response == nullptr) {
        return;
    }

    HttpServlet *servlet = findServlet(request->getPath());
    if (servlet == nullptr) {
        return;
    }
    servlet->handle(request, response);
}

void HttpDispatcher::dispatch(AbstractData *data, TcpConnection *conn)
{
    if (data == nullptr || conn == nullptr) {
        return;
    }

    // dynamic_cast 将 AbstractData* 安全转为 HttpRequest*；
    // 若 data 不是 HTTP 请求对象，则当前 dispatcher 不处理。
    auto *request = dynamic_cast<HttpRequest *>(data);
    if (request == nullptr) {
        return;
    }

    HttpResponse response;
    dispatch(request, &response);
    conn->sendProtocolData(&response);
}

}
