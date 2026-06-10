# TinyPB Sync RPC Example

This example uses the existing stage 8/9 synchronous TinyPB RPC path:

```text
QueryService_Stub -> TinyPbRpcChannel -> TcpClient -> TinyPB -> TcpServer -> TinyPbDispatcher -> QueryServiceImpl
```

## Run

```bash
cd /mnt/d/codeproject/cpp/rpc
./build.sh
./scripts/check_stage8_rpc.sh
```

Expected final output:

```text
[stage8] PASS
```

For the broader synchronous safety net:

```bash
./scripts/check_rpc_sync.sh
```

Expected final output:

```text
[rpc-sync] PASS
```

For the completed client-networking path from stage 20:

```bash
./scripts/check_rpc_client_reactor.sh
```

Expected final output:

```text
[rpc-client-reactor] PASS
```

## Source Pointers

- Server/client acceptance entry: `testcases/test_tinypb_server_client.cc`
- Service proto: `testcases/test_tinypb_server.proto`
- Sync channel: `mytinyrpc/net/tinypb/tinypbrpcchannel.*`
- Controller: `mytinyrpc/net/tinypb/tinypbrpccontroller.*`

## Full-Completion Path

This example now covers the completed synchronous TinyPB path: client-side `TcpConnection`, Reactor/Timer timeout handling, `reqId` response matching, connection reuse, explicit close, and failure rebuild. It is included by `scripts/check_all.sh` and the stage 25 gate `scripts/check_full_completion.sh`.

## Boundary

The synchronous client still models one in-flight request per call. Pending maps, pipelined requests, out-of-order response matching, timeout/cancel arbitration, and async callbacks live in the async RPC example.
