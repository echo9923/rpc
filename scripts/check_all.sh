#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

cd "${ROOT_DIR}"

echo "[all] build project"
./build.sh

run_test() {
    local name="$1"
    local bin="${BUILD_DIR}/${name}"

    if [[ ! -x "${bin}" ]]; then
        echo "[all] FAIL: missing executable ${bin}"
        exit 1
    fi

    echo "[all] run ${name}"
    "${bin}"
}

echo "[all] run core unit tests not covered by rpc-sync"
run_test test_fdevent
run_test test_reactor
run_test test_timer_event
run_test test_timer
run_test test_tcp_timewheel
run_test test_mutex
run_test test_iothread
run_test test_iothread_pool
run_test test_config
run_test test_log
run_test test_start
run_test test_runtime
run_test test_http_define
run_test test_http_codec
run_test test_http_dispatcher
run_test test_coroutine
run_test test_coroutine_pool
run_test test_memory_pool
run_test test_hook
run_test test_hook_sleep
run_test test_hook_socket

echo "[all] run sync rpc regression"
MYTINYRPC_SKIP_BUILD=1 ./scripts/check_rpc_sync.sh

echo "[all] run multi-reactor server regression"
./scripts/check_stage11_server.sh

echo "[all] run http regression"
./scripts/check_stage12_http.sh

echo "[all] run async rpc regression"
MYTINYRPC_SKIP_BUILD=1 ./scripts/check_rpc_async.sh

echo "[all] run generator regression"
./scripts/check_generator.sh
./scripts/check_generator_project.sh

echo "[all] PASS"
