#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

cd "${ROOT_DIR}"

if [[ "${MYTINYRPC_SKIP_BUILD:-0}" == "1" ]]; then
    echo "[coroutine-hook] skip build"
else
    echo "[coroutine-hook] build project"
    ./build.sh
fi

run_test() {
    local name="$1"
    local bin="${BUILD_DIR}/${name}"

    if [[ ! -x "${bin}" ]]; then
        echo "[coroutine-hook] FAIL: missing executable ${bin}"
        exit 1
    fi

    echo "[coroutine-hook] run ${name}"
    "${bin}"
}

run_test test_coroutine
run_test test_coroutinepool
run_test test_memory_pool
run_test test_fdevent
run_test test_reactor
run_test test_hook
run_test test_hook_sleep
run_test test_hook_socket

echo "[coroutine-hook] PASS"
