#pragma once

#include <map>
#include <string>

namespace tinyrpc {

// HTTP 请求方法。当前阶段只覆盖后续 parser 明确要求支持的 GET / POST。
enum class HttpMethod {
    GET = 1,
    POST = 2,
    UNKNOWN = 100,
};

// HTTP 响应状态码。先保留常见状态，后续 dispatcher 可按需扩展。
enum class HttpStatusCode {
    OK = 200,
    BadRequest = 400,
    NotFound = 404,
    InternalServerError = 500,
};

// HTTP header 使用有序 map 保存，便于测试输出稳定且满足基础查找需求。
using HttpHeaders = std::map<std::string, std::string>;

std::string httpMethodToString(HttpMethod method);
HttpMethod stringToHttpMethod(const std::string& method);

std::string httpCodeToString(int code);
std::string httpCodeToString(HttpStatusCode code);

}
