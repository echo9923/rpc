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
- 新增 `test_httpdefine`，覆盖 status code 文本、header 设置读取和 response 状态行、header、body 生成。

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

## 任务六十：HTTP 响应编码

已完成能力：

- 实现 `HttpCodec::encode()`，可将 `HttpResponse` 编码到 `TcpBuffer`。
- encode 前会按 body 实际长度设置 `Content-Length`，覆盖调用方传入的旧值，避免响应长度和 body 不一致。
- 编码结果使用 `HttpResponse::toString()` 生成标准 HTTP/1.x 响应文本：状态行、headers、空行、body。
- `test_http_codec` 补充 200 response、404 response 和 `Content-Length` 修正测试。

## HTTP encode 当前边界

- 当前只编码完整内存 body，不做 gzip。
- 当前不做 streaming response。
- 当前不主动追加 `Connection` 语义，连接生命周期仍由上层服务端流程控制。

## 任务六十一：HttpServlet 与 HttpDispatcher

已完成能力：

- 新增 `HttpServlet` 抽象类，以 `handle(HttpRequest*, HttpResponse*)` 作为 HTTP 业务处理入口。
- 新增 `NotFoundHttpServlet`，未知 path 默认返回 404、`Content-Type: text/plain` 和 body `404 Not Found`。
- 新增 `HttpDispatcher`，按 path 精确匹配 servlet。
- `HttpDispatcher::registerServlet()` 支持注册固定路径，重复路径不覆盖并返回失败。
- `HttpDispatcher::dispatch(HttpRequest*, HttpResponse*)` 支持单测直接分发。
- `HttpDispatcher::dispatch(AbstractData*, TcpConnection*)` 保持 `AbstractDispatcher` 接口兼容，后续接入 `TcpServer` 时会生成 `HttpResponse` 并通过连接写回。
- 新增 `test_http_dispatcher`，覆盖 `/hello` 业务 body、未知 path 404 和重复注册失败。

## HTTP dispatcher 当前边界

- 当前只做精确路径匹配，不做正则路由。
- 当前不做中间件。
- 当前不做静态文件服务。

## 任务六十二：HTTP Server 集成和脚本

已完成能力：

- 新增 `test_http_server`，使用 `TcpServer` + `HttpCodec` + `HttpDispatcher` 启动最小 HTTP server。
- `test_http_server` 注册 `/hello` servlet，返回 `hello http`。
- 未知 path 通过 `NotFoundHttpServlet` 返回 404 和 body `404 Not Found`。
- 新增 `scripts/check_stage12_http.sh`，使用 `curl` 验证 `/hello` 与未知 path。
- `TcpConnection::execute()` 根据 `AbstractCodec::getProtocolType()` 创建 `TinyPbStruct` 或 `HttpRequest`，让 TinyPB 和 HTTP 共用 `TcpServer` / `TcpConnection` 抽象。
- HTTP 脚本中的 curl 调用带 `--max-time`，避免失败时无限等待。

## HTTP server 当前边界

- 当前只提供测试用最小 HTTP server，不做 HTTPS。
- 当前不做 HTTP/2。
- 当前不做压力测试。

## 验证命令

```bash
./build.sh
./build/test_httpdefine
./build/test_http_codec
./build/test_http_dispatcher
./scripts/check_stage12_http.sh
./scripts/check_rpc_sync.sh
```
