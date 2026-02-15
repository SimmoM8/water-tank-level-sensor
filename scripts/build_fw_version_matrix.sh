#!/usr/bin/env bash
set -euo pipefail

# Minimal local/CI build matrix for firmware version handling.
# Requires: arduino-cli. Platform/libraries are pinned in sketch.yaml profile.
#
# Optional overrides:
#   SKETCH_DIR=esp32/level_sensor
#   BUILD_PROFILE=release
#
# Usage:
#   ./scripts/build_fw_version_matrix.sh
#   ./scripts/build_fw_version_matrix.sh --bootstrap

SKETCH_DIR="${SKETCH_DIR:-esp32/level_sensor}"
BUILD_PROFILE="${BUILD_PROFILE:-release}"
SKETCH_FILE="${SKETCH_DIR}/sketch.yaml"
BOOTSTRAP=0
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOOTSTRAP_SCRIPT="${SCRIPT_DIR}/bootstrap_arduino_cli.sh"

print_usage() {
  cat <<'EOF'
Usage: ./scripts/build_fw_version_matrix.sh [--bootstrap]

Options:
  --bootstrap   Run bootstrap script first (installs/checks CLI + pinned deps)
  --help        Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --bootstrap)
      BOOTSTRAP=1
      shift
      ;;
    --help|-h)
      print_usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      print_usage >&2
      exit 2
      ;;
  esac
done

if [[ ${BOOTSTRAP} -eq 1 ]]; then
  if [[ ! -x "${BOOTSTRAP_SCRIPT}" ]]; then
    echo "Bootstrap script not found or not executable: ${BOOTSTRAP_SCRIPT}" >&2
    exit 1
  fi
  echo "==> Running bootstrap"
  "${BOOTSTRAP_SCRIPT}" --install-cli
fi

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "arduino-cli not found in PATH." >&2
  echo "Run: ./scripts/bootstrap_arduino_cli.sh --install-cli" >&2
  echo "Or run this script with: --bootstrap" >&2
  exit 1
fi

if [[ ! -f "${SKETCH_FILE}" ]]; then
  echo "Missing ${SKETCH_FILE}. This file is the dependency source of truth." >&2
  echo "Set SKETCH_DIR correctly or run from repository root." >&2
  exit 1
fi

echo "==> Refreshing Arduino indexes"
arduino-cli core update-index
arduino-cli lib update-index

echo "==> Build 1/2: default FW_VERSION fallback (no -DFW_VERSION)"
arduino-cli compile --profile "${BUILD_PROFILE}" "${SKETCH_DIR}"

echo "==> Build 2/2: explicit FW_VERSION via build flag"
arduino-cli compile \
  --profile "${BUILD_PROFILE}" \
  --build-property 'compiler.cpp.extra_flags=-DFW_VERSION=\"1.4.0\"' \
  "${SKETCH_DIR}"

echo "Build matrix passed"
