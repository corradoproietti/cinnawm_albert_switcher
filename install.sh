#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA_HOME="${XDG_DATA_HOME:-${HOME}/.local/share}"
ALBERT_PREFIX="${CINNAWM_ALBERT_PREFIX:-}"
DEST_DIR="${CINNAWM_ALBERT_PLUGINS_DIR:-}"
UNINSTALL=false
BUILD_DIR=""

cleanup() {
    if [ -n "${BUILD_DIR}" ]; then
        rm -rf "${BUILD_DIR}"
    fi
}
trap cleanup EXIT

usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Build, install, or uninstall the native C++ CinnaWM Switcher plugin for Albert (v35.1+).

Options:
  --prefix=DIR     Albert installation prefix containing lib*/cmake/Albert
  --dest-dir=DIR   Override plugin installation directory (containing cinnawm_switcher.so)
  --uninstall      Remove installed cinnawm_switcher.so (and any legacy Python copy)
  -h, --help       Show this help message
EOF
}

for arg in "$@"; do
    case "$arg" in
        --prefix=*) ALBERT_PREFIX="${arg#*=}" ;;
        --dest-dir=*) DEST_DIR="${arg#*=}" ;;
        --uninstall) UNINSTALL=true ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $arg" >&2; usage >&2; exit 1 ;;
    esac
done

detect_albert_prefix() {
    if [ -n "${ALBERT_PREFIX}" ]; then
        printf '%s\n' "${ALBERT_PREFIX}"
        return 0
    fi
    local candidate
    for candidate in "${HOME}/.local" "/usr/local" "/usr"; do
        if [ -d "${candidate}/lib/cmake/Albert" ] || compgen -G "${candidate}/lib/*/cmake/Albert" >/dev/null 2>&1; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done
    return 1
}

detect_dest_dir() {
    local prefix="$1"
    if [ -n "${DEST_DIR}" ]; then
        printf '%s\n' "${DEST_DIR}"
        return 0
    fi
    if [ -d "${prefix}/lib/albert" ]; then
        printf '%s\n' "${prefix}/lib/albert"
        return 0
    fi
    local multiarch
    for multiarch in "${prefix}"/lib/*/albert; do
        if [ -d "${multiarch}" ]; then
            printf '%s\n' "${multiarch}"
            return 0
        fi
    done
    printf '%s\n' "${prefix}/lib/albert"
}

RESOLVED_PREFIX="$(detect_albert_prefix || printf '%s\n' "${HOME}/.local")"
RESOLVED_DEST_DIR="$(detect_dest_dir "${RESOLVED_PREFIX}")"
LEGACY_PYTHON_DIR="${DATA_HOME}/albert/python/plugins/cinnawm_switcher"

if [ "${UNINSTALL}" = true ]; then
    echo "==> Removing ${RESOLVED_DEST_DIR}/cinnawm_switcher.so ..."
    rm -f "${RESOLVED_DEST_DIR}/cinnawm_switcher.so"
    rm -rf "${LEGACY_PYTHON_DIR}"
    echo "==> Uninstall complete."
    exit 0
fi

for req in cmake g++; do
    if ! command -v "${req}" >/dev/null 2>&1; then
        echo "Error: required build tool '${req}' not found in PATH." >&2
        exit 1
    fi
done

echo "==> Building CinnaWM Switcher (Albert SDK prefix: ${RESOLVED_PREFIX}) ..."
BUILD_DIR="$(mktemp -d)"
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_PREFIX_PATH="${RESOLVED_PREFIX}" \
    -DBUILD_PLUGIN=ON \
    -DBUILD_TESTING=ON
cmake --build "${BUILD_DIR}" -j"$(nproc 2>/dev/null || echo 2)"
ctest --test-dir "${BUILD_DIR}" --output-on-failure

mkdir -p "${RESOLVED_DEST_DIR}"
install -m 0755 "${BUILD_DIR}/cinnawm_switcher.so" "${RESOLVED_DEST_DIR}/cinnawm_switcher.so"
rm -rf "${LEGACY_PYTHON_DIR}" 2>/dev/null || true

echo "==> Installed: ${RESOLVED_DEST_DIR}/cinnawm_switcher.so"
echo "==> Restart Albert and enable 'CinnaWM Switcher' in Settings -> Plugins."
