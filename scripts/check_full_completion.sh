#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cd "${ROOT_DIR}"

trap 'echo "[full-completion] FAIL: command failed at line ${LINENO}"' ERR

run_step() {
    local name="$1"
    shift

    echo "[full-completion] run ${name}"
    "$@"
}

run_step "all regression" ./scripts/check_all.sh
run_step "coroutine hook regression" env MYTINYRPC_SKIP_BUILD=1 ./scripts/check_coroutinehook.sh
run_step "client Reactor regression" env MYTINYRPC_SKIP_BUILD=1 ./scripts/check_rpc_client_reactor.sh
run_step "async rpc regression" env MYTINYRPC_SKIP_BUILD=1 ./scripts/check_rpc_async.sh
run_step "http regression" ./scripts/check_stage12_http.sh
run_step "generated project regression" ./scripts/check_generator_project.sh
run_step "generated release package regression" ./scripts/check_generator_release_package.sh
run_step "resource lifetime regression" env MYTINYRPC_SKIP_BUILD=1 ./scripts/check_resource_lifetime.sh

echo "[full-completion] PASS"
