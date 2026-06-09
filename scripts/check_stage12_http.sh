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
    echo "[stage12-http] missing executable: ${SERVER_BIN}"
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

http_request() {
    local method="$1"
    local path="$2"
    local body="${3:-}"
    local length="${#body}"

    if [[ "${method}" == "POST" ]]; then
        printf "%s %s HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: %s\r\nConnection: close\r\n\r\n%s" \
            "${method}" "${path}" "${length}" "${body}" \
            | nc -w 3 127.0.0.1 "${PORT}"
    else
        printf "%s %s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n" "${method}" "${path}" \
            | nc -w 3 127.0.0.1 "${PORT}"
    fi
}

http_status() {
    printf "%s" "$1" | awk 'NR == 1 { print $2 }'
}

http_body() {
    printf "%s" "$1" | awk '
        NR == 1 {
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

http_header() {
    local response="$1"
    local header_name="$2"

    printf "%s" "${response}" | awk -v target="${header_name}" '
        BEGIN {
            lower_target = tolower(target)
        }
        NR == 1 {
            next
        }
        $0 == "\r" || $0 == "" {
            exit
        }
        {
            line = $0
            sub(/\r$/, "", line)
            colon = index(line, ":")
            if (colon == 0) {
                next
            }
            name = tolower(substr(line, 1, colon - 1))
            value = substr(line, colon + 1)
            sub(/^[ ]+/, "", value)
            if (name == lower_target) {
                print value
                exit
            }
        }
    '
}

assert_response() {
    local label="$1"
    local response="$2"
    local expected_status="$3"
    local expected_body="$4"

    local status
    local body
    local connection
    local content_length

    status="$(http_status "${response}")"
    body="$(http_body "${response}")"
    connection="$(http_header "${response}" "Connection")"
    content_length="$(http_header "${response}" "Content-Length")"

    if [[ "${status}" != "${expected_status}" ]]; then
        echo "[stage12-http] unexpected ${label} status: ${status}"
        exit 1
    fi
    if [[ "${body}" != "${expected_body}" ]]; then
        echo "[stage12-http] unexpected ${label} body: ${body}"
        exit 1
    fi
    if [[ "${connection}" != "close" ]]; then
        echo "[stage12-http] unexpected ${label} connection: ${connection}"
        exit 1
    fi
    if [[ "${content_length}" != "${#expected_body}" ]]; then
        echo "[stage12-http] unexpected ${label} content-length: ${content_length}"
        exit 1
    fi
}

hello_response="$(http_request "GET" "/hello")"
hello_status="$(http_status "${hello_response}")"
hello_body="$(http_body "${hello_response}")"
if [[ "${hello_status}" != "200" ]]; then
    echo "[stage12-http] unexpected /hello status: ${hello_status}"
    exit 1
fi
if [[ "${hello_body}" != "hello http" ]]; then
    echo "[stage12-http] unexpected /hello body: ${hello_body}"
    exit 1
fi

assert_response "/hello header" "${hello_response}" "200" "hello http"

query_response="$(http_request "GET" "/hello?name=alice")"
assert_response "/hello query" "${query_response}" "200" "hello alice"

missing_response="$(http_request "GET" "/missing")"
assert_response "/missing" "${missing_response}" "404" "Not Found"

error_response="$(http_request "GET" "/error")"
assert_response "/error" "${error_response}" "500" "Internal Server Error"

post_response="$(http_request "POST" "/submit" "posted body")"
assert_response "/submit" "${post_response}" "200" "posted body"

echo "[stage12-http] PASS"
