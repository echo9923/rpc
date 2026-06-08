#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

cd "${ROOT_DIR}"

if [[ "${MYTINYRPC_SKIP_BUILD:-0}" == "1" ]]; then
    echo "[rpc-client-reactor] skip build"
else
    echo "[rpc-client-reactor] build project"
    ./build.sh
fi

run_test() {
    local name="$1"
    local bin="${BUILD_DIR}/${name}"

    if [[ ! -x "${bin}" ]]; then
        echo "[rpc-client-reactor] FAIL: missing executable ${bin}"
        exit 1
    fi

    echo "[rpc-client-reactor] run ${name}"
    "${bin}"
}

run_test test_tcp_client
run_test test_connection_codec
run_test test_tinypb_rpc_channel
run_test test_timer
run_test test_timer_task
run_test test_reactor

echo "[rpc-client-reactor] run sync rpc safety net"
MYTINYRPC_SKIP_BUILD=1 ./scripts/check_rpc_sync.sh

echo "[rpc-client-reactor] PASS"
