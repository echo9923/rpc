#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/build/generated_task132_release_project"
GENERATOR="${ROOT_DIR}/generator/tinyrpc_generator.py"
PROTO_FILE="${ROOT_DIR}/testcases/test_tinypb_server.proto"
PORT="39999"

cleanup() {
    if [[ -f "${OUT_DIR}/shutdown.sh" ]]; then
        bash "${OUT_DIR}/shutdown.sh" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

cd "${ROOT_DIR}"

rm -rf "${OUT_DIR}"

assert_file() {
    local file="$1"
    if [[ ! -f "${file}" ]]; then
        echo "[generator-release] FAIL: missing file ${file}"
        exit 1
    fi
}

assert_grep() {
    local pattern="$1"
    local file="$2"
    if ! grep -q "${pattern}" "${file}"; then
        echo "[generator-release] FAIL: ${file} does not contain ${pattern}"
        exit 1
    fi
}

echo "[generator-release] generate release package"
python3 "${GENERATOR}" \
    --proto "${PROTO_FILE}" \
    --service "QueryService" \
    --layout full \
    --package release \
    --out "${OUT_DIR}"

assert_file "${OUT_DIR}/third_party/MYTINYRPC_MANIFEST.md"
assert_file "${OUT_DIR}/third_party/mytinyrpc/mytinyrpc/net/tinypb/tinypbrpcchannel.cc"
assert_file "${OUT_DIR}/third_party/mytinyrpc/mytinyrpc/net/asyncclientsession.cc"
assert_grep "Package: \`release\`" "${OUT_DIR}/README.md"
assert_grep "cmake -S . -B build" "${OUT_DIR}/README.md"
assert_grep "TinyPbRpcChannel::Ptr" "${OUT_DIR}/test_client/test_tinyrpc_client.cc"
assert_grep "setReuseConnection(true)" "${OUT_DIR}/test_client/test_tinyrpc_client.cc"

if grep -q -- "-DMYTINYRPC_ROOT" "${OUT_DIR}/README.md"; then
    echo "[generator-release] FAIL: release README still asks for MYTINYRPC_ROOT"
    exit 1
fi

echo "[generator-release] configure without MYTINYRPC_ROOT"
cmake -S "${OUT_DIR}" -B "${OUT_DIR}/build"

echo "[generator-release] build"
cmake --build "${OUT_DIR}/build" --parallel 1

echo "[generator-release] run server"
bash "${OUT_DIR}/run.sh"

echo "[generator-release] run client"
"${OUT_DIR}/bin/QueryService_client" --client "${PORT}"

echo "[generator-release] shutdown server"
bash "${OUT_DIR}/shutdown.sh"

echo "[generator-release] PASS"
