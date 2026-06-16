#!/usr/bin/env bash

MYTINYRPC_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MYTINYRPC_ROOT_DIR="$(cd "${MYTINYRPC_SCRIPT_DIR}/.." && pwd)"
MYTINYRPC_BUILD_DIR="${MYTINYRPC_ROOT_DIR}/build"

mytinyrpc_fail() {
    local label="$1"
    shift

    echo "[${label}] FAIL: $*"
    exit 1
}

mytinyrpc_build_if_needed() {
    local label="$1"

    cd "${MYTINYRPC_ROOT_DIR}"
    if [[ "${MYTINYRPC_SKIP_BUILD:-0}" == "1" ]]; then
        echo "[${label}] skip build"
        return
    fi

    echo "[${label}] build project"
    ./build.sh
}

mytinyrpc_require_command() {
    local label="$1"
    local command_name="$2"

    if ! command -v "${command_name}" >/dev/null 2>&1; then
        mytinyrpc_fail "${label}" "${command_name} is required but not found"
    fi
}

mytinyrpc_require_executable() {
    local label="$1"
    local name="$2"
    local bin="${MYTINYRPC_BUILD_DIR}/${name}"

    if [[ ! -x "${bin}" ]]; then
        mytinyrpc_fail "${label}" "missing executable ${bin}"
    fi
}

mytinyrpc_run_with_timeout() {
    local timeout_seconds="$1"
    shift

    if command -v timeout >/dev/null 2>&1; then
        timeout "${timeout_seconds}" "$@"
    else
        "$@"
    fi
}

mytinyrpc_run_binary() {
    local label="$1"
    local timeout_seconds="$2"
    local name="$3"
    shift 3

    mytinyrpc_require_executable "${label}" "${name}"

    echo "[${label}] run ${name}"
    mytinyrpc_run_with_timeout "${timeout_seconds}" "${MYTINYRPC_BUILD_DIR}/${name}" "$@"
}

mytinyrpc_run_script() {
    local label="$1"
    local timeout_seconds="$2"
    local script_path="$3"
    shift 3

    local full_path="${MYTINYRPC_ROOT_DIR}/${script_path}"
    if [[ ! -f "${full_path}" ]]; then
        mytinyrpc_fail "${label}" "missing script ${full_path}"
    fi

    echo "[${label}] run ${script_path}"
    mytinyrpc_run_with_timeout \
        "${timeout_seconds}" \
        env MYTINYRPC_SKIP_BUILD=1 bash "${full_path}" "$@"
}
