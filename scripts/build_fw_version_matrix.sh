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

SKETCH_DIR="${SKETCH_DIR:-esp32/level_sensor}"
BUILD_PROFILE="${BUILD_PROFILE:-release}"
SKETCH_FILE="${SKETCH_DIR}/sketch.yaml"

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "arduino-cli not found in PATH" >&2
  exit 1
fi

if [[ ! -f "${SKETCH_FILE}" ]]; then
  echo "Missing ${SKETCH_FILE}. This file is the dependency source of truth." >&2
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
