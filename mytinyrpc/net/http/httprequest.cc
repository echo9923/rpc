#include "net/http/httprequest.h"

#include <cctype>

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

const std::string& HttpRequest::getRequestTarget() const
{
    return m_requestTarget;
}

void HttpRequest::setRequestTarget(const std::string& requestTarget)
{
    m_requestTarget = requestTarget;
}

const std::string& HttpRequest::getQueryString() const
{
    return m_queryString;
}

void HttpRequest::setQueryString(const std::string& queryString)
{
    m_queryString = queryString;
}

void HttpRequest::setQueryParam(const std::string& key, const std::string& value)
{
    if (key.empty()) {
        return;
    }

    // 重复 query key 以后写覆盖先写，给上层业务稳定的确定语义。
    m_queryParams[key] = value;
}

bool HttpRequest::hasQueryParam(const std::string& key) const
{
    return m_queryParams.find(key) != m_queryParams.end();
}

std::string HttpRequest::getQueryParam(const std::string& key) const
{
    auto it = m_queryParams.find(key);
    if (it == m_queryParams.end()) {
        return "";
    }
    return it->second;
}

const std::map<std::string, std::string>& HttpRequest::getQueryParams() const
{
    return m_queryParams;
}

void HttpRequest::clearQueryParams()
{
    m_queryParams.clear();
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
    std::string normalizedKey = normalizeHeaderKey(key);
    if (normalizedKey.empty()) {
        return;
    }
    m_headers[normalizedKey] = value;
}

bool HttpRequest::hasHeader(const std::string& key) const
{
    return m_headers.find(normalizeHeaderKey(key)) != m_headers.end();
}

std::string HttpRequest::getHeader(const std::string& key) const
{
    auto it = m_headers.find(normalizeHeaderKey(key));
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

std::string HttpRequest::normalizeHeaderKey(const std::string& key)
{
    std::string result;
    result.reserve(key.size());
    for (char ch : key) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
}

}
