#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/build/generated_task111_simple"
FULL_OUT_DIR="${ROOT_DIR}/build/generated_task111_full"
MULTI_PROTO_FILE="${ROOT_DIR}/build/generated_task113_multiservice.proto"
MULTI_OUT_DIR="${ROOT_DIR}/build/generated_task113_multiservice"
GENERATOR="${ROOT_DIR}/generator/tinyrpc_generator.py"
PROTO_FILE="${ROOT_DIR}/testcases/test_tinypb_server.proto"
BAD_PROTO_FILE="${ROOT_DIR}/testcases/not_exists.proto"
BAD_SERVICE_DIR="${ROOT_DIR}/build/generated_task111_bad_service"
BAD_PROTO_DIR="${ROOT_DIR}/build/generated_task112_bad_proto"
NO_PROTOC_DIR="${ROOT_DIR}/build/generated_task112_no_protoc"

cd "${ROOT_DIR}"

PYTHON_BIN=""
if command -v python3 >/dev/null 2>&1; then
    PYTHON_BIN="$(command -v python3)"
elif command -v python >/dev/null 2>&1; then
    PYTHON_BIN="$(command -v python)"
else
    echo "[generator] FAIL: python3 or python is required"
    echo "[generator] install in WSL: sudo apt update && sudo apt install -y python3"
    exit 1
fi

rm -rf \
    "${OUT_DIR}" \
    "${FULL_OUT_DIR}" \
    "${MULTI_OUT_DIR}" \
    "${BAD_SERVICE_DIR}" \
    "${BAD_PROTO_DIR}" \
    "${NO_PROTOC_DIR}"

run_generator() {
    "${PYTHON_BIN}" "${GENERATOR}" "$@"
}

assert_file() {
    local file="$1"
    if [[ ! -f "${file}" ]]; then
        echo "[generator] FAIL: missing generated file ${file}"
        exit 1
    fi
}

assert_dir() {
    local dir="$1"
    if [[ ! -d "${dir}" ]]; then
        echo "[generator] FAIL: missing generated directory ${dir}"
        exit 1
    fi
}

assert_grep() {
    local pattern="$1"
    local file="$2"
    if ! grep -q "${pattern}" "${file}"; then
        echo "[generator] FAIL: ${file} does not contain ${pattern}"
        exit 1
    fi
}

echo "[generator] run simple layout"
run_generator --proto "${PROTO_FILE}" --service "QueryService" --out "${OUT_DIR}"

simple_required_files=(
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
    "test_tinypb_server.pb.h"
    "test_tinypb_server.pb.cc"
    "test_tinypb_server.descriptor.pb"
)

for file in "${simple_required_files[@]}"; do
    assert_file "${OUT_DIR}/${file}"
done

assert_grep "<host>127.0.0.1</host>" "${OUT_DIR}/conf.xml"
assert_grep "<port>39999</port>" "${OUT_DIR}/conf.xml"
assert_grep "MYTINYRPC_ROOT" "${OUT_DIR}/CMakeLists.txt"
assert_grep "QueryService Generated Project" "${OUT_DIR}/README.md"
assert_grep 'Layout: `simple`' "${OUT_DIR}/README.md"
assert_grep "QueryService" "${OUT_DIR}/main.cc"
assert_grep "TinyPbRpcChannel" "${OUT_DIR}/client.cc"
assert_grep "TinyPbRpcChannel::Ptr" "${OUT_DIR}/client.cc"
assert_grep "setReuseConnection(true)" "${OUT_DIR}/client.cc"
assert_grep "class QueryServiceImpl : public QueryService" "${OUT_DIR}/interface.h"
assert_grep "void query_name(" "${OUT_DIR}/interface.h"
assert_grep "void QueryServiceImpl::query_name(" "${OUT_DIR}/interface.cc"
assert_grep "QueryService_Stub" "${OUT_DIR}/client.cc"
assert_grep "stub->query_name" "${OUT_DIR}/client.cc"

g++ -std=c++20 -I"${OUT_DIR}" -I"${ROOT_DIR}" -I"${ROOT_DIR}/mytinyrpc" \
    -c "${OUT_DIR}/test_tinypb_server.pb.cc" \
    -o "${OUT_DIR}/test_tinypb_server.pb.o"
g++ -std=c++20 -I"${OUT_DIR}" -I"${ROOT_DIR}" -I"${ROOT_DIR}/mytinyrpc" \
    -c "${OUT_DIR}/interface.cc" \
    -o "${OUT_DIR}/interface.o"
g++ -std=c++20 -I"${OUT_DIR}" -I"${ROOT_DIR}" -I"${ROOT_DIR}/mytinyrpc" \
    -c "${OUT_DIR}/client.cc" \
    -o "${OUT_DIR}/client.o"

echo "[generator] run full layout"
run_generator --proto "${PROTO_FILE}" --service "QueryService" --layout full --out "${FULL_OUT_DIR}"

full_required_dirs=(
    "bin"
    "conf"
    "log"
    "lib"
    "obj"
    "query_service/service"
    "query_service/interface"
    "query_service/pb"
    "query_service/comm"
    "test_client"
)

for dir in "${full_required_dirs[@]}"; do
    assert_dir "${FULL_OUT_DIR}/${dir}"
done

full_required_files=(
    "CMakeLists.txt"
    "README.md"
    "conf/conf.xml"
    "query_service/comm/business_exception.h"
    "query_service/interface/interface_base.h"
    "query_service/interface/interface_base.cc"
    "query_service/interface/query_name_interface.h"
    "query_service/interface/query_name_interface.cc"
    "query_service/pb/test_tinypb_server.proto"
    "query_service/pb/test_tinypb_server.pb.h"
    "query_service/pb/test_tinypb_server.pb.cc"
    "query_service/pb/test_tinypb_server.descriptor.pb"
    "query_service/service/main.cc"
    "query_service/service/server.h"
    "query_service/service/server.cc"
    "test_client/test_tinyrpc_client.cc"
    "run.sh"
    "shutdown.sh"
)

for file in "${full_required_files[@]}"; do
    assert_file "${FULL_OUT_DIR}/${file}"
done

assert_grep 'Layout: `full`' "${FULL_OUT_DIR}/README.md"
assert_grep "class BusinessException" "${FULL_OUT_DIR}/query_service/comm/business_exception.h"
assert_grep "class QueryNameInterface : public InterfaceBase" \
    "${FULL_OUT_DIR}/query_service/interface/query_name_interface.h"
assert_grep "m_queryNameInterface.handle" "${FULL_OUT_DIR}/query_service/service/server.cc"
assert_grep "REGISTER_SERVICE(QueryServiceImpl)" "${FULL_OUT_DIR}/query_service/service/main.cc"
assert_grep "test_client/test_tinyrpc_client.cc" "${FULL_OUT_DIR}/CMakeLists.txt"
assert_grep "TinyPbRpcChannel::Ptr" "${FULL_OUT_DIR}/test_client/test_tinyrpc_client.cc"
assert_grep "setReuseConnection(true)" "${FULL_OUT_DIR}/test_client/test_tinyrpc_client.cc"

g++ -std=c++20 -I"${FULL_OUT_DIR}" -I"${ROOT_DIR}" -I"${ROOT_DIR}/mytinyrpc" \
    -c "${FULL_OUT_DIR}/query_service/pb/test_tinypb_server.pb.cc" \
    -o "${FULL_OUT_DIR}/query_service/pb/test_tinypb_server.pb.o"
g++ -std=c++20 -I"${FULL_OUT_DIR}" -I"${ROOT_DIR}" -I"${ROOT_DIR}/mytinyrpc" \
    -c "${FULL_OUT_DIR}/query_service/interface/query_name_interface.cc" \
    -o "${FULL_OUT_DIR}/query_service/interface/query_name_interface.o"
g++ -std=c++20 -I"${FULL_OUT_DIR}" -I"${ROOT_DIR}" -I"${ROOT_DIR}/mytinyrpc" \
    -c "${FULL_OUT_DIR}/query_service/service/server.cc" \
    -o "${FULL_OUT_DIR}/query_service/service/server.o"
g++ -std=c++20 -I"${FULL_OUT_DIR}" -I"${ROOT_DIR}" -I"${ROOT_DIR}/mytinyrpc" \
    -c "${FULL_OUT_DIR}/test_client/test_tinyrpc_client.cc" \
    -o "${FULL_OUT_DIR}/test_client/test_tinyrpc_client.o"

cat >"${MULTI_PROTO_FILE}" <<'PROTO'
syntax = "proto3";

package stage23;

option cc_generic_services = true;

message PingRequest {
  int32 id = 1;
}

message PingResponse {
  string text = 1;
}

message EchoRequest {
  string text = 1;
}

message EchoResponse {
  string text = 1;
}

service AlphaService {
  rpc ping(PingRequest) returns (PingResponse);
}

service BetaService {
  rpc ping(PingRequest) returns (PingResponse);
  rpc echo(EchoRequest) returns (EchoResponse);
}
PROTO

echo "[generator] run descriptor service selection"
run_generator --proto "${MULTI_PROTO_FILE}" --service "stage23.BetaService" --layout full --out "${MULTI_OUT_DIR}"

assert_file "${MULTI_OUT_DIR}/beta_service/pb/generated_task113_multiservice.pb.h"
assert_file "${MULTI_OUT_DIR}/beta_service/interface/ping_interface.h"
assert_file "${MULTI_OUT_DIR}/beta_service/interface/echo_interface.h"
assert_grep "namespace stage23" "${MULTI_OUT_DIR}/beta_service/service/server.h"
assert_grep "class BetaServiceImpl : public BetaService" "${MULTI_OUT_DIR}/beta_service/service/server.h"
assert_grep "REGISTER_SERVICE(stage23::BetaServiceImpl)" "${MULTI_OUT_DIR}/beta_service/service/main.cc"
assert_grep "stage23::BetaService_Stub" "${MULTI_OUT_DIR}/test_client/test_tinyrpc_client.cc"
assert_grep "m_pingInterface.handle" "${MULTI_OUT_DIR}/beta_service/service/server.cc"
assert_grep "m_echoInterface.handle" "${MULTI_OUT_DIR}/beta_service/service/server.cc"

g++ -std=c++20 -I"${MULTI_OUT_DIR}" -I"${ROOT_DIR}" -I"${ROOT_DIR}/mytinyrpc" \
    -c "${MULTI_OUT_DIR}/beta_service/service/main.cc" \
    -o "${MULTI_OUT_DIR}/beta_service/service/main.o"
g++ -std=c++20 -I"${MULTI_OUT_DIR}" -I"${ROOT_DIR}" -I"${ROOT_DIR}/mytinyrpc" \
    -c "${MULTI_OUT_DIR}/beta_service/service/server.cc" \
    -o "${MULTI_OUT_DIR}/beta_service/service/server.o"
g++ -std=c++20 -I"${MULTI_OUT_DIR}" -I"${ROOT_DIR}" -I"${ROOT_DIR}/mytinyrpc" \
    -c "${MULTI_OUT_DIR}/test_client/test_tinyrpc_client.cc" \
    -o "${MULTI_OUT_DIR}/test_client/test_tinyrpc_client.o"

if run_generator --proto "${BAD_PROTO_FILE}" --service "QueryService" --out "${OUT_DIR}/bad" \
    >/tmp/tinyrpc_generator_bad.out 2>/tmp/tinyrpc_generator_bad.err; then
    echo "[generator] FAIL: invalid proto unexpectedly succeeded"
    exit 1
fi

assert_grep "proto file not found" /tmp/tinyrpc_generator_bad.err

cat >"${BAD_PROTO_DIR}.proto" <<'PROTO'
syntax = "proto3";

service BrokenService {
  rpc bad(BrokenRequest) returns (BrokenResponse)
}
PROTO

if run_generator --proto "${BAD_PROTO_DIR}.proto" --service "BrokenService" --out "${BAD_PROTO_DIR}" \
    >/tmp/tinyrpc_generator_bad_proto.out 2>/tmp/tinyrpc_generator_bad_proto.err; then
    echo "[generator] FAIL: invalid proto syntax unexpectedly succeeded"
    exit 1
fi

assert_grep "protoc failed" /tmp/tinyrpc_generator_bad_proto.err

if PATH="/tmp" "${PYTHON_BIN}" "${GENERATOR}" --proto "${PROTO_FILE}" --service "QueryService" --out "${NO_PROTOC_DIR}" \
    >/tmp/tinyrpc_generator_no_protoc.out 2>/tmp/tinyrpc_generator_no_protoc.err; then
    echo "[generator] FAIL: missing protoc unexpectedly succeeded"
    exit 1
fi

assert_grep "protoc is required but not found" /tmp/tinyrpc_generator_no_protoc.err

if run_generator --proto "${PROTO_FILE}" --service "NotExists" --out "${BAD_SERVICE_DIR}" \
    >/tmp/tinyrpc_generator_bad_service.out 2>/tmp/tinyrpc_generator_bad_service.err; then
    echo "[generator] FAIL: invalid service unexpectedly succeeded"
    exit 1
fi

assert_grep "service not found in proto" /tmp/tinyrpc_generator_bad_service.err

echo "[generator] PASS"
