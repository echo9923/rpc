#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/build/generated_task81_project"
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

echo "[generator-project] generate project"
python3 "${GENERATOR}" --proto "${PROTO_FILE}" --service "QueryService" --out "${OUT_DIR}"

echo "[generator-project] configure"
cmake -S "${OUT_DIR}" -B "${OUT_DIR}/build" -DMYTINYRPC_ROOT="${ROOT_DIR}"

echo "[generator-project] build"
cmake --build "${OUT_DIR}/build"

echo "[generator-project] run server"
bash "${OUT_DIR}/run.sh"

echo "[generator-project] run client"
"${OUT_DIR}/build/QueryService_client" --client "${PORT}"

echo "[generator-project] shutdown server"
bash "${OUT_DIR}/shutdown.sh"

echo "[generator] PASS"
