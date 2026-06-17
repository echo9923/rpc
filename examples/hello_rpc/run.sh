#!/usr/bin/env bash
#
# run.sh -- hello_rpc 示例一键运行脚本。
#
# 流程：
#   1. 编译项目
#   2. 后台启动 TinyPB 服务端，轮询端口就绪
#   3. 运行 TinyPB 客户端（同步 + 异步）
#   4. 后台启动 HTTP 服务端，轮询端口就绪
#   5. 用 curl 验证各 HTTP 路由
#   6. 清理进程，输出最终结果
set -euo pipefail

# run.sh 位于 examples/hello_rpc/，需上两级才到仓库根
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${MYTINYRPC_BUILD_DIR:-${ROOT_DIR}/build}"
TINYPB_SERVER_BIN="${BUILD_DIR}/hello_tinypb_server"
TINYPB_CLIENT_BIN="${BUILD_DIR}/hello_tinypb_client"
HTTP_SERVER_BIN="${BUILD_DIR}/hello_http_server"
CONFIG_FILE="${ROOT_DIR}/examples/hello_rpc/conf/tinypb_server.xml"
TINYPB_PORT="23456"
HTTP_PORT="23457"
TINYPB_PID=""
HTTP_PID=""

cleanup() {
    if [[ -n "${TINYPB_PID}" ]] && kill -0 "${TINYPB_PID}" 2>/dev/null; then
        kill "${TINYPB_PID}" 2>/dev/null || true
        wait "${TINYPB_PID}" 2>/dev/null || true
    fi
    if [[ -n "${HTTP_PID}" ]] && kill -0 "${HTTP_PID}" 2>/dev/null; then
        kill "${HTTP_PID}" 2>/dev/null || true
        wait "${HTTP_PID}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

cd "${ROOT_DIR}"

# curl 用于探测/验证 HTTP 服务端，缺失则直接报错。
if ! command -v curl >/dev/null 2>&1; then
    echo "[hello_rpc] FAIL: curl is required to verify HTTP server"
    exit 1
fi

if [[ "${MYTINYRPC_SKIP_BUILD:-0}" == "1" ]]; then
    echo "[hello_rpc] skip build"
else
    echo "[hello_rpc] build project"
    ./build.sh
fi

for bin in "${TINYPB_SERVER_BIN}" "${TINYPB_CLIENT_BIN}" "${HTTP_SERVER_BIN}"; do
    if [[ ! -x "${bin}" ]]; then
        echo "[hello_rpc] FAIL: missing executable ${bin}"
        exit 1
    fi
done

if [[ ! -f "${CONFIG_FILE}" ]]; then
    echo "[hello_rpc] FAIL: missing config ${CONFIG_FILE}"
    exit 1
fi

echo "[hello_rpc] start tinypb server on port ${TINYPB_PORT}"
"${TINYPB_SERVER_BIN}" "${CONFIG_FILE}" &
TINYPB_PID="$!"

echo "[hello_rpc] wait for tinypb server ready"
for _ in $(seq 1 50); do
    if "${TINYPB_SERVER_BIN}" --probe "${TINYPB_PORT}" >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done
if ! "${TINYPB_SERVER_BIN}" --probe "${TINYPB_PORT}" >/dev/null 2>&1; then
    echo "[hello_rpc] FAIL: tinypb server not ready on port ${TINYPB_PORT}"
    exit 1
fi

echo "[hello_rpc] run tinypb client (sync + async)"
if ! "${TINYPB_CLIENT_BIN}" "${TINYPB_PORT}"; then
    echo "[hello_rpc] FAIL: tinypb client failed"
    exit 1
fi

echo "[hello_rpc] start http server on port ${HTTP_PORT}"
"${HTTP_SERVER_BIN}" --server "${HTTP_PORT}" &
HTTP_PID="$!"

echo "[hello_rpc] wait for http server ready"
http_ready="0"
for _ in $(seq 1 50); do
    if curl -s --max-time 2 -o /dev/null "http://127.0.0.1:${HTTP_PORT}/hello" 2>/dev/null; then
        http_ready="1"
        break
    fi
    sleep 0.1
done
if [[ "${http_ready}" != "1" ]]; then
    echo "[hello_rpc] FAIL: http server not ready on port ${HTTP_PORT}"
    exit 1
fi

echo "[hello_rpc] verify http routes"

check_body() {
    local label="$1" expected="$2" actual="$3"
    if [[ "${actual}" != "${expected}" ]]; then
        echo "[hello_rpc] FAIL: ${label} body mismatch"
        echo "    expected: ${expected}"
        echo "    actual:   ${actual}"
        exit 1
    fi
}

hello_body="$(curl -s --max-time 3 "http://127.0.0.1:${HTTP_PORT}/hello?name=rpc")"
check_body "/hello?name=rpc" "hello rpc" "${hello_body}"
echo "  /hello?name=rpc -> ${hello_body}"

json_body="$(curl -s --max-time 3 "http://127.0.0.1:${HTTP_PORT}/api/json?name=rpc")"
check_body "/api/json?name=rpc" '{"service":"hello_rpc","name":"rpc","method":"GET"}' "${json_body}"
echo "  /api/json?name=rpc -> ${json_body}"

echo_body="$(curl -s --max-time 3 -d 'ping' "http://127.0.0.1:${HTTP_PORT}/echo")"
check_body "POST /echo -d ping" "ping" "${echo_body}"
echo "  POST /echo -d ping -> ${echo_body}"

error_code="$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' "http://127.0.0.1:${HTTP_PORT}/error")"
if [[ "${error_code}" != "500" ]]; then
    echo "[hello_rpc] FAIL: /error expected HTTP 500, got ${error_code}"
    exit 1
fi
echo "  /error -> HTTP ${error_code}"

echo "[hello_rpc] PASS"
