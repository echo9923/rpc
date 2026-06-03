#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
SERVER_BIN="${BUILD_DIR}/test_tinypb_server_client"
PORT="24139"
SERVER_PID=""

cleanup() {
    if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
        kill "${SERVER_PID}" 2>/dev/null || true
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

cd "${ROOT_DIR}"

if [[ ! -x "${SERVER_BIN}" ]]; then
    echo "[stage8] missing executable: ${SERVER_BIN}"
    exit 1
fi

"${SERVER_BIN}" --server "${PORT}" &
SERVER_PID="$!"

for _ in $(seq 1 50); do
    if "${SERVER_BIN}" --probe "${PORT}" >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done

"${SERVER_BIN}" --probe "${PORT}" >/dev/null
"${SERVER_BIN}" --client "${PORT}"

echo "[stage8] PASS"
