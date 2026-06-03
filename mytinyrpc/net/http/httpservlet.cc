#include "net/http/httpservlet.h"
#include "net/http/httpresponse.h"

namespace tinyrpc {

void NotFoundHttpServlet::handle(HttpRequest *request, HttpResponse *response)
{
    // request 当前未参与 404 响应生成，保留参数以保持 servlet 统一签名。
    (void)request;
    if (response == nullptr) {
        return;
    }

    response->setStatusCode(HttpStatusCode::NotFound);
    response->setHeader("Content-Type", "text/plain");
    response->setBody("404 Not Found");
}

}
