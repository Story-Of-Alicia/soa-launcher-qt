#!/usr/bin/env bash

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

for tool in cmake swift xcrun lipo otool file i686-w64-mingw32-gcc i686-w64-mingw32-g++; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Required tool not found: $tool" >&2
    exit 1
  fi
done

QT_PREFIX="${SOA_QT_PREFIX:-${QT_ROOT_DIR:-}}"
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

MACDEPLOYQT="${MACDEPLOYQT:-}"
if [ -z "$MACDEPLOYQT" ] && [ -n "$QT_PREFIX" ] && [ -x "$QT_PREFIX/bin/macdeployqt" ]; then
  MACDEPLOYQT="$QT_PREFIX/bin/macdeployqt"
fi
if [ -z "$MACDEPLOYQT" ]; then
  MACDEPLOYQT="$(command -v macdeployqt || true)"
fi
if [ -z "$MACDEPLOYQT" ] || [ ! -x "$MACDEPLOYQT" ]; then
  echo "macdeployqt was not found. Install Qt 6 or set SOA_QT_PREFIX/MACDEPLOYQT." >&2
  exit 1
fi

QT_CORE_BINARY=""
for candidate in \
  "$QT_PREFIX/lib/QtCore.framework/Versions/A/QtCore" \
  "$QT_PREFIX/lib/QtCore.framework/QtCore" \
  "$QT_PREFIX/lib/libQt6Core.dylib"; do
  if [ -f "$candidate" ]; then
    QT_CORE_BINARY="$candidate"
    break
  fi
done
if [ -z "$QT_CORE_BINARY" ]; then
  echo "QtCore was not found below Qt prefix: $QT_PREFIX" >&2
  echo "Set SOA_QT_PREFIX to a complete Qt 6 macOS installation." >&2
  exit 1
fi

QT_ARCHS="$(lipo -archs "$QT_CORE_BINARY")"
for requested_arch in "${REQUESTED_ARCHS[@]}"; do
  case " $QT_ARCHS " in
    *" $requested_arch "*) ;;
    *)
      echo "Qt at $QT_PREFIX does not contain requested architecture $requested_arch." >&2
      echo "QtCore architectures: $QT_ARCHS" >&2
      echo "Install a universal Qt build or request only an architecture present in QtCore." >&2
      exit 1
      ;;
  esac
done

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
  -DBUILD_TESTING=OFF

cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel

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
if otool -L "$BINARY" | grep -F "$PROJECT_ROOT" >/dev/null \
    || otool -L "$COURIER" | grep -F "$PROJECT_ROOT" >/dev/null; then
  echo "The launcher still contains a build-machine library path." >&2
  exit 1
fi

printf '\nBuilt unsigned local app:\n  %s\n\n' "$APP"
printf 'Signing: skipped by design\n'
printf 'Open it with:\n  open "%s"\n' "$APP"
