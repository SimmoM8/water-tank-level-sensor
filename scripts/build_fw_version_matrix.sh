#!/usr/bin/env bash
set -euo pipefail

# Minimal local/CI build matrix for firmware version handling.
# Requires: arduino-cli, esp32 core installed.
#
# Optional overrides:
#   FQBN=esp32:esp32:esp32
#   SKETCH_DIR=esp32/level_sensor
#
# Usage:
#   ./scripts/build_fw_version_matrix.sh

FQBN="${FQBN:-esp32:esp32:esp32}"
SKETCH_DIR="${SKETCH_DIR:-esp32/level_sensor}"

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "arduino-cli not found in PATH" >&2
  exit 1
fi

echo "==> Build 1/2: default FW_VERSION fallback (no -DFW_VERSION)"
arduino-cli compile --fqbn "${FQBN}" "${SKETCH_DIR}"

echo "==> Build 2/2: explicit FW_VERSION via build flag"
arduino-cli compile \
  --fqbn "${FQBN}" \
  --build-property 'compiler.cpp.extra_flags=-DFW_VERSION=\"1.4.0\"' \
  "${SKETCH_DIR}"

echo "Build matrix passed"

