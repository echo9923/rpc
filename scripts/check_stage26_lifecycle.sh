#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

cd "${ROOT_DIR}"

if [[ "${MYTINYRPC_SKIP_BUILD:-0}" != "1" ]]; then
    echo "[stage26-lifecycle] build project"
    ./build.sh
fi

if [[ ! -x "${BUILD_DIR}/test_tcpserver_lifecycle" ]]; then
    echo "[stage26-lifecycle] missing executable: ${BUILD_DIR}/test_tcpserver_lifecycle"
    exit 1
fi

echo "[stage26-lifecycle] run TcpServer lifecycle tests"
"${BUILD_DIR}/test_tcpserver_lifecycle"

echo "[stage26-lifecycle] PASS"
