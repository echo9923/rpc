#pragma once

#include "net/abstractdata.h"
#include "net/http/http_define.h"

#include <memory>
#include <string>

namespace tinyrpc {

// HttpResponse 承载一次 HTTP 响应的结构化字段。
// toString() 提供最小文本序列化，后续 HttpCodec::encode() 可复用。
class HttpResponse : public AbstractData {
 public:
    using Ptr = std::shared_ptr<HttpResponse>;

    HttpStatusCode getStatusCode() const;
    int getStatusCodeValue() const;
    void setStatusCode(HttpStatusCode code);
    void setStatusCode(int code);

    const std::string& getVersion() const;
    void setVersion(const std::string& version);

    void setHeader(const std::string& key, const std::string& value);
    bool hasHeader(const std::string& key) const;
    std::string getHeader(const std::string& key) const;
    const HttpHeaders& getHeaders() const;

    const std::string& getBody() const;
    void setBody(const std::string& body);

    std::string toString() const;

 private:
    int m_statusCode {static_cast<int>(HttpStatusCode::OK)};
    std::string m_version {"HTTP/1.1"};
    HttpHeaders m_headers;
    std::string m_body;
};

}
