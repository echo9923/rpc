#include "net/http/httpcodec.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>

namespace tinyrpc {

namespace {

constexpr const char *kHttpHeaderEnd = "\r\n\r\n";
constexpr size_t kHttpHeaderEndLength = 4;

}

void HttpCodec::encode(TcpBuffer *buffer, AbstractData *data)
{
    if (buffer == nullptr) {
        if (data != nullptr) {
            data->m_encodeSucc = false;
        }
        return;
    }
    if (data == nullptr) {
        return;
    }

    // dynamic_cast 将 AbstractData* 安全转为 HttpResponse*；
    // 若 data 实际类型不是 HttpResponse，则不能编码为 HTTP 响应。
    auto *response = dynamic_cast<HttpResponse *>(data);
    if (response == nullptr) {
        data->m_encodeSucc = false;
        return;
    }

    response->setHeader("Content-Length", std::to_string(response->getBody().size()));
    std::string raw = response->toString();
    buffer->append(raw);
    response->m_encodeSucc = true;
}

void HttpCodec::decode(TcpBuffer *buffer, AbstractData *data)
{
    if (buffer == nullptr) {
        if (data != nullptr) {
            data->m_decodeSucc = false;
        }
        return;
    }
    if (data == nullptr) {
        return;
    }

    // dynamic_cast 将 AbstractData* 安全转为 HttpRequest*；
    // 若 data 实际类型不是 HttpRequest，则不能填充请求字段。
    auto *request = dynamic_cast<HttpRequest *>(data);
    if (request == nullptr) {
        data->m_decodeSucc = false;
        return;
    }

    std::string raw(buffer->getReadPtr(), buffer->getReadableBytes());
    size_t headerEnd = raw.find(kHttpHeaderEnd);
    if (headerEnd == std::string::npos) {
        request->m_decodeSucc = false;
        return;
    }

    std::string headerPart = raw.substr(0, headerEnd);
    std::istringstream stream(headerPart);

    std::string requestLine;
    if (!std::getline(stream, requestLine)) {
        request->m_decodeSucc = false;
        buffer->retrieve(headerEnd + kHttpHeaderEndLength);
        return;
    }
    if (!requestLine.empty() && requestLine.back() == '\r') {
        requestLine.pop_back();
    }

    if (!parseRequestLine(requestLine, request)) {
        request->m_decodeSucc = false;
        buffer->retrieve(headerEnd + kHttpHeaderEndLength);
        return;
    }

    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!parseHeaderLine(line, request)) {
            request->m_decodeSucc = false;
            buffer->retrieve(headerEnd + kHttpHeaderEndLength);
            return;
        }
    }

    size_t contentLength = 0;
    if (!parseContentLength(*request, &contentLength)) {
        request->m_decodeSucc = false;
        buffer->retrieve(headerEnd + kHttpHeaderEndLength);
        return;
    }

    size_t fullLength = headerEnd + kHttpHeaderEndLength + contentLength;
    if (buffer->getReadableBytes() < fullLength) {
        request->m_decodeSucc = false;
        return;
    }

    request->setBody(raw.substr(headerEnd + kHttpHeaderEndLength, contentLength));
    request->m_decodeSucc = true;
    buffer->retrieve(fullLength);
}

ProtocolType HttpCodec::getProtocolType() const
{
    return ProtocolType::Http;
}

bool HttpCodec::parseRequestLine(const std::string& line, HttpRequest *request)
{
    std::istringstream stream(line);
    std::string method;
    std::string path;
    std::string version;
    std::string extra;

    if (!(stream >> method >> path >> version) || (stream >> extra)) {
        return false;
    }
    if (version.rfind("HTTP/", 0) != 0) {
        return false;
    }

    HttpMethod parsedMethod = stringToHttpMethod(method);
    if (parsedMethod == HttpMethod::UNKNOWN) {
        return false;
    }

    request->setMethod(parsedMethod);
    request->setPath(path);
    request->setVersion(version);
    return true;
}

bool HttpCodec::parseHeaderLine(const std::string& line, HttpRequest *request)
{
    if (line.empty()) {
        return true;
    }

    size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0) {
        return false;
    }

    std::string key = trim(line.substr(0, colon));
    std::string value = trim(line.substr(colon + 1));
    if (key.empty()) {
        return false;
    }

    request->setHeader(key, value);
    return true;
}

bool HttpCodec::parseContentLength(const HttpRequest& request, size_t *contentLength)
{
    *contentLength = 0;
    std::string value = request.getHeader("Content-Length");
    if (value.empty()) {
        return true;
    }

    char *end = nullptr;
    unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0') {
        return false;
    }

    *contentLength = static_cast<size_t>(parsed);
    return true;
}

std::string HttpCodec::trim(const std::string& text)
{
    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }

    size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return text.substr(begin, end - begin);
}

}
