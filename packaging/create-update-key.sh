#!/usr/bin/env bash

# Story of Alicia Launcher
# SPDX-FileCopyrightText: 2026 Story Of Alicia Contributors
# SPDX-License-Identifier: GPL-3.0-only
# Additional terms apply, see ADDITIONAL_TERMS.md.

set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 OUTPUT_DIRECTORY" >&2
  echo "Example: $0 \"$HOME/private\"" >&2
  exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

OUTPUT_DIR="$1"
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"

OPENSSL_BIN="${SOA_OPENSSL:-$(command -v openssl || true)}"

PRIVATE_KEY="$OUTPUT_DIR/soa-update-key.pem"
PUBLIC_HEX="$OUTPUT_DIR/soa-update-public-key.hex"
KEY_ID_FILE="$OUTPUT_DIR/soa-update-key-id.txt"

source "$SCRIPT_DIR/soa-seal.sh"

for command_name in tail od tr awk wc mktemp grep sed; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    echo "Required command not found: $command_name" >&2
    exit 1
  fi
done

if [ -z "$OPENSSL_BIN" ] || [ ! -x "$OPENSSL_BIN" ]; then
  echo "OpenSSL was not found. Set SOA_OPENSSL to an OpenSSL 3 executable." >&2
  exit 1
fi

mkdir -p "$OUTPUT_DIR"
for path in "$PRIVATE_KEY" "$PUBLIC_HEX" "$KEY_ID_FILE"; do
  if [ -e "$path" ]; then
    echo "Refusing to overwrite existing file: $path" >&2
    exit 1
  fi
done

umask 077
"$OPENSSL_BIN" genpkey -algorithm Ed25519 -out "$PRIVATE_KEY"
chmod 600 "$PRIVATE_KEY"

PUBLIC_KEY_HEX="$(soa_seal_public_hex_from_private "$OPENSSL_BIN" "$PRIVATE_KEY")"
KEY_ID="$(soa_seal_key_id_from_private "$OPENSSL_BIN" "$PRIVATE_KEY")"

if [[ ! "$PUBLIC_KEY_HEX" =~ ^[0-9a-f]{64}$ ]]; then
  rm -f "$PRIVATE_KEY"
  echo "Generated public key is invalid." >&2
  exit 1
fi

printf '%s\n' "$PUBLIC_KEY_HEX" >"$PUBLIC_HEX"
printf '%s\n' "$KEY_ID" >"$KEY_ID_FILE"
chmod 644 "$PUBLIC_HEX" "$KEY_ID_FILE"

TEST_DOCUMENT="$(mktemp)"
TEST_SEAL="$(mktemp)"
printf '{"soa_seal_test":1}\n' >"$TEST_DOCUMENT"
soa_seal_sign_file "$OPENSSL_BIN" "$PRIVATE_KEY" manifest "$TEST_DOCUMENT" "$TEST_SEAL"
if ! soa_seal_verify_file "$OPENSSL_BIN" "$PRIVATE_KEY" manifest "$TEST_DOCUMENT" "$TEST_SEAL"; then
  rm -f "$PRIVATE_KEY" "$PUBLIC_HEX" "$KEY_ID_FILE" "$TEST_DOCUMENT" "$TEST_SEAL"
  echo "Generated key failed the SOA Seal self-test." >&2
  exit 1
fi
rm -f "$TEST_DOCUMENT" "$TEST_SEAL"

cat <<EOF_SUMMARY
Created a new launcher update key.

Private key (keep secret):
  $PRIVATE_KEY

Public key:
  $PUBLIC_HEX

SOA Seal key ID:
  $KEY_ID

Key ID file:
  $KEY_ID_FILE

Use the private key for release builds with:
  export SOA_UPDATE_SIGNING_KEY="$PRIVATE_KEY"

Never commit the private .pem file.
EOF_SUMMARY
