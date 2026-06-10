#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
PORT="${MYTINYRPC_RESOURCE_PORT:-40791}"
COMMAND_TIMEOUT="${MYTINYRPC_RESOURCE_TIMEOUT:-15}"
SERVER_PID=""

cd "${ROOT_DIR}"

cleanup() {
    if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
        kill "${SERVER_PID}" 2>/dev/null || true
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

fail() {
    echo "[resource] FAIL: $*"
    exit 1
}

run_bin() {
    local name="$1"
    local bin="${BUILD_DIR}/${name}"

    if [[ ! -x "${bin}" ]]; then
        fail "missing executable ${bin}"
    fi

    echo "[resource] run ${name}"
    timeout "${COMMAND_TIMEOUT}" "${bin}"
}

wait_server_ready() {
    local server_bin="$1"

    for _ in $(seq 1 50); do
        if "${server_bin}" --probe "${PORT}" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.1
    done
    return 1
}

get_fd_count() {
    local pid="$1"

    if [[ ! -d "/proc/${pid}/fd" ]]; then
        fail "cannot inspect fd directory for pid ${pid}"
    fi
    find "/proc/${pid}/fd" -maxdepth 1 -type l | wc -l
}

if [[ "${MYTINYRPC_SKIP_BUILD:-0}" == "1" ]]; then
    echo "[resource] skip build"
else
    echo "[resource] build project"
    ./build.sh
fi

run_bin benchmark_http
run_bin benchmark_tinypb_sync
run_bin benchmark_tinypb_async
run_bin test_log
run_bin test_thread_pool

SERVER_BIN="${BUILD_DIR}/test_tinypb_server_client"
if [[ ! -x "${SERVER_BIN}" ]]; then
    fail "missing executable ${SERVER_BIN}"
fi

echo "[resource] start TinyPB server"
"${SERVER_BIN}" --server "${PORT}" &
SERVER_PID=$!

if ! wait_server_ready "${SERVER_BIN}"; then
    fail "server did not become ready on port ${PORT}"
fi

FD_BEFORE="$(get_fd_count "${SERVER_PID}")"
echo "[resource] fd_before=${FD_BEFORE}"

for _ in $(seq 1 8); do
    timeout "${COMMAND_TIMEOUT}" "${SERVER_BIN}" --client "${PORT}" >/dev/null
done

sleep 0.3
FD_AFTER="$(get_fd_count "${SERVER_PID}")"
echo "[resource] fd_after=${FD_AFTER}"

if (( FD_AFTER > FD_BEFORE + 2 )); then
    fail "server fd count keeps growing: before=${FD_BEFORE}, after=${FD_AFTER}"
fi

echo "[resource] stop TinyPB server"
OLD_SERVER_PID="${SERVER_PID}"
kill "${SERVER_PID}" 2>/dev/null || true
wait "${SERVER_PID}" 2>/dev/null || true
SERVER_PID=""

if kill -0 "${OLD_SERVER_PID}" 2>/dev/null; then
    fail "server process still exists after shutdown"
fi

echo "[resource] PASS"
