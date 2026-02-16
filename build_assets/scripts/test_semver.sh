#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/build/tests"
BIN_PATH="${OUT_DIR}/semver_test"

mkdir -p "${OUT_DIR}"

if ! command -v c++ >/dev/null 2>&1; then
  echo "error: c++ compiler not found. Install clang++ or g++ first." >&2
  exit 1
fi

c++ -std=c++17 -Wall -Wextra -Werror \
  -I"${ROOT_DIR}/esp32/level_sensor" \
  "${ROOT_DIR}/tests/semver_test.cpp" \
  "${ROOT_DIR}/esp32/level_sensor/semver.cpp" \
  -o "${BIN_PATH}"

"${BIN_PATH}"
