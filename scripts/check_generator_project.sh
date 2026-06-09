#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIMPLE_OUT_DIR="${ROOT_DIR}/build/generated_task115_simple_project"
FULL_OUT_DIR="${ROOT_DIR}/build/generated_task115_full_project"
GENERATOR="${ROOT_DIR}/generator/tinyrpc_generator.py"
PROTO_FILE="${ROOT_DIR}/testcases/test_tinypb_server.proto"
PORT="39999"

cleanup() {
    if [[ -f "${SIMPLE_OUT_DIR}/shutdown.sh" ]]; then
        bash "${SIMPLE_OUT_DIR}/shutdown.sh" >/dev/null 2>&1 || true
    fi
    if [[ -f "${FULL_OUT_DIR}/shutdown.sh" ]]; then
        bash "${FULL_OUT_DIR}/shutdown.sh" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

cd "${ROOT_DIR}"

rm -rf "${SIMPLE_OUT_DIR}" "${FULL_OUT_DIR}"

run_project() {
    local label="$1"
    local out_dir="$2"
    shift 2

    echo "[generator-project] generate ${label} project"
    python3 "${GENERATOR}" "$@" --proto "${PROTO_FILE}" --service "QueryService" --out "${out_dir}"

    echo "[generator-project] configure ${label}"
    cmake -S "${out_dir}" -B "${out_dir}/build" -DMYTINYRPC_ROOT="${ROOT_DIR}"

    echo "[generator-project] build ${label}"
    cmake --build "${out_dir}/build" --parallel 1

    echo "[generator-project] run ${label} server"
    bash "${out_dir}/run.sh"

    echo "[generator-project] run ${label} client"
    if [[ -x "${out_dir}/bin/QueryService_client" ]]; then
        "${out_dir}/bin/QueryService_client" --client "${PORT}"
    else
        "${out_dir}/build/QueryService_client" --client "${PORT}"
    fi

    echo "[generator-project] shutdown ${label} server"
    bash "${out_dir}/shutdown.sh"
}

run_project "simple" "${SIMPLE_OUT_DIR}"
run_project "full" "${FULL_OUT_DIR}" --layout full

echo "[generator] PASS"
