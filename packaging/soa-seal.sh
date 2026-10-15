#!/usr/bin/env bash

# Story of Alicia Launcher
# SPDX-FileCopyrightText: 2026 Story of Alicia contributors
# SPDX-License-Identifier: GPL-3.0-only
# Additional terms apply, see ADDITIONAL_TERMS.md.

soa_seal_public_hex_from_private() {
  local openssl_bin="$1"
  local private_key="$2"

  "$openssl_bin" pkey -in "$private_key" -pubout -outform DER \
    | tail -c 32 \
    | od -An -v -tx1 \
    | tr -d ' \n' \
    | tr '[:upper:]' '[:lower:]'
}

soa_seal_key_id_from_private() {
  local openssl_bin="$1"
  local private_key="$2"
  local input hash

  input="$(mktemp)"
  printf 'SOA-SEAL-KEY-ID-V1\0' >"$input"
  "$openssl_bin" pkey -in "$private_key" -pubout -outform DER \
    | tail -c 32 >>"$input"

  hash="$(
    "$openssl_bin" dgst -sha256 -binary "$input" \
      | od -An -v -tx1 \
      | tr -d ' \n' \
      | tr '[:lower:]' '[:upper:]'
  )"
  rm -f "$input"

  if [ "${#hash}" -ne 64 ]; then
    echo "Could not derive the SOA Seal key ID." >&2
    return 1
  fi

  hash="${hash:0:32}"
  printf 'SOA1-%s-%s-%s-%s-%s-%s-%s-%s\n' \
    "${hash:0:4}" "${hash:4:4}" "${hash:8:4}" "${hash:12:4}" \
    "${hash:16:4}" "${hash:20:4}" "${hash:24:4}" "${hash:28:4}"
}

soa_seal_key_id_from_public_pem() {
  local openssl_bin="$1"
  local public_key="$2"
  local input hash

  input="$(mktemp)"
  printf 'SOA-SEAL-KEY-ID-V1\0' >"$input"
  "$openssl_bin" pkey -pubin -in "$public_key" -outform DER \
    | tail -c 32 >>"$input"

  hash="$(
    "$openssl_bin" dgst -sha256 -binary "$input" \
      | od -An -v -tx1 \
      | tr -d ' \n' \
      | tr '[:lower:]' '[:upper:]'
  )"
  rm -f "$input"

  if [ "${#hash}" -ne 64 ]; then
    echo "Could not derive the SOA Seal key ID." >&2
    return 1
  fi

  hash="${hash:0:32}"
  printf 'SOA1-%s-%s-%s-%s-%s-%s-%s-%s\n' \
    "${hash:0:4}" "${hash:4:4}" "${hash:8:4}" "${hash:12:4}" \
    "${hash:16:4}" "${hash:20:4}" "${hash:24:4}" "${hash:28:4}"
}

soa_seal_build_payload() {
  local kind="$1"
  local key_id="$2"
  local input="$3"
  local output="$4"

  case "$kind" in
    manifest|history) ;;
    *)
      echo "Invalid SOA Seal document kind: $kind" >&2
      return 1
      ;;
  esac

  printf 'SOA-SEAL-V1\0kind=%s\0key=%s\0' "$kind" "$key_id" >"$output"
  cat "$input" >>"$output"
}

soa_seal_sign_file() {
  local openssl_bin="$1"
  local private_key="$2"
  local kind="$3"
  local input="$4"
  local output="$5"
  local key_id payload raw_signature signature_b64

  key_id="$(soa_seal_key_id_from_private "$openssl_bin" "$private_key")"
  payload="$(mktemp)"
  raw_signature="$(mktemp)"

  soa_seal_build_payload "$kind" "$key_id" "$input" "$payload"
  "$openssl_bin" pkeyutl -sign -rawin -inkey "$private_key" \
    -in "$payload" -out "$raw_signature"

  if [ "$(wc -c <"$raw_signature" | tr -d ' ')" -ne 64 ]; then
    rm -f "$payload" "$raw_signature"
    echo "Ed25519 returned an unexpected SOA Seal signature size." >&2
    return 1
  fi

  signature_b64="$("$openssl_bin" base64 -A -in "$raw_signature")"
  rm -f "$payload" "$raw_signature"

  cat >"$output" <<EOF_SEAL
SOA-SEAL-V1
kind=$kind
key=$key_id
sig=$signature_b64
EOF_SEAL
}

soa_seal_verify_file_with_public_pem() {
  local openssl_bin="$1"
  local public_key="$2"
  local expected_kind="$3"
  local input="$4"
  local seal_file="$5"
  local normalized line_count magic kind_line key_line sig_line
  local kind key_id signature_b64 expected_key_id payload raw_signature

  normalized="$(mktemp)"
  payload="$(mktemp)"
  raw_signature="$(mktemp)"

  : >"$normalized"
  while IFS= read -r seal_line || [ -n "$seal_line" ]; do
    seal_line="${seal_line%$'\r'}"
    if [[ "$seal_line" == *$'\r'* ]]; then
      rm -f "$normalized" "$payload" "$raw_signature"
      echo "SOA Seal contains an invalid carriage return." >&2
      return 1
    fi
    printf '%s\n' "$seal_line" >>"$normalized"
  done <"$seal_file"

  line_count="$(awk 'END { print NR }' "$normalized")"
  if [ "$line_count" -ne 4 ]; then
    rm -f "$normalized" "$payload" "$raw_signature"
    echo "SOA Seal must contain exactly four lines: $seal_file" >&2
    return 1
  fi

  magic="$(sed -n '1p' "$normalized")"
  kind_line="$(sed -n '2p' "$normalized")"
  key_line="$(sed -n '3p' "$normalized")"
  sig_line="$(sed -n '4p' "$normalized")"

  if [ "$magic" != "SOA-SEAL-V1" ]; then
    rm -f "$normalized" "$payload" "$raw_signature"
    echo "Unsupported update signature format. SOA Seal v1 is required." >&2
    return 1
  fi

  case "$kind_line" in kind=*) kind="${kind_line#kind=}" ;; *) kind="" ;; esac
  case "$key_line" in key=*) key_id="${key_line#key=}" ;; *) key_id="" ;; esac
  case "$sig_line" in sig=*) signature_b64="${sig_line#sig=}" ;; *) signature_b64="" ;; esac

  if [ "$kind" != "$expected_kind" ]; then
    rm -f "$normalized" "$payload" "$raw_signature"
    echo "SOA Seal document kind mismatch: expected $expected_kind, got ${kind:-invalid}." >&2
    return 1
  fi

  if ! printf '%s\n' "$key_id" | grep -Eq '^SOA1-([0-9A-F]{4}-){7}[0-9A-F]{4}$'; then
    rm -f "$normalized" "$payload" "$raw_signature"
    echo "SOA Seal key ID is malformed." >&2
    return 1
  fi

  expected_key_id="$(soa_seal_key_id_from_public_pem "$openssl_bin" "$public_key")"
  if [ "$key_id" != "$expected_key_id" ]; then
    rm -f "$normalized" "$payload" "$raw_signature"
    echo "SOA Seal was created by a different update key: $key_id" >&2
    return 1
  fi

  if ! printf '%s\n' "$signature_b64" | grep -Eq '^[A-Za-z0-9+/]{86}==$'; then
    rm -f "$normalized" "$payload" "$raw_signature"
    echo "SOA Seal signature is not canonical base64." >&2
    return 1
  fi

  if ! printf '%s' "$signature_b64" | "$openssl_bin" base64 -d -A >"$raw_signature" 2>/dev/null; then
    rm -f "$normalized" "$payload" "$raw_signature"
    echo "SOA Seal signature is not valid base64." >&2
    return 1
  fi

  if [ "$(wc -c <"$raw_signature" | tr -d ' ')" -ne 64 ]; then
    rm -f "$normalized" "$payload" "$raw_signature"
    echo "SOA Seal signature is not a 64-byte Ed25519 signature." >&2
    return 1
  fi

  soa_seal_build_payload "$kind" "$key_id" "$input" "$payload"

  if ! "$openssl_bin" pkeyutl -verify -pubin -inkey "$public_key" -rawin \
      -in "$payload" -sigfile "$raw_signature" >/dev/null 2>&1; then
    rm -f "$normalized" "$payload" "$raw_signature"
    echo "SOA Seal verification failed." >&2
    return 1
  fi

  rm -f "$normalized" "$payload" "$raw_signature"
}

soa_seal_verify_file() {
  local openssl_bin="$1"
  local private_key="$2"
  local expected_kind="$3"
  local input="$4"
  local seal_file="$5"
  local public_key status

  public_key="$(mktemp)"
  "$openssl_bin" pkey -in "$private_key" -pubout -out "$public_key"
  status=0
  soa_seal_verify_file_with_public_pem "$openssl_bin" "$public_key" \
    "$expected_kind" "$input" "$seal_file" || status=$?
  rm -f "$public_key"
  return "$status"
}

soa_seal_public_pem_from_hex_file() {
  local openssl_bin="$1"
  local hex_file="$2"
  local output="$3"
  local public_hex der

  public_hex="$(tr -d '[:space:]' <"$hex_file" | tr '[:upper:]' '[:lower:]')"
  if [[ ! "$public_hex" =~ ^[0-9a-f]{64}$ ]]; then
    echo "SOA update public key must contain exactly 64 hexadecimal characters." >&2
    return 1
  fi

  der="$(mktemp)"
  printf '%b' '\x30\x2a\x30\x05\x06\x03\x2b\x65\x70\x03\x21\x00' >"$der"
  for ((offset = 0; offset < 64; offset += 2)); do
    printf '%b' "\\x${public_hex:offset:2}" >>"$der"
  done
  if ! "$openssl_bin" pkey -pubin -inform DER -in "$der" -out "$output" >/dev/null 2>&1; then
    rm -f "$der"
    echo "Could not convert the SOA update public key to PEM." >&2
    return 1
  fi
  rm -f "$der"
}
