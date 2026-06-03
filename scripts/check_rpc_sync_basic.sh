#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

cd "${ROOT_DIR}"

echo "[rpc-sync-basic] build project"
./build.sh

run_test() {
    local name="$1"
    local bin="${BUILD_DIR}/${name}"

    if [[ ! -x "${bin}" ]]; then
        echo "[rpc-sync-basic] FAIL: missing executable ${bin}"
        exit 1
    fi

    echo "[rpc-sync-basic] run ${name}"
    "${bin}"
}

run_test test_tinypb_codec
run_test test_tinypb_dispatcher
run_test test_tcp_client
run_test test_tinypb_rpc_channel

echo "[rpc-sync-basic] run stage8 end-to-end rpc"
./scripts/check_stage8_rpc.sh

echo "[rpc-sync-basic] PASS"
