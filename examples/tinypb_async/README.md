# TinyPB Async RPC Example

This example uses `TinyPbRpcAsyncChannel` to demonstrate pending request tracking, IOThread Reactor dispatch, timeout, cancellation, callback lifecycle, and a real nonblocking TinyPB network path.

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
- Async session: `mytinyrpc/net/tinypb/asyncclientsession.*`
- Timer support: `mytinyrpc/net/timer.*`
- IOThread support: `mytinyrpc/net/iothread.*`

## Covered Behaviors

- Multiple async requests complete.
- Pending map matches responses by `reqId`.
- Unknown or late responses do not trigger duplicate callbacks.
- Timeout and cancellation remove pending contexts and run closure once.
- Requests are sent through `AsyncClientSession` on a nonblocking socket.
- EPOLLOUT drains the output buffer and EPOLLIN reads TinyPB responses.
- A real `TcpServer` end-to-end path verifies the default async client behavior.

## Full-Completion Path

This example is the completed stage 21 async path. It is included by `scripts/check_all.sh`, and the broader stage 25 gate re-runs it through `scripts/check_full_completion.sh`.

## Boundary

The async Channel does not implement connection pools, load balancing, retry policy, request priority, application-level backpressure, or business-level cancellation propagation. Those are deliberate production-grade boundaries outside the current supplement plan.
