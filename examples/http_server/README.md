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
[stage12] PASS
```

The script starts `test_http_server`, checks `/hello`, checks an unknown path returns `404`, and stops the server process.

## Source Pointers

- HTTP server entry: `testcases/test_http_server.cc`
- HTTP codec: `mytinyrpc/net/http/httpcodec.*`
- HTTP dispatcher: `mytinyrpc/net/http/httpdispatcher.*`
- Servlet abstraction: `mytinyrpc/net/http/httpservlet.*`

## Boundary

This is a minimal HTTP/1.x path. It supports common request parsing and response encoding, not HTTPS, HTTP/2, chunked transfer, streaming responses, or static files.
