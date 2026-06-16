#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/testlib.sh"

LABEL="build-targets"

cd "${MYTINYRPC_ROOT_DIR}"
mytinyrpc_build_if_needed "${LABEL}"

targets=(
    test_tcp_echo_server
    test_fdevent
    test_reactor
    test_abstract_codec
    test_tinypb_data
    test_tinypb_codec
    test_tcp_buffer
    test_coroutine
    test_coroutine_pool
    test_coroutinepool
    test_memory_pool
    test_hook
    test_hook_sleep
    test_hook_socket
    test_connection_codec
    test_tinypb_dispatcher
    test_protobuf_service
    test_tcp_client
    test_tcpserver_lifecycle
    test_tinypb_rpc_channel
    test_tinypb_rpc_async_channel
    test_tinypb_async_client
    test_tinypb_server_client
    test_req_id
    test_timer_task
    test_timer
    test_tcp_timewheel
    test_mutex
    test_iothread
    test_iothread_pool
    test_thread_pool
    test_http_define
    test_http_codec
    test_http_dispatcher
    test_http_server
    test_config
    test_log
    test_start
    test_runtime
    benchmark_tinypb_sync
    benchmark_tinypb_async
    benchmark_http
    e2e_server
    e2e_client
)

missing_count=0
for target in "${targets[@]}"; do
    if [[ -x "${MYTINYRPC_BUILD_DIR}/${target}" ]]; then
        echo "[${LABEL}] ready ${target}"
    else
        echo "[${LABEL}] missing ${target}"
        missing_count=$((missing_count + 1))
    fi
done

if (( missing_count != 0 )); then
    mytinyrpc_fail "${LABEL}" "${missing_count} build target(s) are missing"
fi

echo "[${LABEL}] PASS"
