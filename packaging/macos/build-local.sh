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
if [ "$(uname -s)" != Darwin ]; then
  echo "The macOS app must be built on macOS." >&2
  exit 1
fi
BUILD_DIR="$(soa_absolute_directory "${SOA_BUILD_DIR:-$PROJECT_ROOT/build-macos-local}")"
soa_validate_output_directory "$BUILD_DIR" "$PROJECT_ROOT"
soa_validate_build_cache "$BUILD_DIR" "$PROJECT_ROOT" Xcode
ARCHS="${SOA_MACOS_ARCHS:-${SOA_MACOS_ARCH:-x86_64;arm64}}"
BUILD_TYPE="${SOA_BUILD_TYPE:-Release}"
IFS=';' read -r -a REQUESTED_ARCHS <<< "$ARCHS"

for tool in cmake ctest swift xcrun lipo otool install_name_tool file i686-w64-mingw32-gcc i686-w64-mingw32-g++; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Required tool not found: $tool" >&2
    exit 1
  fi
done

SOA_QT_VERSION="${SOA_QT_VERSION:-6.8.3}"
QT_DOWNLOAD_ROOT="${SOA_QT_INSTALL_ROOT:-$HOME/Qt}"
QT_EXPLICIT="${SOA_QT_PREFIX:-${QT_ROOT_DIR:-}}"
QT_PREFIX="$QT_EXPLICIT"

qt_core_binary() {
  local prefix="$1" candidate
  for candidate in \
    "$prefix/lib/QtCore.framework/Versions/A/QtCore" \
    "$prefix/lib/QtCore.framework/QtCore" \
    "$prefix/lib/libQt6Core.dylib"; do
    if [ -f "$candidate" ]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

qt_has_architectures() {
  local core="$1" architectures requested_arch
  architectures="$(lipo -archs "$core" 2>/dev/null)" || return 1
  for requested_arch in "${REQUESTED_ARCHS[@]}"; do
    case " $architectures " in
      *" $requested_arch "*) ;;
      *) return 1 ;;
    esac
  done
}

if [ -z "$QT_PREFIX" ]; then
  for candidate in "$QT_DOWNLOAD_ROOT/$SOA_QT_VERSION/macos"; do
    if core="$(qt_core_binary "$candidate" 2>/dev/null)" && qt_has_architectures "$core"; then
      QT_PREFIX="$candidate"
      break
    fi
  done
fi
if [ -z "$QT_PREFIX" ] && command -v qtpaths6 >/dev/null 2>&1; then
  QT_PREFIX="$(qtpaths6 --query QT_INSTALL_PREFIX 2>/dev/null || true)"
fi
if [ -z "$QT_PREFIX" ] && command -v qmake6 >/dev/null 2>&1; then
  QT_PREFIX="$(qmake6 -query QT_INSTALL_PREFIX 2>/dev/null || true)"
fi
if [ -z "$QT_PREFIX" ] && command -v qmake >/dev/null 2>&1; then
  QT_VERSION="$(qmake -query QT_VERSION 2>/dev/null || true)"
  if [[ "$QT_VERSION" == 6.* ]]; then
    QT_PREFIX="$(qmake -query QT_INSTALL_PREFIX 2>/dev/null || true)"
  fi
fi
if [ -z "$QT_PREFIX" ] && command -v brew >/dev/null 2>&1; then
  QT_PREFIX="$(brew --prefix qt 2>/dev/null || brew --prefix qt@6 2>/dev/null || true)"
fi

QT_CORE_BINARY="$(qt_core_binary "$QT_PREFIX" 2>/dev/null || true)"
if [ -z "$QT_CORE_BINARY" ] || ! qt_has_architectures "$QT_CORE_BINARY"; then
  if [ -n "$QT_EXPLICIT" ]; then
    printf 'Qt at %s does not support the requested architectures: %s\n' "$QT_EXPLICIT" "$ARCHS" >&2
    if [ -n "$QT_CORE_BINARY" ]; then
      printf 'QtCore architectures: %s\n' "$(lipo -archs "$QT_CORE_BINARY")" >&2
    fi
    exit 1
  fi

  QT_PREFIX="$QT_DOWNLOAD_ROOT/$SOA_QT_VERSION/macos"
  QT_CORE_BINARY="$(qt_core_binary "$QT_PREFIX" 2>/dev/null || true)"
  if [ -z "$QT_CORE_BINARY" ] || ! qt_has_architectures "$QT_CORE_BINARY"; then
    AQT="$(command -v aqt || true)"
    if [ -z "$AQT" ] && [ -x "$HOME/.local/bin/aqt" ]; then
      AQT="$HOME/.local/bin/aqt"
    fi
    if [ -z "$AQT" ]; then
      printf 'A compatible Qt installation is required for: %s\n' "$ARCHS" >&2
      printf 'Install aqtinstall yourself (e.g. using pipx), then retry, or set SOA_QT_PREFIX.\n' >&2
      exit 1
    fi
    printf 'Installing universal Qt %s in %s via aqt...\n' "$SOA_QT_VERSION" "$QT_DOWNLOAD_ROOT"
    "$AQT" install-qt mac desktop "$SOA_QT_VERSION" clang_64 -O "$QT_DOWNLOAD_ROOT"
    QT_CORE_BINARY="$(qt_core_binary "$QT_PREFIX" 2>/dev/null || true)"
    if [ -z "$QT_CORE_BINARY" ] || ! qt_has_architectures "$QT_CORE_BINARY"; then
      printf 'The Qt installation is incomplete or lacks the requested architectures: %s\n' "$ARCHS" >&2
      exit 1
    fi
  fi
fi

MACDEPLOYQT="${MACDEPLOYQT:-$QT_PREFIX/bin/macdeployqt}"
if [ ! -x "$MACDEPLOYQT" ]; then
  printf 'macdeployqt was not found in Qt prefix: %s\n' "$QT_PREFIX" >&2
  printf 'Set SOA_QT_PREFIX to a full Qt 6 macOS installation.\n' >&2
  exit 1
fi
printf 'Qt: %s (%s)\n' "$QT_PREFIX" "$(lipo -archs "$QT_CORE_BINARY")"

CMAKE_QT_ARGS=()
if [ -n "$QT_PREFIX" ]; then
  CMAKE_QT_ARGS+=("-DCMAKE_PREFIX_PATH=$QT_PREFIX")
fi

cmake \
  -S "$PROJECT_ROOT" \
  -B "$BUILD_DIR" \
  -G Xcode \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
  "${CMAKE_QT_ARGS[@]}" \
  -DSOA_REQUIRE_ALICIA_LOG_HOOK=ON \
  -DBUILD_TESTING=ON

cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel
if ctest --test-dir "$BUILD_DIR" -C "$BUILD_TYPE" --output-on-failure; then
  printf 'All local tests passed.\n'
else
  if [ "${SOA_REQUIRE_TESTS:-0}" = 1 ]; then
    printf 'Tests failed. Aborting strict build.\n' >&2
    exit 1
  fi
  printf 'Warning: some local tests failed. Continuing with app packaging and signing.\n' >&2
fi

APP="$(soa_build_value "$BUILD_DIR" "$BUILD_TYPE" app_bundle)"
if [ ! -d "$APP" ]; then
  printf 'The configured app bundle was not built: %s\n' "$APP" >&2
  exit 1
fi

"$MACDEPLOYQT" "$APP" -always-overwrite -verbose=1

BINARY="$(soa_build_value "$BUILD_DIR" "$BUILD_TYPE" executable)"
ACTUAL_ARCHS="$(lipo -archs "$BINARY")"
for requested_arch in "${REQUESTED_ARCHS[@]}"; do
  case " $ACTUAL_ARCHS " in
    *" $requested_arch "*) ;;
    *)
      echo "The launcher binary does not contain requested architecture $requested_arch: $ACTUAL_ARCHS" >&2
      exit 1
      ;;
  esac
done

NETWORK_NAME="$(soa_build_value "$BUILD_DIR" "$BUILD_TYPE" network_name)"
COURIER="$APP/Contents/Frameworks/$NETWORK_NAME"
if [ ! -f "$COURIER" ]; then
  echo "The Swift Courier library is missing from the application bundle." >&2
  exit 1
fi
COURIER_ARCHS="$(lipo -archs "$COURIER")"
for requested_arch in "${REQUESTED_ARCHS[@]}"; do
  case " $COURIER_ARCHS " in
    *" $requested_arch "*) ;;
    *)
      echo "The Swift network library does not contain requested architecture $requested_arch: $COURIER_ARCHS" >&2
      exit 1
      ;;
  esac
done
COURIER_INSTALL_NAME="$(otool -D "$COURIER" | tail -n 1)"
if [ "$COURIER_INSTALL_NAME" != "@rpath/$NETWORK_NAME" ]; then
  echo "Unexpected Swift network install name: $COURIER_INSTALL_NAME" >&2
  exit 1
fi
if ! otool -L "$BINARY" | grep -F "@rpath/$NETWORK_NAME" >/dev/null; then
  echo "The launcher does not reference the bundled Swift network library through @rpath." >&2
  exit 1
fi
HOOK_ROOT="$APP/Contents/Resources/alicia-log-hook"
AUDIO_HOST="$HOOK_ROOT/soa-audio-host"
if [ ! -s "$AUDIO_HOST" ]; then
  echo "The bundled CoreAudio helper is missing: $AUDIO_HOST" >&2
  exit 1
fi
AUDIO_HOST_ARCHS="$(lipo -archs "$AUDIO_HOST")"
for requested_arch in "${REQUESTED_ARCHS[@]}"; do
  case " $AUDIO_HOST_ARCHS " in
    *" $requested_arch "*) ;;
    *)
      echo "The audio host does not contain requested architecture $requested_arch: $AUDIO_HOST_ARCHS" >&2
      exit 1
      ;;
  esac
done
for hook_artifact in SoaAliciaLogInjector.exe SoaAliciaLogHook.dll README.md MINHOOK_LICENSE.txt; do
  if [ ! -s "$HOOK_ROOT/$hook_artifact" ]; then
    echo "The bundled Alicia injector/compatibility component is missing: $HOOK_ROOT/$hook_artifact" >&2
    exit 1
  fi
done
for hook_artifact in SoaAliciaLogInjector.exe SoaAliciaLogHook.dll; do
  hook_description="$(file -b "$HOOK_ROOT/$hook_artifact")"
  if [[ "$hook_description" != *"PE32 executable"* ]] \
      || [[ "$hook_description" == *"PE32+"* ]]; then
    echo "The Alicia injector/compatibility component is not Windows x86 PE32: $hook_description" >&2
    exit 1
  fi
done
soa_otool_dependencies() {
  otool -L "$1" | awk '/^[[:space:]]+/ && $1 ~ /^\// {print $1}' | sort -u
}

soa_otool_rpaths() {
  otool -l "$1" | awk '
    $1 == "cmd" && $2 == "LC_RPATH" { awaiting_path = 1; next }
    awaiting_path && $1 == "path" { print $2; awaiting_path = 0 }
  ' | sort -u
}

while IFS= read -r -d '' item; do
  if ! file -b "$item" | grep -q 'Mach-O'; then
    continue
  fi

  while IFS= read -r rpath; do
    case "$rpath" in
      /System/Library/*|/usr/lib/*) ;;
      /*)
        printf 'Removing nonportable runtime search path from %s: %s\n' "$item" "$rpath"
        install_name_tool -delete_rpath "$rpath" "$item"
        ;;
    esac
  done < <(soa_otool_rpaths "$item")

  while IFS= read -r dependency; do
    case "$dependency" in
      /System/Library/*|/usr/lib/*) ;;
      /*)
        printf 'Nonportable library dependency in %s: %s\n' "$item" "$dependency" >&2
        exit 1
        ;;
    esac
  done < <(soa_otool_dependencies "$item")

  while IFS= read -r rpath; do
    case "$rpath" in
      /System/Library/*|/usr/lib/*) ;;
      /*)
        printf 'Nonportable runtime search path remains in %s: %s\n' "$item" "$rpath" >&2
        exit 1
        ;;
    esac
  done < <(soa_otool_rpaths "$item")
done < <(find "$APP/Contents" -type f -print0)

bash "$SCRIPT_DIR/sign-app.sh" "$APP" -

printf '\nBuilt ad-hoc signed local app:\n  %s\n\n' "$APP"
printf 'Open it with:\n  open "%s"\n' "$APP"
