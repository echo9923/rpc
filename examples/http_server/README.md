# HTTP Server Example

This example uses `TcpServer` with `HttpCodec` and `HttpDispatcher`.

## Run

```bash
cd /mnt/d/codeproject/cpp/rpc
./build.sh
./scripts/check_stage12_http.sh
```

Expected final output:

```text
[stage12-http] PASS
```

The script starts `test_http_server`, checks `/hello`, query handling, `404`, `500`, POST body, `Connection: close`, `Content-Length`, and stops the server process.

## Source Pointers

- HTTP server entry: `testcases/test_http_server.cc`
- HTTP codec: `mytinyrpc/net/http/httpcodec.*`
- HTTP dispatcher: `mytinyrpc/net/http/httpdispatcher.*`
- Servlet abstraction: `mytinyrpc/net/http/httpservlet.*`

## Full-Completion Path

This example is the completed stage 22 HTTP path. It covers HTTP/1.0/1.1 GET/POST, origin-form and absolute-form request targets, query map, case-insensitive headers, `Content-Length` body, default response headers, exact/root servlet dispatch, error responses, HTTP request context, and close-after-response semantics. It is included by `scripts/check_all.sh` and `scripts/check_full_completion.sh`.

## Boundary

This is a minimal HTTP/1.x path. It supports common request parsing and response encoding, not HTTPS, HTTP/2, chunked transfer, streaming responses, or static files.
