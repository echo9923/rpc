#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
SERVER_BIN="${BUILD_DIR}/test_http_server"
PORT="24142"
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
    echo "[stage12] missing executable: ${SERVER_BIN}"
    exit 1
fi

"${SERVER_BIN}" --server "${PORT}" &
SERVER_PID="$!"

for _ in $(seq 1 50); do
    if curl --max-time 1 -fsS "http://127.0.0.1:${PORT}/hello" >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done

hello_body="$(curl --max-time 3 -fsS "http://127.0.0.1:${PORT}/hello")"
if [[ "${hello_body}" != "hello http" ]]; then
    echo "[stage12] unexpected /hello body: ${hello_body}"
    exit 1
fi

missing_status="$(curl --max-time 3 -s -o /tmp/stage12_missing_body.txt -w "%{http_code}" "http://127.0.0.1:${PORT}/missing")"
missing_body="$(cat /tmp/stage12_missing_body.txt)"
rm -f /tmp/stage12_missing_body.txt

if [[ "${missing_status}" != "404" ]]; then
    echo "[stage12] unexpected /missing status: ${missing_status}"
    exit 1
fi

if [[ "${missing_body}" != "404 Not Found" ]]; then
    echo "[stage12] unexpected /missing body: ${missing_body}"
    exit 1
fi

echo "[stage12] PASS"
