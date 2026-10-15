#!/usr/bin/env bash

# Story of Alicia Launcher
# SPDX-FileCopyrightText: 2026 Story Of Alicia Contributors
# SPDX-License-Identifier: GPL-3.0-only
# Additional terms apply, see ADDITIONAL_TERMS.md.

set -euo pipefail

usage() {
  cat <<'EOF'
Usage: notarize.sh ARTIFACT

Submit an app archive or DMG to Apple's notarization service and wait for the
result. The artifact is not stapled by this script because an app is submitted
as a zip but stapled as an app bundle.

Required environment variables:
  SOA_NOTARY_KEY         Path to the App Store Connect AuthKey_*.p8 file
  SOA_NOTARY_KEY_ID      App Store Connect API key ID
  SOA_NOTARY_ISSUER_ID   App Store Connect issuer ID

APPLE_NOTARY_KEY_PATH, APPLE_NOTARY_KEY_ID, and APPLE_NOTARY_ISSUER_ID are
accepted as aliases.
EOF
}

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  usage
  exit 0
fi
if [ "$#" -ne 1 ]; then
  usage >&2
  exit 2
fi

ARTIFACT="$1"
NOTARY_KEY="${SOA_NOTARY_KEY:-${APPLE_NOTARY_KEY_PATH:-${NOTARY_KEY:-}}}"
NOTARY_KEY_ID="${SOA_NOTARY_KEY_ID:-${APPLE_NOTARY_KEY_ID:-${NOTARY_KEY_ID:-}}}"
NOTARY_ISSUER_ID="${SOA_NOTARY_ISSUER_ID:-${APPLE_NOTARY_ISSUER_ID:-${NOTARY_ISSUER_ID:-}}}"

if [ ! -f "$ARTIFACT" ]; then
  echo "Notarization artifact does not exist: $ARTIFACT" >&2
  exit 1
fi
if [ -z "$NOTARY_KEY" ] || [ ! -f "$NOTARY_KEY" ]; then
  echo "Set SOA_NOTARY_KEY to the AuthKey_*.p8 file." >&2
  exit 1
fi
if [ -z "$NOTARY_KEY_ID" ]; then
  echo "Set SOA_NOTARY_KEY_ID to the App Store Connect API key ID." >&2
  exit 1
fi
if [ -z "$NOTARY_ISSUER_ID" ]; then
  echo "Set SOA_NOTARY_ISSUER_ID to the App Store Connect issuer ID." >&2
  exit 1
fi
for tool in xcrun plutil mktemp; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Required tool not found: $tool" >&2
    exit 1
  fi
done

RESULT_FILE="$(mktemp "${TMPDIR:-/tmp}/soa-notary-result.XXXXXX")"
cleanup() {
  rm -f "$RESULT_FILE"
}
trap cleanup EXIT

AUTH_ARGS=(
  --key "$NOTARY_KEY"
  --key-id "$NOTARY_KEY_ID"
  --issuer "$NOTARY_ISSUER_ID"
)

printf 'Submitting to Apple notarization:\n  %s\n' "$ARTIFACT"
if ! xcrun notarytool submit "$ARTIFACT" \
    "${AUTH_ARGS[@]}" \
    --wait \
    --output-format plist >"$RESULT_FILE"; then
  submission_id="$(plutil -extract id raw -o - "$RESULT_FILE" 2>/dev/null || true)"
  if [ -n "$submission_id" ]; then
    xcrun notarytool log "$submission_id" "${AUTH_ARGS[@]}" || true
  else
    plutil -p "$RESULT_FILE" 2>/dev/null || true
  fi
  exit 1
fi

submission_status="$(plutil -extract status raw -o - "$RESULT_FILE" 2>/dev/null || true)"
submission_id="$(plutil -extract id raw -o - "$RESULT_FILE" 2>/dev/null || true)"
if [ "$submission_status" != "Accepted" ]; then
  if [ -n "$submission_id" ]; then
    xcrun notarytool log "$submission_id" "${AUTH_ARGS[@]}" || true
  else
    plutil -p "$RESULT_FILE" 2>/dev/null || true
  fi
  echo "Apple notarization finished with status: ${submission_status:-unknown}" >&2
  exit 1
fi

printf 'Apple notarization accepted submission %s.\n' "$submission_id"
