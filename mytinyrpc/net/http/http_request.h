#pragma once

#include "net/abstractdata.h"
#include "net/http/http_define.h"

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

    const std::string& getVersion() const;
    void setVersion(const std::string& version);

    void setHeader(const std::string& key, const std::string& value);
    bool hasHeader(const std::string& key) const;
    std::string getHeader(const std::string& key) const;
    const HttpHeaders& getHeaders() const;

    const std::string& getBody() const;
    void setBody(const std::string& body);

 private:
    HttpMethod m_method {HttpMethod::UNKNOWN};
    std::string m_path {"/"};
    std::string m_version {"HTTP/1.1"};
    HttpHeaders m_headers;
    std::string m_body;
};

}
