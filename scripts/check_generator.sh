#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/build/generated_task79"
GENERATOR="${ROOT_DIR}/generator/tinyrpc_generator.py"
PROTO_FILE="${ROOT_DIR}/testcases/test_tinypb_server.proto"
BAD_PROTO_FILE="${ROOT_DIR}/testcases/not_exists.proto"

cd "${ROOT_DIR}"

rm -rf "${OUT_DIR}"

run_generator() {
    local proto_file="$1"
    local service_name="$2"
    local out_dir="$3"

    if command -v python3 >/dev/null 2>&1; then
        python3 "${GENERATOR}" --proto "${proto_file}" --service "${service_name}" --out "${out_dir}"
        return
    fi
    if command -v python >/dev/null 2>&1; then
        python "${GENERATOR}" --proto "${proto_file}" --service "${service_name}" --out "${out_dir}"
        return
    fi

    echo "[generator] FAIL: python3 or python is required"
    echo "[generator] install in WSL: sudo apt update && sudo apt install -y python3"
    exit 1
}

echo "[generator] run task79 generator"
run_generator "${PROTO_FILE}" "QueryService" "${OUT_DIR}"

required_files=(
    "conf.xml"
    "main.cc"
    "server.h"
    "server.cc"
    "client.cc"
    "run.sh"
    "shutdown.sh"
    "test_tinypb_server.proto"
)

for file in "${required_files[@]}"; do
    if [[ ! -f "${OUT_DIR}/${file}" ]]; then
        echo "[generator] FAIL: missing generated file ${file}"
        exit 1
    fi
done

grep -q "QueryService" "${OUT_DIR}/conf.xml"
grep -q "QueryService" "${OUT_DIR}/main.cc"
grep -q "test_tinypb_server.proto" "${OUT_DIR}/client.cc"

if run_generator "${BAD_PROTO_FILE}" "QueryService" "${OUT_DIR}/bad" \
    >/tmp/tinyrpc_generator_bad.out 2>/tmp/tinyrpc_generator_bad.err; then
    echo "[generator] FAIL: invalid proto unexpectedly succeeded"
    exit 1
fi

grep -q "proto file not found" /tmp/tinyrpc_generator_bad.err

echo "[generator] PASS"
