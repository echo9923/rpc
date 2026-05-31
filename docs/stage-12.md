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

## 任务五十九：HTTP 请求解码

已完成能力：

- 新增 `HttpCodec`，继承 `AbstractCodec` 并返回 `ProtocolType::Http`。
- 实现 `HttpCodec::decode()`，可解析 HTTP request line、headers 和 body。
- request line 当前要求格式为 `METHOD path HTTP/x.y`，并只接受 `GET` 与 `POST`。
- headers 按 `key: value` 解析，key 和 value 两侧空白会被裁剪。
- `Content-Length` 存在时按长度读取 body；body 未补齐时不消费 buffer，且不误判成功。
- 非法 request line 或非法 header 会返回失败并消费当前坏包头部，避免 parser 对同一坏包死循环。
- 新增 `test_http_codec`，覆盖 GET、POST、半包补齐和非法 request line。

## HTTP decode 当前边界

- 当前 decode 只处理 request，不处理 response。
- 当前不做 chunked、multipart 或 HTTP/2。
- 当前不做大小写无关 header 查找，`Content-Length` 需要使用标准大小写。
- `HttpCodec::encode()` 仍保持安全失败，任务六十会实现 response 编码。

## 验证命令

```bash
./build.sh
./build/test_http_define
./build/test_http_codec
./scripts/check_rpc_sync.sh
```
