# 阶段 12：HTTP 协议栈

阶段 12 的目标是在现有 `TcpServer`、`AbstractCodec` 和 `AbstractDispatcher` 模型上增加 HTTP 支持。本阶段先补 HTTP request/response 基础数据结构，再实现 HTTP codec、dispatcher 和 server 验收脚本。

## 任务五十八：HTTP 基础数据结构

已完成能力：

- 新增 `HttpMethod`，当前覆盖 `GET`、`POST` 和 `UNKNOWN`。
- 新增 `HttpStatusCode`，当前覆盖 `200`、`400`、`404` 和 `500`。
- 新增 `HttpHeaders`，使用 `std::map<std::string, std::string>` 保存 header。
- 新增 `httpMethodToString()`、`stringToHttpMethod()` 和 `httpCodeToString()`，为后续 parser 与 codec 复用基础转换能力。
- 新增 `HttpRequest`，记录 method、path、version、headers 和 body，并继承 `AbstractData`。
- 新增 `HttpResponse`，记录 version、status、headers 和 body，并可通过 `toString()` 生成最小 HTTP 响应文本。
- 新增 `test_http_define`，覆盖 status code 文本、header 设置读取和 response 状态行、header、body 生成。

## 当前边界

- 当前只实现数据结构和最小响应字符串生成，不做 HTTP parser。
- 当前不做 chunked、multipart、HTTP/2 或 keep-alive 完整语义。
- `HttpResponse::toString()` 会在缺少 `Content-Length` 时自动补齐 body 长度，后续 `HttpCodec::encode()` 会复用该行为。

## 验证命令

```bash
./build.sh
./build/test_http_define
./scripts/check_rpc_sync.sh
```
