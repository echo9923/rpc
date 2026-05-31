#include "net/http/http_define.h"

namespace tinyrpc {

std::string httpMethodToString(HttpMethod method)
{
    // 将内部枚举转换为 HTTP request line 中使用的方法文本。
    switch (method) {
    case HttpMethod::GET:
        return "GET";
    case HttpMethod::POST:
        return "POST";
    default:
        return "UNKNOWN";
    }
}

HttpMethod stringToHttpMethod(const std::string& method)
{
    // 当前 parser 只识别 GET / POST，其余方法统一归为 UNKNOWN。
    if (method == "GET") {
        return HttpMethod::GET;
    }
    if (method == "POST") {
        return HttpMethod::POST;
    }
    return HttpMethod::UNKNOWN;
}

std::string httpCodeToString(int code)
{
    // 将状态码转换为 reason phrase，用于 HTTP response status line。
    switch (code) {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 500:
        return "Internal Server Error";
    default:
        return "Unknown";
    }
}

std::string httpCodeToString(HttpStatusCode code)
{
    return httpCodeToString(static_cast<int>(code));
}

}
