#!/usr/bin/env bash
set -euo pipefail

# Bootstraps Arduino CLI and installs pinned platform/libraries from sketch.yaml.
# Usage:
#   ./scripts/bootstrap_arduino_cli.sh
#   ./scripts/bootstrap_arduino_cli.sh --install-cli
#
# Optional overrides:
#   SKETCH_DIR=esp32/level_sensor
#   BUILD_PROFILE=release

SKETCH_DIR="${SKETCH_DIR:-esp32/level_sensor}"
BUILD_PROFILE="${BUILD_PROFILE:-release}"
INSTALL_CLI=0

print_usage() {
  cat <<'EOF'
Usage: ./scripts/bootstrap_arduino_cli.sh [--install-cli]

Options:
  --install-cli   Attempt to install arduino-cli automatically (macOS/Linux) if missing
  --help          Show this help
EOF
}

print_cli_instructions() {
  cat <<'EOF' >&2
arduino-cli is not installed.
Install docs: https://arduino.github.io/arduino-cli/latest/installation/

Quick install (macOS/Linux):
  curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

Or rerun bootstrap with:
  ./scripts/bootstrap_arduino_cli.sh --install-cli
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --install-cli)
      INSTALL_CLI=1
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

ensure_arduino_cli() {
  if command -v arduino-cli >/dev/null 2>&1; then
    return 0
  fi

  if [[ ${INSTALL_CLI} -ne 1 ]]; then
    print_cli_instructions
    return 1
  fi

  if ! command -v curl >/dev/null 2>&1; then
    echo "curl is required to auto-install arduino-cli but was not found." >&2
    print_cli_instructions
    return 1
  fi

  local install_dir="${HOME}/.local/bin"
  mkdir -p "${install_dir}"
  echo "Installing arduino-cli into ${install_dir}..."
  if ! curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh -s -- -b "${install_dir}"; then
    echo "Failed to auto-install arduino-cli." >&2
    print_cli_instructions
    return 1
  fi

  export PATH="${install_dir}:${PATH}"

  if ! command -v arduino-cli >/dev/null 2>&1; then
    echo "arduino-cli still not found after installation attempt." >&2
    echo "Add '${install_dir}' to PATH and retry." >&2
    return 1
  fi
}

ensure_arduino_cli

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

if [[ -f "${SKETCH_DIR}/sketch.yaml" ]]; then
  SKETCH_FILE="${SKETCH_DIR}/sketch.yaml"
elif [[ -f "${REPO_ROOT}/${SKETCH_DIR}/sketch.yaml" ]]; then
  SKETCH_FILE="${REPO_ROOT}/${SKETCH_DIR}/sketch.yaml"
else
  echo "Missing sketch.yaml at '${SKETCH_DIR}/sketch.yaml'." >&2
  exit 1
fi

ESP32_URL="https://espressif.github.io/arduino-esp32/package_esp32_index.json"
echo "Configuring Arduino CLI boards manager URL..."
arduino-cli config init --overwrite
arduino-cli config add board_manager.additional_urls "${ESP32_URL}"
if ! arduino-cli config dump | grep -Fq "${ESP32_URL}"; then
  echo "Failed to configure Arduino CLI with Espressif boards manager URL." >&2
  exit 1
fi

echo "Refreshing Arduino indexes..."
arduino-cli core update-index
arduino-cli lib update-index

mapfile -t DEP_LINES < <(awk -v profile="${BUILD_PROFILE}" '
  $0 ~ "^  " profile ":" {in_profile=1; section=""; next}
  in_profile && $0 ~ "^  [^[:space:]][^:]*:" {in_profile=0; section=""; next}
  in_profile && $0 ~ "^    fqbn:" {
    v=$0
    sub("^    fqbn: ", "", v)
    print "FQBN|" v
  }
  in_profile && $0 ~ "^    platforms:" {section="platforms"; next}
  in_profile && $0 ~ "^    libraries:" {section="libraries"; next}
  in_profile && section=="platforms" && $0 ~ "^      - platform: " {
    v=$0
    sub("^      - platform: ", "", v)
    print "PLATFORM|" v
  }
  in_profile && section=="libraries" && $0 ~ "^      - " {
    v=$0
    sub("^      - ", "", v)
    print "LIB|" v
  }
' "${SKETCH_FILE}")

if [[ ${#DEP_LINES[@]} -eq 0 ]]; then
  echo "No dependencies found for profile '${BUILD_PROFILE}' in ${SKETCH_FILE}." >&2
  exit 1
fi

FQBN=""
PLATFORM_COUNT=0
LIB_COUNT=0

for line in "${DEP_LINES[@]}"; do
  kind="${line%%|*}"
  spec="${line#*|}"

  if [[ "${kind}" == "FQBN" ]]; then
    FQBN="${spec}"
    continue
  fi

  pin_re='^(.+) \(([^)]+)\)$'
  if [[ ! "${spec}" =~ ${pin_re} ]]; then
    echo "Dependency '${spec}' is not pinned as 'Name (version)' in ${SKETCH_FILE}." >&2
    exit 1
  fi

  name="${BASH_REMATCH[1]}"
  version="${BASH_REMATCH[2]}"

  if [[ "${kind}" == "PLATFORM" ]]; then
    PLATFORM_COUNT=$((PLATFORM_COUNT + 1))
    echo "Installing platform ${name}@${version}"
    arduino-cli core install "${name}@${version}"
  elif [[ "${kind}" == "LIB" ]]; then
    LIB_COUNT=$((LIB_COUNT + 1))
    echo "Installing library ${name}@${version} (primary)"
    if ! arduino-cli lib install "${name}@${version}"; then
      echo "Primary install failed; retrying with name-only for ${name}"
      arduino-cli lib install "${name}"
    fi

    lib_list="$(arduino-cli lib list)"
    actual_version="$(printf '%s\n' "${lib_list}" \
      | awk -v n="${name}" '
          $0 ~ ("^" n "[[:space:]]+") {
            if (match($0, /[[:space:]]+v?[0-9]+\.[0-9]+(\.[0-9]+)?([-.+][0-9A-Za-z.]+)?/, m)) {
              v=m[0]
              sub(/^[[:space:]]+/, "", v)
              sub(/^v/, "", v)
              print v
              exit
            }
          }
        ')"

    expected_version="${version#v}"
    if [[ -z "${actual_version}" ]]; then
      echo "Library '${name}' appears uninstalled; expected version ${expected_version}." >&2
      exit 1
    fi
    if [[ "${actual_version}" != "${expected_version}" ]]; then
      echo "Pinned library version mismatch for '${name}': expected ${expected_version}, got ${actual_version}." >&2
      exit 1
    fi
  fi
done

if [[ -z "${FQBN}" ]]; then
  echo "Missing fqbn for profile '${BUILD_PROFILE}' in ${SKETCH_FILE}." >&2
  exit 1
fi

echo "Bootstrap complete."
echo "Profile: ${BUILD_PROFILE}"
echo "FQBN: ${FQBN}"
echo "Installed platform deps: ${PLATFORM_COUNT}"
echo "Installed library deps: ${LIB_COUNT}"
