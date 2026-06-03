#include "net/http/httpresponse.h"

#include <string>

namespace tinyrpc {

HttpStatusCode HttpResponse::getStatusCode() const
{
    return static_cast<HttpStatusCode>(m_statusCode);
}

int HttpResponse::getStatusCodeValue() const
{
    return m_statusCode;
}

void HttpResponse::setStatusCode(HttpStatusCode code)
{
    m_statusCode = static_cast<int>(code);
}

void HttpResponse::setStatusCode(int code)
{
    m_statusCode = code;
}

const std::string& HttpResponse::getVersion() const
{
    return m_version;
}

void HttpResponse::setVersion(const std::string& version)
{
    m_version = version;
}

void HttpResponse::setHeader(const std::string& key, const std::string& value)
{
    m_headers[key] = value;
}

bool HttpResponse::hasHeader(const std::string& key) const
{
    return m_headers.find(key) != m_headers.end();
}

std::string HttpResponse::getHeader(const std::string& key) const
{
    auto it = m_headers.find(key);
    if (it == m_headers.end()) {
        return "";
    }
    return it->second;
}

const HttpHeaders& HttpResponse::getHeaders() const
{
    return m_headers;
}

const std::string& HttpResponse::getBody() const
{
    return m_body;
}

void HttpResponse::setBody(const std::string& body)
{
    m_body = body;
}

std::string HttpResponse::toString() const
{
    // 按 HTTP/1.x 响应格式生成：status line、headers、空行、body。
    std::string result;
    result += m_version;
    result += " ";
    result += std::to_string(m_statusCode);
    result += " ";
    result += httpCodeToString(m_statusCode);
    result += "\r\n";

    bool hasContentLength = false;
    for (const auto& header : m_headers) {
        if (header.first == "Content-Length") {
            hasContentLength = true;
        }
        result += header.first;
        result += ": ";
        result += header.second;
        result += "\r\n";
    }

    if (!hasContentLength) {
        // 未显式设置 Content-Length 时按 body 字节数自动补齐。
        result += "Content-Length: ";
        result += std::to_string(m_body.size());
        result += "\r\n";
    }

    result += "\r\n";
    result += m_body;
    return result;
}

}
