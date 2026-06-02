#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

cd "${ROOT_DIR}"

echo "[rpc-async] build project"
./build.sh

run_test() {
    local name="$1"
    local bin="${BUILD_DIR}/${name}"

    if [[ ! -x "${bin}" ]]; then
        echo "[rpc-async] FAIL: missing executable ${bin}"
        exit 1
    fi

    echo "[rpc-async] run ${name}"
    "${bin}"
}

run_test test_tinypb_rpc_async_channel
run_test test_tinypb_async_client
run_test test_msg_req
run_test test_timer
run_test test_timer_event
run_test test_tinypb_rpc_channel

echo "[rpc-async] run sync rpc safety net"
./scripts/check_rpc_sync.sh

echo "[rpc-async] PASS"
