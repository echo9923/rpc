#include "net/http/httpcodec.h"
#include "net/http/httprequest.h"
#include "net/http/httpresponse.h"

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>

namespace tinyrpc {

namespace {

constexpr const char *kHttpHeaderEnd = "\r\n\r\n";
constexpr size_t kHttpHeaderEndLength = 4;
constexpr const char *kHttpSchemePrefix = "http://";

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
    std::string target;
    std::string version;
    std::string extra;

    if (!(stream >> method >> target >> version) || (stream >> extra)) {
        return false;
    }
    if (version != "HTTP/1.0" && version != "HTTP/1.1") {
        return false;
    }

    HttpMethod parsedMethod = stringToHttpMethod(method);
    if (parsedMethod == HttpMethod::UNKNOWN) {
        return false;
    }

    if (!parseRequestTarget(target, request)) {
        return false;
    }

    request->setMethod(parsedMethod);
    request->setVersion(version);
    return true;
}

bool HttpCodec::parseRequestTarget(const std::string& target, HttpRequest *request)
{
    if (target.empty() || request == nullptr) {
        return false;
    }

    std::string pathAndQuery = target;
    if (target.rfind(kHttpSchemePrefix, 0) == 0) {
        size_t pathBegin = target.find('/', std::string(kHttpSchemePrefix).size());
        if (pathBegin == std::string::npos) {
            pathAndQuery = "/";
        } else {
            pathAndQuery = target.substr(pathBegin);
        }
    } else if (target[0] != '/') {
        return false;
    }

    size_t queryBegin = pathAndQuery.find('?');
    std::string path = queryBegin == std::string::npos ? pathAndQuery : pathAndQuery.substr(0, queryBegin);
    std::string queryString = queryBegin == std::string::npos ? "" : pathAndQuery.substr(queryBegin + 1);
    if (path.empty()) {
        path = "/";
    }
    if (path[0] != '/') {
        return false;
    }

    request->setRequestTarget(target);
    request->setPath(path);
    request->setQueryString(queryString);
    request->clearQueryParams();
    return parseQueryString(queryString, request);
}

bool HttpCodec::parseQueryString(const std::string& queryString, HttpRequest *request)
{
    if (request == nullptr || queryString.empty()) {
        return true;
    }

    size_t begin = 0;
    while (begin <= queryString.size()) {
        size_t end = queryString.find('&', begin);
        std::string pair = queryString.substr(
            begin,
            end == std::string::npos ? std::string::npos : end - begin
        );

        if (!pair.empty()) {
            size_t equal = pair.find('=');
            std::string key = equal == std::string::npos ? pair : pair.substr(0, equal);
            std::string value = equal == std::string::npos ? "" : pair.substr(equal + 1);
            request->setQueryParam(key, value);
        }

        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }

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
