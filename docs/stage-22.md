# 阶段 22：HTTP 协议栈补全

阶段 22 在阶段 12 的最小 HTTP 闭环基础上，补齐 request target、query、header/body 边界、response 默认 header、servlet 失败处理和真实服务端关闭语义。

## 支持矩阵

| 能力 | 当前状态 | 说明 |
|---|---|---|
| HTTP 版本 | 支持 `HTTP/1.0` / `HTTP/1.1` | 其他版本解析失败并消费坏包头部。 |
| Method | 支持 `GET` / `POST` | 其他 method 暂不支持。 |
| Request target | 支持 origin-form / absolute-form | 例如 `/hello?name=alice` 和 `http://host/hello?name=alice`。 |
| Path | 支持 root path 和普通 path | 空 path 会归一为 `/`。 |
| Query | 支持解析到 map | 重复 key 采用后值覆盖；不做 URL decode。 |
| Header | 查询大小写不敏感 | 同名 header 采用后值覆盖。 |
| Body | 支持 `Content-Length` body | 上限固定为 `1 MiB`；支持 POST 空 body。 |
| Response header | 自动补齐默认值 | 编码时修正 `Content-Length`，补 `Content-Type`，统一 `Connection: close`。 |
| Error response | 支持 400 / 404 / 500 | servlet 失败或异常统一转 500。 |
| Servlet | 支持精确路径和 root path | 不做正则路由和 path parameter。 |
| 连接语义 | 统一关闭 | 阶段 22 不实现 keep-alive，HTTP 响应写完后关闭连接。 |

## 不支持范围

- 不支持 HTTPS / TLS。
- 不支持 HTTP/2。
- 不支持 chunked body。
- 不支持 multipart。
- 不支持 gzip。
- 不支持 streaming response。
- 不支持 keep-alive。
- 不支持正则路由和 path parameter。

## 验证命令

```bash
./build.sh
./build/test_http_define
./build/test_http_codec
./build/test_http_dispatcher
./build/test_runtime
./scripts/check_stage12_http.sh
./scripts/check_all.sh
```

`scripts/check_stage12_http.sh` 当前覆盖 `/hello`、query、404、500、POST body、`Connection: close` 和 `Content-Length`。
