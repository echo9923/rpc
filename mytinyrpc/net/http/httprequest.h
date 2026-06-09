#pragma once

#include "net/abstractdata.h"
#include "net/http/httpdefine.h"

#include <map>
#include <memory>
#include <string>

namespace tinyrpc {

// HttpRequest 承载一次 HTTP 请求的结构化字段。
// 当前只做数据保存，不负责从网络字节流解析。
class HttpRequest : public AbstractData {
 public:
    using Ptr = std::shared_ptr<HttpRequest>;

    HttpMethod getMethod() const;
    void setMethod(HttpMethod method);

    const std::string& getPath() const;
    void setPath(const std::string& path);

    const std::string& getRequestTarget() const;
    void setRequestTarget(const std::string& requestTarget);

    const std::string& getQueryString() const;
    void setQueryString(const std::string& queryString);
    void setQueryParam(const std::string& key, const std::string& value);
    bool hasQueryParam(const std::string& key) const;
    std::string getQueryParam(const std::string& key) const;
    const std::map<std::string, std::string>& getQueryParams() const;
    void clearQueryParams();

    const std::string& getVersion() const;
    void setVersion(const std::string& version);

    void setHeader(const std::string& key, const std::string& value);
    bool hasHeader(const std::string& key) const;
    std::string getHeader(const std::string& key) const;
    const HttpHeaders& getHeaders() const;

    const std::string& getBody() const;
    void setBody(const std::string& body);

 private:
    static std::string normalizeHeaderKey(const std::string& key);

    HttpMethod m_method {HttpMethod::UNKNOWN};
    std::string m_requestTarget {"/"};
    std::string m_path {"/"};
    std::string m_queryString;
    std::map<std::string, std::string> m_queryParams;
    std::string m_version {"HTTP/1.1"};
    HttpHeaders m_headers;
    std::string m_body;
};

}
