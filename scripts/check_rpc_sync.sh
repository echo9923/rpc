#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

cd "${ROOT_DIR}"

if [[ "${MYTINYRPC_SKIP_BUILD:-0}" == "1" ]]; then
    echo "[rpc-sync] skip build"
else
    echo "[rpc-sync] build project"
    ./build.sh
fi

run_test() {
    local name="$1"
    local bin="${BUILD_DIR}/${name}"

    if [[ ! -x "${bin}" ]]; then
        echo "[rpc-sync] FAIL: missing executable ${bin}"
        exit 1
    fi

    echo "[rpc-sync] run ${name}"
    "${bin}"
}

run_test test_tcp_buffer
run_test test_abstract_codec
run_test test_tinypb_data
run_test test_tinypb_codec
run_test test_connection_codec
run_test test_protobuf_service
run_test test_tinypb_dispatcher
run_test test_req_id
run_test test_tcp_client
run_test test_tinypb_rpc_channel

echo "[rpc-sync] run stage8 end-to-end rpc"
./scripts/check_stage8_rpc.sh

echo "[rpc-sync] PASS"
