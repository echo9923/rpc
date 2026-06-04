#!/usr/bin/env bash
#
# run.sh -- E2E 联动测试入口脚本。
#
# 流程：
#   1. 编译项目
#   2. 后台启动 e2e_server
#   3. 轮询探测端口就绪
#   4. 运行 e2e_client
#   5. 清理 server 进程
#   6. 输出最终结果
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
SERVER_BIN="${BUILD_DIR}/e2e_server"
CLIENT_BIN="${BUILD_DIR}/e2e_client"
CONFIG_FILE="${ROOT_DIR}/e2e/conf/e2e_server.xml"
PORT="32168"
SERVER_PID=""

cleanup() {
    if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
        kill "${SERVER_PID}" 2>/dev/null || true
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

cd "${ROOT_DIR}"

if [[ "${MYTINYRPC_SKIP_BUILD:-0}" == "1" ]]; then
    echo "[e2e] skip build"
else
    echo "[e2e] build project"
    ./build.sh
fi

if [[ ! -x "${SERVER_BIN}" ]]; then
    echo "[e2e] FAIL: missing executable ${SERVER_BIN}"
    exit 1
fi

if [[ ! -x "${CLIENT_BIN}" ]]; then
    echo "[e2e] FAIL: missing executable ${CLIENT_BIN}"
    exit 1
fi

if [[ ! -f "${CONFIG_FILE}" ]]; then
    echo "[e2e] FAIL: missing config ${CONFIG_FILE}"
    exit 1
fi

echo "[e2e] start server on port ${PORT}"
"${SERVER_BIN}" "${CONFIG_FILE}" &
SERVER_PID="$!"

echo "[e2e] wait for server ready"
for _ in $(seq 1 50); do
    if "${SERVER_BIN}" --probe "${PORT}" >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done

if ! "${SERVER_BIN}" --probe "${PORT}" >/dev/null 2>&1; then
    echo "[e2e] FAIL: server not ready on port ${PORT}"
    exit 1
fi

echo "[e2e] run client"
"${CLIENT_BIN}" "${PORT}"
CLIENT_EXIT=$?

if [[ ${CLIENT_EXIT} -ne 0 ]]; then
    echo "[e2e] FAIL: client exited with ${CLIENT_EXIT}"
    exit 1
fi

echo "[e2e] PASS"
