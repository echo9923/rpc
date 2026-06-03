#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/build/generated_task80"
GENERATOR="${ROOT_DIR}/generator/tinyrpc_generator.py"
PROTO_FILE="${ROOT_DIR}/testcases/test_tinypb_server.proto"
BAD_PROTO_FILE="${ROOT_DIR}/testcases/not_exists.proto"
BAD_SERVICE_DIR="${ROOT_DIR}/build/generated_task80_bad_service"

cd "${ROOT_DIR}"

rm -rf "${OUT_DIR}" "${BAD_SERVICE_DIR}"

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

echo "[generator] run generator"
run_generator "${PROTO_FILE}" "QueryService" "${OUT_DIR}"

required_files=(
    "CMakeLists.txt"
    "README.md"
    "conf.xml"
    "interface.h"
    "interface.cc"
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

grep -q "<host>127.0.0.1</host>" "${OUT_DIR}/conf.xml"
grep -q "<port>39999</port>" "${OUT_DIR}/conf.xml"
grep -q "MYTINYRPC_ROOT" "${OUT_DIR}/CMakeLists.txt"
grep -q "QueryService Generated Project" "${OUT_DIR}/README.md"
grep -q "QueryService" "${OUT_DIR}/main.cc"
grep -q "TinyPbRpcChannel" "${OUT_DIR}/client.cc"
grep -q "class QueryServiceImpl : public QueryService" "${OUT_DIR}/interface.h"
grep -q "void query_name(" "${OUT_DIR}/interface.h"
grep -q "void QueryServiceImpl::query_name(" "${OUT_DIR}/interface.cc"
grep -q "QueryService_Stub" "${OUT_DIR}/client.cc"
grep -q "stub->query_name" "${OUT_DIR}/client.cc"

protoc --proto_path="${OUT_DIR}" --cpp_out="${OUT_DIR}" "${OUT_DIR}/test_tinypb_server.proto"

g++ -std=c++20 -I"${OUT_DIR}" -I"${ROOT_DIR}" -I"${ROOT_DIR}/mytinyrpc" \
    -c "${OUT_DIR}/test_tinypb_server.pb.cc" \
    -o "${OUT_DIR}/test_tinypb_server.pb.o"
g++ -std=c++20 -I"${OUT_DIR}" -I"${ROOT_DIR}" -I"${ROOT_DIR}/mytinyrpc" \
    -c "${OUT_DIR}/interface.cc" \
    -o "${OUT_DIR}/interface.o"
g++ -std=c++20 -I"${OUT_DIR}" -I"${ROOT_DIR}" -I"${ROOT_DIR}/mytinyrpc" \
    -c "${OUT_DIR}/client.cc" \
    -o "${OUT_DIR}/client.o"

if run_generator "${BAD_PROTO_FILE}" "QueryService" "${OUT_DIR}/bad" \
    >/tmp/tinyrpc_generator_bad.out 2>/tmp/tinyrpc_generator_bad.err; then
    echo "[generator] FAIL: invalid proto unexpectedly succeeded"
    exit 1
fi

grep -q "proto file not found" /tmp/tinyrpc_generator_bad.err

if run_generator "${PROTO_FILE}" "NotExists" "${BAD_SERVICE_DIR}" \
    >/tmp/tinyrpc_generator_bad_service.out 2>/tmp/tinyrpc_generator_bad_service.err; then
    echo "[generator] FAIL: invalid service unexpectedly succeeded"
    exit 1
fi

grep -q "service not found in proto" /tmp/tinyrpc_generator_bad_service.err

echo "[generator] PASS"
