#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/testlib.sh"

LABEL="full-suite"

cd "${MYTINYRPC_ROOT_DIR}"
mytinyrpc_build_if_needed "${LABEL}"

export MYTINYRPC_SKIP_BUILD=1

mytinyrpc_run_script "${LABEL}" 60 scripts/check_build_targets.sh
mytinyrpc_run_script "${LABEL}" 900 scripts/check_unit_suite.sh
mytinyrpc_run_script "${LABEL}" 180 scripts/check_benchmark_suite.sh
mytinyrpc_run_script "${LABEL}" 900 scripts/check_network_suite.sh
mytinyrpc_run_script "${LABEL}" 240 scripts/check_resource_lifetime.sh
mytinyrpc_run_script "${LABEL}" 600 scripts/check_generator.sh
mytinyrpc_run_script "${LABEL}" 900 scripts/check_generator_project.sh
mytinyrpc_run_script "${LABEL}" 900 scripts/check_generator_release_package.sh

echo "[${LABEL}] PASS"
