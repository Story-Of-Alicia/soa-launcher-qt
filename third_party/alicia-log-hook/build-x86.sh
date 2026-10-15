#!/usr/bin/env bash

# Story of Alicia Launcher
# SPDX-FileCopyrightText: 2026 Story of Alicia contributors
# SPDX-License-Identifier: GPL-3.0-only
# Additional terms apply, see ADDITIONAL_TERMS.md.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="${1:-$SCRIPT_DIR/out}"
CC_BIN="${2:-${SOA_MINGW_C_COMPILER:-i686-w64-mingw32-gcc}}"
CXX_BIN="${3:-${SOA_MINGW_CXX_COMPILER:-i686-w64-mingw32-g++}}"
OBJDUMP_BIN="${SOA_MINGW_OBJDUMP:-${CC_BIN%gcc}objdump}"

for compiler in "$CC_BIN" "$CXX_BIN" "$OBJDUMP_BIN"; do
  if ! command -v "$compiler" >/dev/null 2>&1; then
    echo "Required Windows x86 build tool not found: $compiler" >&2
    echo "Arch/CachyOS: install mingw-w64-gcc." >&2
    echo "Debian/Ubuntu: install gcc-mingw-w64-i686, g++-mingw-w64-i686, and binutils-mingw-w64-i686." >&2
    echo "macOS: install Homebrew's mingw-w64 package." >&2
    exit 1
  fi
done

BUILD_TEMP="$(mktemp -d)"
cleanup() {
  rm -rf "$BUILD_TEMP"
}
trap cleanup EXIT

mkdir -p "$OUTPUT_DIR"

COMMON_DEFINES=(
  -DWINVER=0x0601
  -D_WIN32_WINNT=0x0601
  -DUNICODE
  -D_UNICODE
)
MINHOOK_INCLUDE="$SCRIPT_DIR/minhook/include"
MINHOOK_SOURCE="$SCRIPT_DIR/minhook/src"

"$CC_BIN" -std=c11 -Os -Wall -Wextra "${COMMON_DEFINES[@]}" \
  -I"$MINHOOK_INCLUDE" -I"$MINHOOK_SOURCE" -I"$MINHOOK_SOURCE/hde" \
  -c "$MINHOOK_SOURCE/buffer.c" -o "$BUILD_TEMP/minhook-buffer.o"
"$CC_BIN" -std=c11 -Os -Wall -Wextra "${COMMON_DEFINES[@]}" \
  -I"$MINHOOK_INCLUDE" -I"$MINHOOK_SOURCE" -I"$MINHOOK_SOURCE/hde" \
  -c "$MINHOOK_SOURCE/hook.c" -o "$BUILD_TEMP/minhook-hook.o"
"$CC_BIN" -std=c11 -Os -Wall -Wextra "${COMMON_DEFINES[@]}" \
  -I"$MINHOOK_INCLUDE" -I"$MINHOOK_SOURCE" -I"$MINHOOK_SOURCE/hde" \
  -c "$MINHOOK_SOURCE/trampoline.c" -o "$BUILD_TEMP/minhook-trampoline.o"
"$CC_BIN" -std=c11 -Os -Wall -Wextra "${COMMON_DEFINES[@]}" \
  -I"$MINHOOK_INCLUDE" -I"$MINHOOK_SOURCE" -I"$MINHOOK_SOURCE/hde" \
  -c "$MINHOOK_SOURCE/hde/hde32.c" -o "$BUILD_TEMP/minhook-hde32.o"

"$CXX_BIN" -std=c++17 -Os -Wall -Wextra "${COMMON_DEFINES[@]}" \
  -I"$MINHOOK_INCLUDE" \
  -c "$SCRIPT_DIR/src/hook.cpp" -o "$BUILD_TEMP/hook.o"
"$CXX_BIN" -std=c++17 -Os -Wall -Wextra "${COMMON_DEFINES[@]}" \
  -I"$MINHOOK_INCLUDE" \
  -c "$SCRIPT_DIR/src/null_dsound.cpp" -o "$BUILD_TEMP/null-dsound.o"
"$CXX_BIN" -std=c++17 -Os -Wall -Wextra "${COMMON_DEFINES[@]}" \
  -I"$MINHOOK_INCLUDE" \
  -c "$SCRIPT_DIR/src/pipe_dsound.cpp" -o "$BUILD_TEMP/pipe-dsound.o"

"$CXX_BIN" -shared -s -static-libgcc -static-libstdc++ \
  -static -Wl,--no-undefined \
  -Wl,--subsystem,windows \
  "$BUILD_TEMP/hook.o" \
  "$BUILD_TEMP/null-dsound.o" \
  "$BUILD_TEMP/pipe-dsound.o" \
  "$BUILD_TEMP/minhook-buffer.o" \
  "$BUILD_TEMP/minhook-hook.o" \
  "$BUILD_TEMP/minhook-trampoline.o" \
  "$BUILD_TEMP/minhook-hde32.o" \
  -lkernel32 -ldsound -ldxguid -lwinmm -lole32 -lws2_32 \
  -o "$OUTPUT_DIR/SoaAliciaLogHook.dll"

"$CXX_BIN" -std=c++17 -Os -Wall -Wextra "${COMMON_DEFINES[@]}" \
  -static -Wl,--no-undefined -Wl,--subsystem,windows \
  -static-libgcc -static-libstdc++ \
  "$SCRIPT_DIR/src/injector.cpp" -lshell32 -lkernel32 \
  -o "$OUTPUT_DIR/SoaAliciaLogInjector.exe"

check_runtime_imports() {
  local artifact="$1"
  local imports
  imports="$("$OBJDUMP_BIN" -p "$artifact" \
    | sed -nE 's/^[[:space:]]*DLL Name:[[:space:]]*//p')"
  if [ -z "$imports" ]; then
    echo "Could not read PE imports from Alicia injector/compatibility artifact: $artifact" >&2
    exit 1
  fi

  local dependency
  while IFS= read -r dependency; do
    local dependency_lower
    dependency_lower="$(printf '%s' "$dependency" | tr '[:upper:]' '[:lower:]')"
    case "$dependency_lower" in
      libgcc_s_*.dll|libstdc++-6.dll|libwinpthread-1.dll|libssp-0.dll|libatomic-1.dll|libquadmath-0.dll)
        echo "Alicia injector/compatibility artifact has an unbundled MinGW runtime dependency: $dependency" >&2
        echo "Artifact: $artifact" >&2
        exit 1
        ;;
    esac
  done <<< "$imports"

  printf 'Verified self-contained MinGW runtime imports: %s\n' "$artifact"
}

for artifact in \
    "$OUTPUT_DIR/SoaAliciaLogHook.dll" \
    "$OUTPUT_DIR/SoaAliciaLogInjector.exe"; do
  if [ ! -s "$artifact" ]; then
    echo "Expected injector/compatibility artifact was not produced: $artifact" >&2
    exit 1
  fi
  if command -v file >/dev/null 2>&1; then
    description="$(file -b "$artifact")"
    if [[ "$description" != *"PE32 executable"* ]] || [[ "$description" == *"PE32+"* ]]; then
      echo "Log-hook artifact is not Windows x86 PE32: $artifact ($description)" >&2
      exit 1
    fi
  fi
  check_runtime_imports "$artifact"
done

printf 'Built Windows x86 Alicia injector and compatibility hook:\n  %s\n  %s\n' \
  "$OUTPUT_DIR/SoaAliciaLogInjector.exe" \
  "$OUTPUT_DIR/SoaAliciaLogHook.dll"
