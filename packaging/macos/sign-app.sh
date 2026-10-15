#!/usr/bin/env bash

# Story of Alicia Launcher
# SPDX-FileCopyrightText: 2026 Story of Alicia contributors
# SPDX-License-Identifier: GPL-3.0-only
# Additional terms apply, see ADDITIONAL_TERMS.md.

set -euo pipefail

APP="${1:?Application bundle path is required}"
IDENTITY="${2:?Signing identity is required}"
ENTITLEMENTS="${3:-}"

if [ ! -d "$APP" ]; then
  echo "Application bundle does not exist: $APP" >&2
  exit 1
fi

MAIN_EXECUTABLE_NAME="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$APP/Contents/Info.plist")"
case "$MAIN_EXECUTABLE_NAME" in
  ""|*/*)
    echo "Invalid CFBundleExecutable in $APP/Contents/Info.plist" >&2
    exit 1
    ;;
esac
MAIN_EXECUTABLE="$APP/Contents/MacOS/$MAIN_EXECUTABLE_NAME"
if [ ! -f "$MAIN_EXECUTABLE" ]; then
  echo "Application executable does not exist: $MAIN_EXECUTABLE" >&2
  exit 1
fi

remove_signature() {
  codesign --remove-signature "$1" >/dev/null 2>&1 || true
}

is_macho() {
  local description
  description="$(file -L -b "$1")"
  [[ "$description" == *"Mach-O"* ]]
}

is_signable() {
  case "$1" in
    *.dylib|*.so) return 0 ;;
    *) is_macho "$1" ;;
  esac
}

sign_item() {
  local item="$1"
  remove_signature "$item"
  printf 'Signing nested code: %s\n' "$item"
  if [ "$IDENTITY" = "-" ]; then
    codesign --force --sign - "$item"
  else
    codesign --force --timestamp --options runtime --sign "$IDENTITY" "$item"
  fi
}

while IFS= read -r -d '' item; do
  if [ "$item" != "$MAIN_EXECUTABLE" ] && is_signable "$item"; then
    remove_signature "$item"
  fi
done < <(find "$APP/Contents" \( -type f -o -type l \) -print0)

while IFS= read -r -d '' bundle; do
  remove_signature "$bundle"
done < <(find "$APP" -depth -type d \( \
  -name '*.framework' -o \
  -name '*.app' -o \
  -name '*.xpc' -o \
  -name '*.appex' -o \
  -name '*.bundle' \
\) ! -path "$APP" -print0)

remove_signature "$APP"

while IFS= read -r -d '' item; do
  if [ "$item" != "$MAIN_EXECUTABLE" ] && is_signable "$item"; then
    sign_item "$item"
  fi
done < <(find "$APP/Contents" \( -type f -o -type l \) -print0)

while IFS= read -r -d '' bundle; do
  sign_item "$bundle"
done < <(find "$APP" -depth -type d \( \
  -name '*.framework' -o \
  -name '*.app' -o \
  -name '*.xpc' -o \
  -name '*.appex' -o \
  -name '*.bundle' \
\) ! -path "$APP" -print0)

printf 'Signing application bundle: %s\n' "$APP"
if [ "$IDENTITY" = "-" ]; then
  if [ -n "$ENTITLEMENTS" ] && [ -f "$ENTITLEMENTS" ]; then
    codesign --force --entitlements "$ENTITLEMENTS" --sign - "$APP"
  else
    codesign --force --sign - "$APP"
  fi
elif [ -n "$ENTITLEMENTS" ] && [ -f "$ENTITLEMENTS" ]; then
  codesign --force --timestamp --options runtime --entitlements "$ENTITLEMENTS" --sign "$IDENTITY" "$APP"
else
  codesign --force --timestamp --options runtime --sign "$IDENTITY" "$APP"
fi

codesign --verify --deep --strict --verbose=4 "$APP"

if [ "$IDENTITY" = "-" ]; then
  while IFS= read -r -d '' item; do
    if is_signable "$item"; then
      if codesign -dvv "$item" 2>&1 | grep -q '^TeamIdentifier='; then
        team_id="$(codesign -dvv "$item" 2>&1 | sed -n 's/^TeamIdentifier=//p' | head -n 1)"
        if [ -n "$team_id" ] && [ "$team_id" != "not set" ]; then
          echo "Unexpected TeamIdentifier after ad-hoc signing: $item ($team_id)" >&2
          exit 1
        fi
      fi
    fi
  done < <(find "$APP/Contents" \( -type f -o -type l \) -print0)
fi
