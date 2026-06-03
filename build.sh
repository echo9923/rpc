#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
SERVER_BIN="$BUILD_DIR/test_tcp_echo_server"

print_wsl_dependencies() {
    echo "[build] install dependencies in WSL:"
    echo "[build]   sudo apt update"
    echo "[build]   sudo apt install -y build-essential cmake netcat-openbsd libgtest-dev protobuf-compiler libprotobuf-dev"
}

if ! command -v cmake >/dev/null 2>&1; then
    echo "[build] FAIL: cmake is required but not found"
    print_wsl_dependencies
    exit 1
fi

if ! command -v protoc >/dev/null 2>&1; then
    echo "[build] FAIL: protoc is required but not found"
    print_wsl_dependencies
    exit 1
fi

if [ ! -f "/usr/include/gtest/gtest.h" ] && [ ! -f "/usr/local/include/gtest/gtest.h" ]; then
    echo "[build] FAIL: gtest headers are required but not found"
    print_wsl_dependencies
    exit 1
fi

if [ ! -f "/usr/include/google/protobuf/service.h" ] && [ ! -f "/usr/local/include/google/protobuf/service.h" ]; then
    echo "[build] FAIL: protobuf headers are required but not found"
    print_wsl_dependencies
    exit 1
fi

# WSL 挂载 Windows 工作区时，高并发 Make 生成依赖文件偶发丢失
# compiler_depend.make。默认串行构建保证验收稳定；需要提速时可显式设置。
JOBS="${MYTINYRPC_BUILD_JOBS:-1}"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$BUILD_DIR" --parallel "$JOBS"

echo "[build] output: $SERVER_BIN"
