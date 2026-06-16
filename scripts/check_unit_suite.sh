#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/testlib.sh"

LABEL="unit-suite"
COMMAND_TIMEOUT="${MYTINYRPC_UNIT_TIMEOUT:-45}"

cd "${MYTINYRPC_ROOT_DIR}"
mytinyrpc_build_if_needed "${LABEL}"

unit_tests=(
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
    test_config
    test_log
    test_start
    test_runtime
)

for unit_test in "${unit_tests[@]}"; do
    mytinyrpc_run_binary "${LABEL}" "${COMMAND_TIMEOUT}" "${unit_test}"
done

echo "[${LABEL}] PASS"
