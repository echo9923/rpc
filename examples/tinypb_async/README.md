# TinyPB Async RPC Example

This example uses `TinyPbRpcAsyncChannel` to demonstrate pending request tracking, IOThread dispatch, timeout, cancellation, and callback lifecycle.

## Run

```bash
cd /mnt/d/codeproject/cpp/rpc
./build.sh
./scripts/check_rpc_async.sh
```

Expected final output:

```text
[rpc-async] PASS
```

## Source Pointers

- Async channel tests: `testcases/test_tinypb_rpc_async_channel.cc`
- Script client: `testcases/test_tinypb_async_client.cc`
- Async channel: `mytinyrpc/net/tinypb/tinypbrpcasyncchannel.*`
- Timer support: `mytinyrpc/net/timer.*`
- IOThread support: `mytinyrpc/net/iothread.*`

## Covered Behaviors

- Multiple async requests complete.
- Pending map matches responses by `msgReq`.
- Unknown or late responses do not trigger duplicate callbacks.
- Timeout and cancellation remove pending contexts and run closure once.

## Boundary

The current network path still uses synchronous `TcpClient::sendAndRecvTinyPb()` inside IOThread. It is asynchronous to the caller, but not a fully nonblocking socket pipeline.
