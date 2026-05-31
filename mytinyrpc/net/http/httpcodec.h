#pragma once

#include "net/abstractcodec.h"

#include <cstddef>
#include <memory>
#include <string>

namespace tinyrpc {

class HttpRequest;

// HttpCodec 负责 HTTP/1.x 文本协议和 HttpRequest/HttpResponse 的转换。
// 当前任务只实现 request decode；response encode 在任务六十补齐。
class HttpCodec : public AbstractCodec {
 public:
    using Ptr = std::shared_ptr<HttpCodec>;

    ~HttpCodec() override = default;

    void encode(TcpBuffer *buffer, AbstractData *data) override;
    void decode(TcpBuffer *buffer, AbstractData *data) override;
    ProtocolType getProtocolType() const override;

 private:
    static bool parseRequestLine(const std::string& line, HttpRequest *request);
    static bool parseHeaderLine(const std::string& line, HttpRequest *request);
    static bool parseContentLength(const HttpRequest& request, size_t *contentLength);
    static std::string trim(const std::string& text);
};

}
