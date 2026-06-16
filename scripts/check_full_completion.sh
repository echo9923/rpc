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

run_step "full regression suite" ./scripts/check_full_suite.sh

echo "[full-completion] PASS"
