#!/usr/bin/env bash
set -euo pipefail

MANIFEST_DIR="${1:-manifests}"

if [[ ! -d "${MANIFEST_DIR}" ]]; then
  echo "::error::Manifest directory not found: ${MANIFEST_DIR}" >&2
  exit 1
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "::error::jq is required to validate manifests." >&2
  exit 1
fi

mapfile -t manifest_files < <(find "${MANIFEST_DIR}" -type f -name '*.json' | sort)

if [[ ${#manifest_files[@]} -eq 0 ]]; then
  echo "::error::No manifest JSON files found in ${MANIFEST_DIR}" >&2
  exit 1
fi

fail=0
for manifest in "${manifest_files[@]}"; do
  if ! jq -e . "${manifest}" >/dev/null 2>&1; then
    echo "::error file=${manifest}::Invalid JSON" >&2
    fail=1
    continue
  fi

  version="$(jq -r '.version // ""' "${manifest}")"
  url="$(jq -r '.url // ""' "${manifest}")"
  sha256="$(jq -r '.sha256 // ""' "${manifest}")"

  if [[ -z "${version}" ]]; then
    echo "::error file=${manifest}::Missing or empty required field: version" >&2
    fail=1
  fi
  if [[ -z "${url}" ]]; then
    echo "::error file=${manifest}::Missing or empty required field: url" >&2
    fail=1
  elif [[ ! "${url}" =~ ^https:// ]]; then
    echo "::error file=${manifest}::Field 'url' must start with https://" >&2
    fail=1
  fi
  if [[ -z "${sha256}" ]]; then
    echo "::error file=${manifest}::Missing or empty required field: sha256" >&2
    fail=1
  elif [[ ! "${sha256}" =~ ^[0-9a-fA-F]{64}$ ]]; then
    echo "::error file=${manifest}::Field 'sha256' must be a 64-character hex string" >&2
    fail=1
  fi
done

if [[ ${fail} -ne 0 ]]; then
  exit 1
fi

echo "Manifest validation passed for ${#manifest_files[@]} file(s) in ${MANIFEST_DIR}."
