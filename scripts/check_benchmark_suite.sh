#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/testlib.sh"

LABEL="benchmark-suite"
COMMAND_TIMEOUT="${MYTINYRPC_BENCHMARK_TIMEOUT:-60}"

cd "${MYTINYRPC_ROOT_DIR}"
mytinyrpc_build_if_needed "${LABEL}"

benchmarks=(
    benchmark_http
    benchmark_tinypb_sync
    benchmark_tinypb_async
)

for benchmark in "${benchmarks[@]}"; do
    mytinyrpc_run_binary "${LABEL}" "${COMMAND_TIMEOUT}" "${benchmark}"
done

echo "[${LABEL}] PASS"
