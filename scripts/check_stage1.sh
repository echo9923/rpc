#!/usr/bin/env bash

set -e

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
SERVER_BIN="$BUILD_DIR/test_tcp_echo_server"
SERVER_LOG="$BUILD_DIR/stage1_server.log"
HOST="127.0.0.1"
PORT="19999"

cd "$ROOT_DIR"

echo "[stage1] build project"
./build.sh

if ! command -v nc >/dev/null 2>&1; then
  echo "[stage1] FAIL: nc is required but not found"
  echo "[stage1] install netcat-openbsd or another nc-compatible package"
  exit 1
fi

NC_CLOSE_ARGS=("-w" "3")
if nc -h 2>&1 | grep -q -- "-N"; then
  NC_CLOSE_ARGS=("-N" "-w" "3")
elif nc -h 2>&1 | grep -q -- "-q"; then
  NC_CLOSE_ARGS=("-q" "1" "-w" "3")
fi

send_echo() {
  nc "${NC_CLOSE_ARGS[@]}" "$HOST" "$PORT"
}

check_port() {
  nc -z -w 1 "$HOST" "$PORT" >/dev/null 2>&1
}

if [ ! -x "$SERVER_BIN" ]; then
  echo "[stage1] FAIL: server binary not found: $SERVER_BIN"
  exit 1
fi

echo "[stage1] start server"
"$SERVER_BIN" > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!

cleanup() {
  if kill -0 "$SERVER_PID" >/dev/null 2>&1; then
    kill "$SERVER_PID" >/dev/null 2>&1 || true
    wait "$SERVER_PID" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

echo "[stage1] wait server"
for i in $(seq 1 30); do
  if check_port; then
    break
  fi
  sleep 0.1
done

if ! check_port; then
  echo "[stage1] FAIL: server did not listen on $HOST:$PORT"
  echo "[stage1] server log:"
  cat "$SERVER_LOG"
  exit 1
fi

echo "[stage1] run echo test 1"
EXPECTED="hello tinyrpc"
ACTUAL="$(printf "%s\n" "$EXPECTED" | send_echo)"
ACTUAL="${ACTUAL%$'\n'}"

if [ "$ACTUAL" != "$EXPECTED" ]; then
  echo "[stage1] FAIL: echo mismatch"
  echo "[stage1] expected: $EXPECTED"
  echo "[stage1] actual:   $ACTUAL"
  echo "[stage1] server log:"
  cat "$SERVER_LOG"
  exit 1
fi

echo "[stage1] run echo test 2"
EXPECTED_MULTI="line-1
line-2
line-3"
ACTUAL_MULTI="$(printf "%s\n" "$EXPECTED_MULTI" | send_echo)"
ACTUAL_MULTI="${ACTUAL_MULTI%$'\n'}"

if [ "$ACTUAL_MULTI" != "$EXPECTED_MULTI" ]; then
  echo "[stage1] FAIL: multi-line echo mismatch"
  echo "[stage1] expected:"
  printf "%s\n" "$EXPECTED_MULTI"
  echo "[stage1] actual:"
  printf "%s\n" "$ACTUAL_MULTI"
  echo "[stage1] server log:"
  cat "$SERVER_LOG"
  exit 1
fi

echo "[stage1] PASS"
