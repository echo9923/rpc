#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/testlib.sh"

LABEL="network-suite"
SCRIPT_TIMEOUT="${MYTINYRPC_NETWORK_TIMEOUT:-180}"

cd "${MYTINYRPC_ROOT_DIR}"
mytinyrpc_build_if_needed "${LABEL}"
mytinyrpc_require_command "${LABEL}" nc

network_scripts=(
    scripts/check_stage1.sh
    scripts/check_stage8_rpc.sh
    scripts/check_stage11_server.sh
    scripts/check_stage12_http.sh
    scripts/check_rpc_sync.sh
    scripts/check_rpc_client_reactor.sh
    scripts/check_rpc_async.sh
    e2e/run.sh
)

for network_script in "${network_scripts[@]}"; do
    mytinyrpc_run_script "${LABEL}" "${SCRIPT_TIMEOUT}" "${network_script}"
done

echo "[${LABEL}] PASS"
