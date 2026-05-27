#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
SERVER_BIN="$BUILD_DIR/test_tcp_echo_server"

if ! command -v cmake >/dev/null 2>&1; then
    echo "[build] FAIL: cmake is required but not found"
    echo "[build] install dependencies in WSL:"
    echo "[build]   sudo apt update"
    echo "[build]   sudo apt install -y build-essential cmake netcat-openbsd"
    exit 1
fi

if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
else
    JOBS="4"
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$BUILD_DIR" --parallel "$JOBS"

echo "[build] output: $SERVER_BIN"
