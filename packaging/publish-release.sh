#!/usr/bin/env bash
# Story of Alicia Launcher
# SPDX-FileCopyrightText: 2026 Story Of Alicia Contributors
# SPDX-License-Identifier: GPL-3.0-only
# Additional terms apply, see ADDITIONAL_TERMS.md.

set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "Usage: $0 APPIMAGE DMG" >&2
  exit 2
fi

APPIMAGE="$1"
DMG="$2"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
GITHUB_REPOSITORY="${SOA_GITHUB_REPOSITORY:-Story-Of-Alicia/soa-launcher-qt}"
UPDATE_BRANCH="${SOA_UPDATE_BRANCH:-launcher-updates}"
ACCOUNT_ID="${SOA_R2_ACCOUNT_ID:-}"
BUCKET="${SOA_R2_BUCKET:-}"
PREFIX="${SOA_R2_PREFIX:-launcher}"
OPENSSL_BIN="${SOA_OPENSSL:-$(command -v openssl || true)}"
LINUX_DIR="$SCRIPT_DIR/linux"
MACOS_DIR="$SCRIPT_DIR/macos"
TEMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/soa-publish-release.XXXXXX")"
PUBLIC_PEM="$TEMP_ROOT/update-public.pem"
UPDATE_CLONE="$TEMP_ROOT/launcher-updates"

cleanup() {
  rm -rf "$TEMP_ROOT"
}
trap cleanup EXIT

for command_name in aws gh git jq wc awk tr grep sed mktemp find; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    echo "Required command not found: $command_name" >&2
    exit 1
  fi
done
if [ -z "$OPENSSL_BIN" ] || [ ! -x "$OPENSSL_BIN" ]; then
  echo "OpenSSL was not found. Set SOA_OPENSSL to an OpenSSL 3 executable." >&2
  exit 1
fi
if [ -z "$ACCOUNT_ID" ] || [ -z "$BUCKET" ]; then
  echo "Set SOA_R2_ACCOUNT_ID and SOA_R2_BUCKET." >&2
  exit 1
fi
if [ -z "${AWS_ACCESS_KEY_ID:-}" ] || [ -z "${AWS_SECRET_ACCESS_KEY:-}" ]; then
  echo "Set AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY for R2." >&2
  exit 1
fi
if [ ! -s "$APPIMAGE" ] || [ ! -s "$DMG" ]; then
  echo "Both the AppImage and DMG must exist and be non-empty." >&2
  exit 1
fi
if [ ! -s "$SCRIPT_DIR/soa-update-public-key.hex" ]; then
  echo "Missing packaging/soa-update-public-key.hex." >&2
  exit 1
fi

source "$SCRIPT_DIR/soa-seal.sh"
soa_seal_public_pem_from_hex_file "$OPENSSL_BIN" "$SCRIPT_DIR/soa-update-public-key.hex" "$PUBLIC_PEM"

for platform_dir in "$LINUX_DIR" "$MACOS_DIR"; do
  for name in manifest.json manifest.json.seal versions.json versions.json.seal; do
    if [ ! -s "$platform_dir/$name" ]; then
      echo "Required update metadata is missing: $platform_dir/$name" >&2
      exit 1
    fi
  done
done

soa_seal_verify_file_with_public_pem "$OPENSSL_BIN" "$PUBLIC_PEM" manifest "$LINUX_DIR/manifest.json" "$LINUX_DIR/manifest.json.seal"
soa_seal_verify_file_with_public_pem "$OPENSSL_BIN" "$PUBLIC_PEM" history "$LINUX_DIR/versions.json" "$LINUX_DIR/versions.json.seal"
soa_seal_verify_file_with_public_pem "$OPENSSL_BIN" "$PUBLIC_PEM" manifest "$MACOS_DIR/manifest.json" "$MACOS_DIR/manifest.json.seal"
soa_seal_verify_file_with_public_pem "$OPENSSL_BIN" "$PUBLIC_PEM" history "$MACOS_DIR/versions.json" "$MACOS_DIR/versions.json.seal"

LINUX_VERSION="$(jq -r '.version // empty' "$LINUX_DIR/manifest.json")"
MACOS_VERSION="$(jq -r '.version // empty' "$MACOS_DIR/manifest.json")"
if [ -z "$LINUX_VERSION" ] || [ "$LINUX_VERSION" != "$MACOS_VERSION" ]; then
  echo "Linux and macOS manifests must contain the same release version." >&2
  exit 1
fi
VERSION="$LINUX_VERSION"
TAG="${SOA_RELEASE_TAG:-v$VERSION}"
EXPECTED_APPIMAGE_NAME="Story_Of_Alicia-${VERSION}-x86_64.AppImage"
EXPECTED_DMG_NAME="Story_Of_Alicia-${VERSION}-macos.dmg"
if [ "$(basename "$APPIMAGE")" != "$EXPECTED_APPIMAGE_NAME" ]; then
  echo "Expected Linux release filename: $EXPECTED_APPIMAGE_NAME" >&2
  exit 1
fi
if [ "$(basename "$DMG")" != "$EXPECTED_DMG_NAME" ]; then
  echo "Expected macOS release filename: $EXPECTED_DMG_NAME" >&2
  exit 1
fi

validate_package_manifest() {
  local manifest="$1"
  local platform="$2"
  local package="$3"
  local base_url="$4"
  local file_name sha256 size expected_url expected_mirror
  file_name="$(basename "$package")"
  sha256="$("$OPENSSL_BIN" dgst -sha256 "$package" | awk '{print $NF}')"
  size="$(wc -c <"$package" | tr -d ' ')"
  expected_url="$base_url/$file_name"
  expected_mirror="https://github.com/$GITHUB_REPOSITORY/releases/download/$TAG/$file_name"
  jq -e \
    --arg platform "$platform" \
    --arg version "$VERSION" \
    --arg file_name "$file_name" \
    --arg sha256 "$sha256" \
    --argjson size "$size" \
    --arg url "$expected_url" \
    --arg mirror "$expected_mirror" \
    '.schema == 1
     and .platform == $platform
     and .version == $version
     and .file_name == $file_name
     and .sha256 == $sha256
     and .size == $size
     and .url == $url
     and (.mirrors | type == "array")
     and (.mirrors | index($mirror) != null)' "$manifest" >/dev/null
}

validate_history() {
  local history="$1"
  local platform="$2"
  jq -e --arg platform "$platform" --arg version "$VERSION" '
    .schema == 1
    and .platform == $platform
    and (.releases | type == "array")
    and ((.releases | length) >= 1)
    and ((.releases | length) <= 3)
    and (all(.releases[]; .platform == $platform))
    and (([.releases[].version] | unique | length) == (.releases | length))
    and (([.releases[].version] | map(select(. == $version)) | length) == 1)
  ' "$history" >/dev/null
}

validate_package_manifest "$LINUX_DIR/manifest.json" linux-x86_64 "$APPIMAGE" "https://r2.storyofalicia.com/$PREFIX/linux"
validate_package_manifest "$MACOS_DIR/manifest.json" macos "$DMG" "https://r2.storyofalicia.com/$PREFIX/macos"
validate_history "$LINUX_DIR/versions.json" linux-x86_64
validate_history "$MACOS_DIR/versions.json" macos

gh auth status >/dev/null
if gh release view "$TAG" --repo "$GITHUB_REPOSITORY" >/dev/null 2>&1; then
  gh release upload "$TAG" "$APPIMAGE" "$DMG" --repo "$GITHUB_REPOSITORY" --clobber
else
  gh release create "$TAG" "$APPIMAGE" "$DMG" \
    --repo "$GITHUB_REPOSITORY" \
    --verify-tag \
    --title "$TAG" \
    --generate-notes
fi


if ! gh repo clone "$GITHUB_REPOSITORY" "$UPDATE_CLONE" -- --branch "$UPDATE_BRANCH" --single-branch --depth 1 >/dev/null; then
  echo "Could not clone the $UPDATE_BRANCH branch. Create it before publishing." >&2
  exit 1
fi

find "$UPDATE_CLONE" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf -- {} +
mkdir -p "$UPDATE_CLONE/linux" "$UPDATE_CLONE/macos"
cp "$LINUX_DIR/manifest.json" "$LINUX_DIR/manifest.json.seal" "$LINUX_DIR/versions.json" "$LINUX_DIR/versions.json.seal" "$UPDATE_CLONE/linux/"
cp "$MACOS_DIR/manifest.json" "$MACOS_DIR/manifest.json.seal" "$MACOS_DIR/versions.json" "$MACOS_DIR/versions.json.seal" "$UPDATE_CLONE/macos/"

git_name="${SOA_GIT_AUTHOR_NAME:-$(git -C "$PROJECT_ROOT" config user.name || true)}"
git_email="${SOA_GIT_AUTHOR_EMAIL:-$(git -C "$PROJECT_ROOT" config user.email || true)}"
if [ -z "$git_name" ] || [ -z "$git_email" ]; then
  github_login="$(gh api user --jq .login)"
  git_name="${git_name:-$github_login}"
  git_email="${git_email:-$github_login@users.noreply.github.com}"
fi
git -C "$UPDATE_CLONE" config user.name "$git_name"
git -C "$UPDATE_CLONE" config user.email "$git_email"
git -C "$UPDATE_CLONE" add -A
if ! git -C "$UPDATE_CLONE" diff --cached --quiet; then
  git -C "$UPDATE_CLONE" commit -m "Update launcher metadata for $TAG" >/dev/null
  git -C "$UPDATE_CLONE" push origin "HEAD:$UPDATE_BRANCH"
fi

ENDPOINT="https://${ACCOUNT_ID}.r2.cloudflarestorage.com"
export AWS_DEFAULT_REGION="${AWS_DEFAULT_REGION:-auto}"

upload() {
  local source="$1"
  local destination="$2"
  local content_type="$3"
  local cache_control="$4"
  aws s3 cp "$source" "s3://$BUCKET/$destination" \
    --endpoint-url "$ENDPOINT" \
    --content-type "$content_type" \
    --cache-control "$cache_control" \
    --only-show-errors
}

upload "$APPIMAGE" "$PREFIX/linux/$(basename "$APPIMAGE")" application/octet-stream 'public,max-age=31536000,immutable'
upload "$DMG" "$PREFIX/macos/$(basename "$DMG")" application/octet-stream 'public,max-age=31536000,immutable'
upload "$LINUX_DIR/versions.json.seal" "$PREFIX/linux/versions.json.seal" text/plain 'no-cache'
upload "$LINUX_DIR/versions.json" "$PREFIX/linux/versions.json" application/json 'no-cache'
upload "$MACOS_DIR/versions.json.seal" "$PREFIX/macos/versions.json.seal" text/plain 'no-cache'
upload "$MACOS_DIR/versions.json" "$PREFIX/macos/versions.json" application/json 'no-cache'
upload "$LINUX_DIR/manifest.json.seal" "$PREFIX/linux/manifest.json.seal" text/plain 'no-cache'
upload "$MACOS_DIR/manifest.json.seal" "$PREFIX/macos/manifest.json.seal" text/plain 'no-cache'
upload "$LINUX_DIR/manifest.json" "$PREFIX/linux/manifest.json" application/json 'no-cache'
upload "$MACOS_DIR/manifest.json" "$PREFIX/macos/manifest.json" application/json 'no-cache'

printf 'Published %s to GitHub Releases, the %s metadata branch, and R2.\n' "$TAG" "$UPDATE_BRANCH"
