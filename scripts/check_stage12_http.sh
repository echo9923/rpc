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
    if printf "GET /hello HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n" \
        | nc -w 1 127.0.0.1 "${PORT}" >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done

http_get() {
    local path="$1"
    local response
    response="$(
        printf "GET %s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n" "${path}" \
            | nc -w 3 127.0.0.1 "${PORT}"
    )"

    printf "%s" "${response}" | awk '
        NR == 1 {
            print $2
            next
        }
        body_started {
            print
            next
        }
        $0 == "\r" || $0 == "" {
            body_started = 1
        }
    ' | tr -d '\r'
}

hello_response="$(http_get "/hello")"
hello_status="$(printf "%s" "${hello_response}" | sed -n '1p')"
hello_body="$(printf "%s" "${hello_response}" | sed -n '2,$p')"
if [[ "${hello_status}" != "200" ]]; then
    echo "[stage12] unexpected /hello status: ${hello_status}"
    exit 1
fi
if [[ "${hello_body}" != "hello http" ]]; then
    echo "[stage12] unexpected /hello body: ${hello_body}"
    exit 1
fi

missing_response="$(http_get "/missing")"
missing_status="$(printf "%s" "${missing_response}" | sed -n '1p')"
missing_body="$(printf "%s" "${missing_response}" | sed -n '2,$p')"

if [[ "${missing_status}" != "404" ]]; then
    echo "[stage12] unexpected /missing status: ${missing_status}"
    exit 1
fi

if [[ "${missing_body}" != "404 Not Found" ]]; then
    echo "[stage12] unexpected /missing body: ${missing_body}"
    exit 1
fi

echo "[stage12] PASS"
