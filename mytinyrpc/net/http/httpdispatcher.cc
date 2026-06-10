#include "net/http/httpdispatcher.h"
#include "comm/runtime.h"
#include "net/http/httprequest.h"
#include "net/http/httpresponse.h"
#include "net/tcpconnection.h"

#include <exception>

namespace tinyrpc {

namespace {

class RequestContextGuard {
 public:
    RequestContextGuard(HttpRequest *request, TcpConnection *conn)
    {
        std::string reqId;
        std::string method;
        std::string path;
        std::string localAddr = "local";
        std::string peerAddr = "peer";
        if (request != nullptr) {
            reqId = request->getHeader("X-Req-Id");
            if (reqId.empty()) {
                reqId = request->getHeader("X-Trace-Id");
            }
            method = httpMethodToString(request->getMethod());
            path = request->getPath();
        }
        if (conn != nullptr) {
            std::string local = conn->getLocalAddressString();
            std::string peer = conn->getPeerAddressString();
            if (!local.empty()) {
                localAddr = local;
            }
            if (!peer.empty()) {
                peerAddr = peer;
            }
        }
        getRuntime().setCurrentRequestContext(
            reqId,
            "http",
            method,
            localAddr,
            peerAddr,
            ProtocolType::Http,
            path
        );
    }

    ~RequestContextGuard()
    {
        getRuntime().clearCurrentRequestContext();
    }
};

}  // namespace

HttpDispatcher::HttpDispatcher()
    : m_notFoundServlet(std::make_shared<NotFoundHttpServlet>())
{
}

bool HttpDispatcher::registerServlet(const std::string& path, HttpServlet::Ptr servlet)
{
    if (path.empty() || path[0] != '/' || servlet == nullptr) {
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
    dispatch(request, response, nullptr);
}

void HttpDispatcher::dispatch(HttpRequest *request, HttpResponse *response, TcpConnection *conn)
{
    if (request == nullptr || response == nullptr) {
        return;
    }

    RequestContextGuard contextGuard(request, conn);
    HttpServlet *servlet = findServlet(request->getPath());
    if (servlet == nullptr) {
        return;
    }

    bool ok = false;
    try {
        ok = servlet->handle(request, response);
    } catch (const std::exception&) {
        ok = false;
    } catch (...) {
        ok = false;
    }

    if (!ok) {
        response->setErrorResponse(HttpStatusCode::InternalServerError);
    }
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
    dispatch(request, &response, conn);
    conn->sendProtocolData(&response);
}

}
