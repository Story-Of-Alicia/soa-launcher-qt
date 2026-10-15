#!/usr/bin/env bash

# Story of Alicia Launcher
# SPDX-FileCopyrightText: 2026 Story of Alicia contributors
# SPDX-License-Identifier: GPL-3.0-only
# Additional terms apply, see ADDITIONAL_TERMS.md.

set -euo pipefail

usage() {
  cat <<'EOF'
Usage: verify-release.sh DMG MANIFEST MANIFEST_SEAL HISTORY HISTORY_SEAL

Verify the Developer ID signatures, notarization tickets, Gatekeeper status,
universal architectures, deployment target, and bundled Mach-O dependencies of
a completed Story of Alicia macOS DMG and its SOA Seal metadata.

Optional environment variables:
  SOA_OPENSSL                OpenSSL 3 executable
  SOA_EXPECTED_TEAM_ID       Require this Apple Team ID in the app signature
  SOA_NETWORK_LIBRARY_NAME   Bundled Swift library name (default: libsoa_network.dylib)
  SOA_MACOS_ARCHS            Required architectures (default: x86_64;arm64)
  SOA_MINIMUM_MACOS_VERSION  Expected deployment target (default: 12.0)
EOF
}

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  usage
  exit 0
fi
if [ "$#" -ne 5 ]; then
  usage >&2
  exit 2
fi

DMG="$1"
MANIFEST="$2"
MANIFEST_SEAL="$3"
HISTORY="$4"
HISTORY_SEAL="$5"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGING_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OPENSSL_BIN="${SOA_OPENSSL:-$(command -v openssl || true)}"
REQUIRED_ARCHS="${SOA_MACOS_ARCHS:-${SOA_MACOS_ARCH:-x86_64;arm64}}"
EXPECTED_MINIMUM_VERSION="${SOA_MINIMUM_MACOS_VERSION:-12.0}"
NETWORK_LIBRARY_NAME="${SOA_NETWORK_LIBRARY_NAME:-libsoa_network.dylib}"
case "$NETWORK_LIBRARY_NAME" in
  ""|*/*)
    echo "Invalid Swift network library name: $NETWORK_LIBRARY_NAME" >&2
    exit 1
    ;;
esac
IFS=';' read -r -a ARCH_LIST <<<"$REQUIRED_ARCHS"

if [ ! -s "$DMG" ]; then
  echo "DMG does not exist or is empty: $DMG" >&2
  exit 1
fi
for tool in codesign file find hdiutil lipo otool plutil spctl vtool xcrun tail od tr awk wc mktemp grep sed; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Required tool not found: $tool" >&2
    exit 1
  fi
done
if [ -z "$OPENSSL_BIN" ] || [ ! -x "$OPENSSL_BIN" ]; then
  echo "OpenSSL was not found. Set SOA_OPENSSL to an OpenSSL 3 executable." >&2
  exit 1
fi
for path in "$MANIFEST" "$MANIFEST_SEAL" "$HISTORY" "$HISTORY_SEAL"; do
  if [ ! -s "$path" ]; then
    echo "Required update metadata is missing or empty: $path" >&2
    exit 1
  fi
done

source "$PACKAGING_DIR/soa-seal.sh"
PUBLIC_PEM="$(mktemp "${TMPDIR:-/tmp}/soa-update-public.XXXXXX")"
soa_seal_public_pem_from_hex_file "$OPENSSL_BIN" "$PACKAGING_DIR/soa-update-public-key.hex" "$PUBLIC_PEM"
soa_seal_verify_file_with_public_pem "$OPENSSL_BIN" "$PUBLIC_PEM" manifest "$MANIFEST" "$MANIFEST_SEAL"
soa_seal_verify_file_with_public_pem "$OPENSSL_BIN" "$PUBLIC_PEM" history "$HISTORY" "$HISTORY_SEAL"
rm -f "$PUBLIC_PEM"

MOUNT_POINT="$(mktemp -d "${TMPDIR:-/tmp}/soa-release-verify.XXXXXX")"
MOUNTED=0
cleanup() {
  if [ "$MOUNTED" -eq 1 ]; then
    hdiutil detach "$MOUNT_POINT" >/dev/null 2>&1 || true
  fi
  rmdir "$MOUNT_POINT" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "Verifying DMG signature and notarization ticket..."
codesign --verify --strict --verbose=4 "$DMG"
xcrun stapler validate "$DMG"
spctl --assess \
  --type open \
  --context context:primary-signature \
  --verbose=4 \
  "$DMG"

hdiutil attach "$DMG" -nobrowse -readonly -mountpoint "$MOUNT_POINT" >/dev/null
MOUNTED=1

apps=()
while IFS= read -r -d '' candidate; do
  apps+=("$candidate")
done < <(find "$MOUNT_POINT" -maxdepth 1 -type d -name '*.app' -print0)
if [ "${#apps[@]}" -ne 1 ]; then
  printf 'Expected one application in the DMG, found %s.\n' "${#apps[@]}" >&2
  exit 1
fi
APP="${apps[0]}"

echo "Verifying application signature and notarization ticket..."
codesign --verify --deep --strict --verbose=4 "$APP"
xcrun stapler validate "$APP"
spctl --assess --type execute --verbose=4 "$APP"

APP_SIGNATURE="$(codesign -dvv "$APP" 2>&1)"
TEAM_ID="$(printf '%s\n' "$APP_SIGNATURE" | sed -n 's/^TeamIdentifier=//p' | head -n 1)"
if [ -z "$TEAM_ID" ] || [ "$TEAM_ID" = "not set" ]; then
  echo "The application signature does not contain an Apple Team ID." >&2
  exit 1
fi
if [ -n "${SOA_EXPECTED_TEAM_ID:-}" ] && [ "$TEAM_ID" != "$SOA_EXPECTED_TEAM_ID" ]; then
  echo "Unexpected Apple Team ID: $TEAM_ID (expected $SOA_EXPECTED_TEAM_ID)" >&2
  exit 1
fi

MAIN_EXECUTABLE_NAME="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$APP/Contents/Info.plist")"
MAIN_EXECUTABLE="$APP/Contents/MacOS/$MAIN_EXECUTABLE_NAME"
COURIER="$APP/Contents/Frameworks/$NETWORK_LIBRARY_NAME"
AUDIO_HOST="$APP/Contents/Resources/alicia-log-hook/soa-audio-host"

if [ -s "$COURIER" ]; then
  COURIER_NAME="$(basename "$COURIER")"
  COURIER_INSTALL_NAME="$(otool -D "$COURIER" | tail -n 1)"
  if [ "$COURIER_INSTALL_NAME" != "@rpath/$COURIER_NAME" ]; then
    echo "Unexpected Swift network install name: $COURIER_INSTALL_NAME" >&2
    exit 1
  fi
  if ! otool -L "$MAIN_EXECUTABLE" | grep -F "@rpath/$COURIER_NAME" >/dev/null; then
    echo "The launcher does not reference the bundled Swift network library through @rpath." >&2
    exit 1
  fi
fi

for hook_artifact in SoaAliciaLogInjector.exe SoaAliciaLogHook.dll README.md MINHOOK_LICENSE.txt; do
  if [ ! -s "$APP/Contents/Resources/alicia-log-hook/$hook_artifact" ]; then
    echo "Required bundled Alicia component is missing: $hook_artifact" >&2
    exit 1
  fi
done

for binary in "$MAIN_EXECUTABLE" "$COURIER" "$AUDIO_HOST"; do
  if [ ! -s "$binary" ]; then
    echo "Required bundled executable is missing: $binary" >&2
    exit 1
  fi
  actual_archs=" $(lipo -archs "$binary") "
  for required_arch in "${ARCH_LIST[@]}"; do
    if [[ "$actual_archs" != *" $required_arch "* ]]; then
      echo "$binary is missing architecture $required_arch: $actual_archs" >&2
      exit 1
    fi
  done
  printf 'Architectures: %s: %s\n' "$binary" "${actual_archs# }"
done

MINIMUM_VERSION="$(vtool -show-build "$MAIN_EXECUTABLE" \
  | awk '$1 == "minos" { print $2; exit }')"
if [ "$MINIMUM_VERSION" != "$EXPECTED_MINIMUM_VERSION" ]; then
  echo "Unexpected macOS deployment target: ${MINIMUM_VERSION:-unknown} (expected $EXPECTED_MINIMUM_VERSION)" >&2
  exit 1
fi

echo "Verifying every nested Mach-O signature..."
while IFS= read -r -d '' item; do
  if file -L -b "$item" | grep -q 'Mach-O'; then
    codesign --verify --strict --verbose=2 "$item"
  fi
done < <(find "$APP/Contents" -type f -print0)

echo "Checking for non-portable Mach-O dependencies..."
BAD_DEPENDENCIES=0
while IFS= read -r -d '' item; do
  if ! file -L -b "$item" | grep -q 'Mach-O'; then
    continue
  fi
  while IFS= read -r dependency; do
    case "$dependency" in
      ""|@rpath/*|@loader_path/*|@executable_path/*|/System/Library/*|/usr/lib/*) ;;
      *)
        printf 'Non-portable dependency:\n  %s\n  -> %s\n' "$item" "$dependency" >&2
        BAD_DEPENDENCIES=1
        ;;
    esac
  done < <(otool -L "$item" | awk 'NR > 1 && /^[[:space:]]/ { print $1 }')

  while IFS= read -r rpath; do
    case "$rpath" in
      ""|@loader_path/*|@executable_path/*|@rpath/*|/System/Library/*|/usr/lib/*) ;;
      *)
        printf 'Non-portable runtime search path:\n  %s\n  -> %s\n' "$item" "$rpath" >&2
        BAD_DEPENDENCIES=1
        ;;
    esac
  done < <(otool -l "$item" | awk '
    $1 == "cmd" && $2 == "LC_RPATH" { awaiting_path = 1; next }
    awaiting_path && $1 == "path" { print $2; awaiting_path = 0 }
  ')
done < <(find "$APP/Contents" -type f -print0)
if [ "$BAD_DEPENDENCIES" -ne 0 ]; then
  exit 1
fi

printf '\nVerified macOS release:\n'
printf '  DMG: %s\n' "$DMG"
printf '  App: %s\n' "$(basename "$APP")"
printf '  Apple Team ID: %s\n' "$TEAM_ID"
printf '  Architectures: %s\n' "${REQUIRED_ARCHS//;/, }"
printf '  Minimum macOS: %s\n' "$MINIMUM_VERSION"
printf '  Gatekeeper: accepted\n'
printf '  Notarization tickets: valid\n'
