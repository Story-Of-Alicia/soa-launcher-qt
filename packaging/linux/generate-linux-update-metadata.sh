#!/usr/bin/env bash
# Story of Alicia Launcher
# SPDX-FileCopyrightText: 2026 Story Of Alicia Contributors
# SPDX-License-Identifier: GPL-3.0-only
# Additional terms apply, see ADDITIONAL_TERMS.md.

set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "Usage: $0 VERSION APPIMAGE" >&2
  exit 2
fi

VERSION="$1"
APPIMAGE="$2"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGING_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BASE_URL="${SOA_UPDATE_BASE_URL:-https://r2.storyofalicia.com/launcher/linux}"
GITHUB_REPOSITORY="${SOA_UPDATE_GITHUB_REPOSITORY:-Story-Of-Alicia/soa-launcher-qt}"
UPDATE_BRANCH="${SOA_UPDATE_BRANCH:-launcher-updates}"
FALLBACK_BASE_URL="${SOA_UPDATE_FALLBACK_BASE_URL:-https://raw.githubusercontent.com/$GITHUB_REPOSITORY/$UPDATE_BRANCH/linux}"
SIGNING_KEY="${SOA_UPDATE_SIGNING_KEY:-}"
HISTORY_INPUT="${SOA_UPDATE_HISTORY_INPUT:-}"
OPENSSL_BIN="${SOA_OPENSSL:-$(command -v openssl || true)}"
MANIFEST="$SCRIPT_DIR/manifest.json"
HISTORY="$SCRIPT_DIR/versions.json"
TEMP_ROOT=""

source "$PACKAGING_DIR/soa-seal.sh"

cleanup() {
  if [ -n "$TEMP_ROOT" ]; then
    rm -rf "$TEMP_ROOT"
  fi
}
trap cleanup EXIT

for command_name in jq curl sha256sum stat tail od tr awk wc date mktemp grep sed cmp; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    echo "Required command not found: $command_name" >&2
    exit 1
  fi
done

if [ -z "$OPENSSL_BIN" ] || [ ! -x "$OPENSSL_BIN" ]; then
  echo "OpenSSL was not found. Set SOA_OPENSSL to an OpenSSL 3 executable." >&2
  exit 1
fi
if [ ! -f "$APPIMAGE" ]; then
  echo "AppImage not found: $APPIMAGE" >&2
  exit 1
fi
if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+([+-][0-9A-Za-z.-]+)?$ ]]; then
  echo "Invalid launcher version: $VERSION" >&2
  exit 1
fi
if [ -z "$SIGNING_KEY" ] || [ ! -f "$SIGNING_KEY" ]; then
  echo "SOA_UPDATE_SIGNING_KEY must point to the private Ed25519 update key." >&2
  exit 1
fi
if [ ! -f "$PACKAGING_DIR/soa-update-public-key.hex" ]; then
  echo "Launcher update public key is missing: $PACKAGING_DIR/soa-update-public-key.hex" >&2
  exit 1
fi

DERIVED_PUBLIC_KEY_HEX="$(soa_seal_public_hex_from_private "$OPENSSL_BIN" "$SIGNING_KEY")"
EXPECTED_PUBLIC_KEY_HEX="$(tr -d '[:space:]' <"$PACKAGING_DIR/soa-update-public-key.hex" | tr '[:upper:]' '[:lower:]')"
if [ "$DERIVED_PUBLIC_KEY_HEX" != "$EXPECTED_PUBLIC_KEY_HEX" ]; then
  echo "The private update key does not match packaging/soa-update-public-key.hex." >&2
  exit 1
fi
SOA_SEAL_KEY_ID="$(soa_seal_key_id_from_private "$OPENSSL_BIN" "$SIGNING_KEY")"

FILE_NAME="$(basename "$APPIMAGE")"
EXPECTED_FILE_NAME="Story_Of_Alicia-${VERSION}-x86_64.AppImage"
if [ "$FILE_NAME" != "$EXPECTED_FILE_NAME" ]; then
  echo "Expected release filename: $EXPECTED_FILE_NAME" >&2
  exit 1
fi
SHA256="$(sha256sum "$APPIMAGE" | awk '{print $1}')"
SIZE="$(stat -c '%s' "$APPIMAGE")"
RELEASED_AT="${SOA_UPDATE_RELEASED_AT:-$(date -u +%Y-%m-%dT%H:%M:%SZ)}"

validate_history() {
  local input="$1"
  jq -e '
    (.schema == 1)
    and (.platform == "linux-x86_64")
    and (.releases | type == "array")
    and ((.releases | length) >= 1)
    and ((.releases | length) <= 3)
    and (all(.releases[]; .platform == "linux-x86_64"))
    and (([.releases[].version] | unique | length) == (.releases | length))
  ' "$input" >/dev/null
}

fetch_file() {
  local url="$1"
  local output="$2"
  local status
  status="$(curl --location --silent --show-error --connect-timeout 10 --max-time 30 \
    --output "$output" --write-out '%{http_code}' "$url")" || return 3
  case "$status" in
    200) return 0 ;;
    404) rm -f "$output"; return 1 ;;
    *) rm -f "$output"; echo "Remote update history request returned HTTP $status: $url" >&2; return 3 ;;
  esac
}

fetch_history_pair() {
  local label="$1"
  local base="$2"
  local prefix="$3"
  local json="$prefix.json"
  local seal="$prefix.json.seal"
  local json_status seal_status

  json_status=0
  fetch_file "$base/versions.json" "$json" || json_status=$?
  seal_status=0
  fetch_file "$base/versions.json.seal" "$seal" || seal_status=$?

  if [ "$json_status" -eq 1 ] && [ "$seal_status" -eq 1 ]; then
    return 1
  fi
  if [ "$json_status" -ne 0 ] || [ "$seal_status" -ne 0 ]; then
    if [ "$json_status" -eq 3 ] || [ "$seal_status" -eq 3 ]; then
      echo "$label update history is unavailable." >&2
      return 3
    fi
    echo "$label update history is incomplete." >&2
    return 2
  fi
  if ! soa_seal_verify_file "$OPENSSL_BIN" "$SIGNING_KEY" history "$json" "$seal"; then
    echo "$label update history has an invalid SOA Seal." >&2
    return 2
  fi
  if ! validate_history "$json"; then
    echo "$label update history is invalid." >&2
    return 2
  fi
  return 0
}

jq -n \
  --arg version "$VERSION" \
  --arg file_name "$FILE_NAME" \
  --arg url "$BASE_URL/$FILE_NAME" \
  --arg github_url "https://github.com/$GITHUB_REPOSITORY/releases/download/v$VERSION/$FILE_NAME" \
  --arg sha256 "$SHA256" \
  --argjson size "$SIZE" \
  --arg released_at "$RELEASED_AT" \
  '{schema: 1, platform: "linux-x86_64", version: $version,
    file_name: $file_name, url: $url, mirrors: [$github_url], sha256: $sha256, size: $size,
    released_at: $released_at, required: false}' >"$MANIFEST"

if [ -n "$HISTORY_INPUT" ]; then
  if [ ! -f "$HISTORY_INPUT" ]; then
    echo "SOA_UPDATE_HISTORY_INPUT does not exist: $HISTORY_INPUT" >&2
    exit 1
  fi
  HISTORY_SIGNATURE_INPUT="${SOA_UPDATE_HISTORY_SIGNATURE_INPUT:-$HISTORY_INPUT.seal}"
  if [ ! -f "$HISTORY_SIGNATURE_INPUT" ]; then
    echo "Existing history requires its SOA Seal file: $HISTORY_SIGNATURE_INPUT" >&2
    exit 1
  fi
  if ! soa_seal_verify_file "$OPENSSL_BIN" "$SIGNING_KEY" history "$HISTORY_INPUT" "$HISTORY_SIGNATURE_INPUT"; then
    echo "Existing launcher history does not have a valid SOA Seal v1 signature." >&2
    exit 1
  fi
  if ! validate_history "$HISTORY_INPUT"; then
    echo "Existing launcher history must contain one to three unique Linux releases." >&2
    exit 1
  fi
else
  TEMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/soa-linux-history.XXXXXX")"
  primary_status=0
  fetch_history_pair R2 "$BASE_URL" "$TEMP_ROOT/r2-versions" || primary_status=$?
  fallback_status=0
  fetch_history_pair GitHub "$FALLBACK_BASE_URL" "$TEMP_ROOT/github-versions" || fallback_status=$?

  if [ "$primary_status" -eq 2 ] || [ "$fallback_status" -eq 2 ]; then
    echo "Remote launcher history is inconsistent or invalid; refusing to replace it." >&2
    exit 1
  fi
  if [ "$primary_status" -eq 0 ] && [ "$fallback_status" -eq 0 ]; then
    if ! cmp -s "$TEMP_ROOT/r2-versions.json" "$TEMP_ROOT/github-versions.json"; then
      echo "R2 and GitHub launcher histories differ; reconcile them before generating a release." >&2
      exit 1
    fi
    HISTORY_INPUT="$TEMP_ROOT/r2-versions.json"
    HISTORY_SIGNATURE_INPUT="$TEMP_ROOT/r2-versions.json.seal"
  elif [ "$primary_status" -eq 0 ]; then
    HISTORY_INPUT="$TEMP_ROOT/r2-versions.json"
    HISTORY_SIGNATURE_INPUT="$TEMP_ROOT/r2-versions.json.seal"
  elif [ "$fallback_status" -eq 0 ]; then
    HISTORY_INPUT="$TEMP_ROOT/github-versions.json"
    HISTORY_SIGNATURE_INPUT="$TEMP_ROOT/github-versions.json.seal"
  elif [ "$primary_status" -eq 1 ] && [ "$fallback_status" -eq 1 ]; then
    HISTORY_INPUT=""
  else
    echo "Could not obtain a verified remote launcher history from R2 or GitHub." >&2
    exit 1
  fi
fi

if [ -n "$HISTORY_INPUT" ]; then
  jq --slurpfile current "$MANIFEST" \
    'def release_fields:
       {schema, platform, version, minimum_version, message, file_name, url, mirrors,
        sha256, size, released_at, required}
       | with_entries(select(.value != null));
     {schema: 1, platform: "linux-x86_64",
      releases: (([.releases[] | release_fields
                   | select(.version != $current[0].version)]
                  + [($current[0] | release_fields)])
                 | sort_by(.released_at)
                 | if length > 3 then .[-3:] else . end)}' "$HISTORY_INPUT" >"$HISTORY"
else
  jq -n --slurpfile current "$MANIFEST" \
    '{schema: 1, platform: "linux-x86_64", releases: $current}' >"$HISTORY"
fi

if ! validate_history "$HISTORY" \
    || ! jq -e --arg version "$VERSION" \
      '([.releases[].version] | map(select(. == $version)) | length) == 1' \
      "$HISTORY" >/dev/null; then
  echo "Generated launcher history is invalid or does not contain the current release exactly once." >&2
  exit 1
fi

soa_seal_sign_file "$OPENSSL_BIN" "$SIGNING_KEY" manifest "$MANIFEST" "$MANIFEST.seal"
soa_seal_sign_file "$OPENSSL_BIN" "$SIGNING_KEY" history "$HISTORY" "$HISTORY.seal"
soa_seal_verify_file "$OPENSSL_BIN" "$SIGNING_KEY" manifest "$MANIFEST" "$MANIFEST.seal"
soa_seal_verify_file "$OPENSSL_BIN" "$SIGNING_KEY" history "$HISTORY" "$HISTORY.seal"

echo "Generated SOA Seal v1 Linux update metadata with key $SOA_SEAL_KEY_ID:"
printf '  %s\n' "$MANIFEST" "$MANIFEST.seal" "$HISTORY" "$HISTORY.seal"
