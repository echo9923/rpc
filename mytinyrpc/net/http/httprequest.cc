#include "net/http/httprequest.h"

namespace tinyrpc {

HttpMethod HttpRequest::getMethod() const
{
    return m_method;
}

void HttpRequest::setMethod(HttpMethod method)
{
    m_method = method;
}

const std::string& HttpRequest::getPath() const
{
    return m_path;
}

void HttpRequest::setPath(const std::string& path)
{
    m_path = path;
}

const std::string& HttpRequest::getVersion() const
{
    return m_version;
}

void HttpRequest::setVersion(const std::string& version)
{
    m_version = version;
}

void HttpRequest::setHeader(const std::string& key, const std::string& value)
{
    // 同名 header 后写覆盖先写，保持最小数据结构语义。
    m_headers[key] = value;
}

bool HttpRequest::hasHeader(const std::string& key) const
{
    return m_headers.find(key) != m_headers.end();
}

std::string HttpRequest::getHeader(const std::string& key) const
{
    auto it = m_headers.find(key);
    if (it == m_headers.end()) {
        return "";
    }
    return it->second;
}

const HttpHeaders& HttpRequest::getHeaders() const
{
    return m_headers;
}

const std::string& HttpRequest::getBody() const
{
    return m_body;
}

void HttpRequest::setBody(const std::string& body)
{
    m_body = body;
}

}
