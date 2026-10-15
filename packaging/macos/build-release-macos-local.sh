#!/usr/bin/env bash

# Story of Alicia Launcher
# SPDX-FileCopyrightText: 2026 Story of Alicia contributors
# SPDX-License-Identifier: GPL-3.0-only
# Additional terms apply, see ADDITIONAL_TERMS.md.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../build-common.sh"
PROJECT_ROOT="$(soa_resolve_project_root "${SOA_SOURCE_DIR:-}" "$SCRIPT_DIR")"
SCRIPT_DIR="$PROJECT_ROOT/packaging/macos"

usage() {
  cat <<'EOF'
Usage: build-release-macos-local.sh

Build, test, deploy, Developer-ID sign, notarize, staple, package, and verify
one universal Story of Alicia macOS DMG.

Required environment variables:
  SOA_NOTARY_KEY          Path to the App Store Connect AuthKey_*.p8 file
  SOA_NOTARY_KEY_ID       App Store Connect API key ID
  SOA_NOTARY_ISSUER_ID    App Store Connect issuer ID
  SOA_UPDATE_SIGNING_KEY  Path to soa-update-key.pem. Required because update
                          metadata is generated automatically.

Optional environment variables:
  SOA_DEVELOPER_IDENTITY  Developer ID fingerprint or full identity. When
                          omitted, the only installed Developer ID Application
                          identity is selected automatically.
  SOA_QT_PREFIX           Complete universal Qt macOS installation
  SOA_QT_VERSION          Version downloaded by aqt when Qt is missing (default: 6.8.3)
  SOA_QT_INSTALL_ROOT     Qt installation root (default: ~/Qt)
  SOA_RELEASE_REQUIRE_TESTS  Require passing tests (default: 1, set 0 to continue)
  SOA_BUILD_DIR           Build directory (default: build-macos-release-local)
  SOA_BUILD_TYPE          CMake configuration (default: Release)
  SOA_MACOS_ARCHS         Architectures (default: x86_64;arm64)
  SOA_OPENSSL             OpenSSL 3 executable used by metadata generation

APPLE_DEVELOPER_IDENTITY, APPLE_NOTARY_KEY_PATH, APPLE_NOTARY_KEY_ID,
APPLE_NOTARY_ISSUER_ID, SIGNING_IDENTITY, NOTARY_KEY, NOTARY_KEY_ID, and
NOTARY_ISSUER_ID are accepted as aliases.
EOF
}

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  usage
  exit 0
fi
if [ "$#" -ne 0 ]; then
  usage >&2
  exit 2
fi
if [ "$(uname -s)" != "Darwin" ]; then
  echo "A signed and notarized macOS release must be built on macOS." >&2
  exit 1
fi

BUILD_DIR="${SOA_BUILD_DIR:-$PROJECT_ROOT/build-macos-release-local}"
BUILD_TYPE="${SOA_BUILD_TYPE:-Release}"
DEVELOPER_IDENTITY="${SOA_DEVELOPER_IDENTITY:-${APPLE_DEVELOPER_IDENTITY:-${SIGNING_IDENTITY:-}}}"
export SOA_NOTARY_KEY="${SOA_NOTARY_KEY:-${APPLE_NOTARY_KEY_PATH:-${NOTARY_KEY:-}}}"
export SOA_NOTARY_KEY_ID="${SOA_NOTARY_KEY_ID:-${APPLE_NOTARY_KEY_ID:-${NOTARY_KEY_ID:-}}}"
export SOA_NOTARY_ISSUER_ID="${SOA_NOTARY_ISSUER_ID:-${APPLE_NOTARY_ISSUER_ID:-${NOTARY_ISSUER_ID:-}}}"

for tool in cmake codesign cpack ditto hdiutil lipo security spctl xcrun; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Required tool not found: $tool" >&2
    exit 1
  fi
done

if [ -z "$DEVELOPER_IDENTITY" ]; then
  identities=()
  while IFS= read -r identity; do
    identities+=("$identity")
  done < <(security find-identity -v -p codesigning \
    | sed -n 's/^[[:space:]]*[0-9][0-9]*) [0-9A-Fa-f]* "\(Developer ID Application:.*\)"$/\1/p')
  if [ "${#identities[@]}" -ne 1 ]; then
    echo "Could not select exactly one installed Developer ID Application identity." >&2
    security find-identity -v -p codesigning >&2 || true
    echo "Set SOA_DEVELOPER_IDENTITY to the certificate fingerprint or full identity." >&2
    exit 1
  fi
  DEVELOPER_IDENTITY="${identities[0]}"
fi

if ! security find-identity -v -p codesigning \
    | grep -F "$DEVELOPER_IDENTITY" >/dev/null; then
  echo "Developer ID signing identity is not available in the current keychain: $DEVELOPER_IDENTITY" >&2
  exit 1
fi
if [ -z "$SOA_NOTARY_KEY" ] || [ ! -f "$SOA_NOTARY_KEY" ]; then
  echo "Set SOA_NOTARY_KEY to the App Store Connect AuthKey_*.p8 file." >&2
  exit 1
fi
if [ -z "$SOA_NOTARY_KEY_ID" ] || [ -z "$SOA_NOTARY_ISSUER_ID" ]; then
  echo "Set SOA_NOTARY_KEY_ID and SOA_NOTARY_ISSUER_ID." >&2
  exit 1
fi
if [ -z "${SOA_UPDATE_SIGNING_KEY:-}" ] || [ ! -f "$SOA_UPDATE_SIGNING_KEY" ]; then
  echo "SOA_UPDATE_SIGNING_KEY must point to soa-update-key.pem." >&2
  echo "Create one with: ./packaging/create-update-key.sh /path/to/private-directory" >&2
  exit 1
fi

BUILD_DIR="$(soa_absolute_directory "$BUILD_DIR")"
soa_validate_output_directory "$BUILD_DIR" "$PROJECT_ROOT"
soa_validate_build_cache "$BUILD_DIR" "$PROJECT_ROOT" Xcode

printf 'Building universal macOS release with identity:\n  %s\n' "$DEVELOPER_IDENTITY"
SOA_SOURCE_DIR="$PROJECT_ROOT" \
SOA_BUILD_DIR="$BUILD_DIR" \
SOA_BUILD_TYPE="$BUILD_TYPE" \
SOA_REQUIRE_TESTS="${SOA_RELEASE_REQUIRE_TESTS:-1}" \
  "$SCRIPT_DIR/build-local.sh"

APP="$(soa_build_value "$BUILD_DIR" "$BUILD_TYPE" app_bundle)"
if [ ! -d "$APP" ]; then
  printf 'The configured app bundle was not built: %s\n' "$APP" >&2
  exit 1
fi
VERSION="$(soa_build_value "$BUILD_DIR" "$BUILD_TYPE" version)"
if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+([+-][0-9A-Za-z.-]+)?$ ]]; then
  echo "Could not determine a valid launcher version." >&2
  exit 1
fi
PACKAGE_NAME="$(soa_build_value "$BUILD_DIR" "$BUILD_TYPE" package_name)"
NETWORK_NAME="$(soa_build_value "$BUILD_DIR" "$BUILD_TYPE" network_name)"
GENERATED_DMG="$BUILD_DIR/$PACKAGE_NAME.dmg"
DMG="$BUILD_DIR/Story_Of_Alicia-${VERSION}-macos.dmg"
TEMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/soa-release-local.XXXXXX")"
cleanup() {
  rm -rf "$TEMP_ROOT"
}
trap cleanup EXIT

"$SCRIPT_DIR/sign-app.sh" \
  "$APP" \
  "$DEVELOPER_IDENTITY" \
  "$SCRIPT_DIR/entitlements.plist"

SIGNED_TEAM_ID="$(codesign -dvv "$APP" 2>&1 | sed -n 's/^TeamIdentifier=//p' | head -n 1)"
if [ -z "$SIGNED_TEAM_ID" ] || [ "$SIGNED_TEAM_ID" = "not set" ]; then
  echo "The signed application does not contain an Apple Team ID." >&2
  exit 1
fi
if [ -n "${SOA_EXPECTED_TEAM_ID:-}" ] && [ "$SIGNED_TEAM_ID" != "$SOA_EXPECTED_TEAM_ID" ]; then
  echo "Unexpected Apple Team ID: $SIGNED_TEAM_ID (expected $SOA_EXPECTED_TEAM_ID)" >&2
  exit 1
fi

APP_NOTARY_ZIP="$TEMP_ROOT/soa-launcher-macos.zip"
ditto -c -k --keepParent "$APP" "$APP_NOTARY_ZIP"
"$SCRIPT_DIR/notarize.sh" "$APP_NOTARY_ZIP"
xcrun stapler staple "$APP"
xcrun stapler validate "$APP"
spctl --assess --type execute --verbose=4 "$APP"

rm -f "$GENERATED_DMG" "$DMG"
(
  cd "$BUILD_DIR"
  cpack --config "$BUILD_DIR/CPackConfig.cmake" -G DragNDrop -C "$BUILD_TYPE" --verbose
)
if [ ! -s "$GENERATED_DMG" ]; then
  echo "CPack did not produce the expected DMG: $GENERATED_DMG" >&2
  exit 1
fi
mv "$GENERATED_DMG" "$DMG"

codesign --force --timestamp --sign "$DEVELOPER_IDENTITY" "$DMG"
codesign --verify --strict --verbose=4 "$DMG"
"$SCRIPT_DIR/notarize.sh" "$DMG"
xcrun stapler staple "$DMG"
xcrun stapler validate "$DMG"
spctl --assess \
  --type open \
  --context context:primary-signature \
  --verbose=4 \
  "$DMG"

"$SCRIPT_DIR/generate-macos-update-metadata.sh" "$VERSION" "$DMG"
SOA_EXPECTED_TEAM_ID="$SIGNED_TEAM_ID" \
SOA_NETWORK_LIBRARY_NAME="$NETWORK_NAME" \
  bash "$SCRIPT_DIR/verify-release.sh" \
  "$DMG" \
  "$SCRIPT_DIR/manifest.json" \
  "$SCRIPT_DIR/manifest.json.seal" \
  "$SCRIPT_DIR/versions.json" \
  "$SCRIPT_DIR/versions.json.seal"


printf '\nCompleted signed and notarized macOS release:\n  %s\n' "$DMG"
