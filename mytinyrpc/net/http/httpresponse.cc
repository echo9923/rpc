#include "net/http/httpresponse.h"

#include <cctype>
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
    std::string normalizedKey = normalizeHeaderKey(key);
    if (normalizedKey.empty()) {
        return;
    }
    m_headers[normalizedKey] = value;
}

bool HttpResponse::hasHeader(const std::string& key) const
{
    return m_headers.find(normalizeHeaderKey(key)) != m_headers.end();
}

std::string HttpResponse::getHeader(const std::string& key) const
{
    auto it = m_headers.find(normalizeHeaderKey(key));
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

void HttpResponse::setErrorResponse(HttpStatusCode code)
{
    setStatusCode(code);
    setHeader("Content-Type", "text/plain; charset=utf-8");
    setBody(httpCodeToString(code));
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

    for (const auto& header : m_headers) {
        result += header.first;
        result += ": ";
        result += header.second;
        result += "\r\n";
    }

    result += "\r\n";
    result += m_body;
    return result;
}

void HttpResponse::prepareForEncode()
{
    // HTTP response 统一在编码前补齐默认 header，避免不同调用方生成不同格式。
    setHeader("Content-Length", std::to_string(m_body.size()));
    if (!hasHeader("Content-Type")) {
        setHeader("Content-Type", "text/plain; charset=utf-8");
    }
    setHeader("Connection", "close");
}

std::string HttpResponse::normalizeHeaderKey(const std::string& key)
{
    std::string result;
    result.reserve(key.size());
    for (char ch : key) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
}

}
