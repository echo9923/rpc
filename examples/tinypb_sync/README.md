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

## Source Pointers

- Server/client acceptance entry: `testcases/test_tinypb_server_client.cc`
- Service proto: `testcases/test_tinypb_server.proto`
- Sync channel: `mytinyrpc/net/tinypb/tinypbrpcchannel.*`
- Controller: `mytinyrpc/net/tinypb/tinypbrpccontroller.*`

## Boundary

The synchronous client keeps one in-flight request and checks `reqId` mismatch directly. Pending maps and out-of-order response handling live in the async RPC example.
