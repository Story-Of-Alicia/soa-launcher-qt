#!/usr/bin/env bash

set -euo pipefail

ROOT="${1:?Usage: check-linux-portability.sh APPDIR}"
ROOT="$(cd "$ROOT" && pwd -P)"
MAX_GLIBC="${SOA_MAX_GLIBC:-2.35}"
MAX_GLIBCXX="${SOA_MAX_GLIBCXX:-3.4.30}"
FAILED=0
for required_artifact in "$ROOT/usr/bin/soa_launcher" "$ROOT/usr/lib/libsoa_network.so"; do
  if [ ! -s "$required_artifact" ]; then
    echo "Required installed launcher artifact is missing: $required_artifact" >&2
    FAILED=1
  fi
done
BUNDLE_LIBRARY_PATH="$ROOT/usr/lib"
if [ -d "$ROOT/usr/lib64" ]; then
  BUNDLE_LIBRARY_PATH="$BUNDLE_LIBRARY_PATH:$ROOT/usr/lib64"
fi

version_greater() {
  [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -n 1)" = "$1" ] && [ "$1" != "$2" ]
}

while IFS= read -r -d '' item; do
  if ! file -b "$item" 2>/dev/null | grep -q '^ELF '; then
    continue
  fi

  isa="$(readelf -nW "$item" 2>/dev/null | grep -E 'x86 ISA needed|x86 feature needed' || true)"
  if grep -qE 'x86-64-v[234]' <<< "$isa"; then
    printf 'Nonportable ISA requirement: %s\n%s\n' "$item" "$isa" >&2
    FAILED=1
  fi

  glibc="$(readelf --version-info -W "$item" 2>/dev/null | grep -oE 'GLIBC_[0-9]+(\.[0-9]+)+' | sed 's/^GLIBC_//' | sort -V | tail -n 1 || true)"
  glibcxx="$(readelf --version-info -W "$item" 2>/dev/null | grep -oE 'GLIBCXX_[0-9]+(\.[0-9]+)+' | sed 's/^GLIBCXX_//' | sort -V | tail -n 1 || true)"

  if [ -n "$glibc" ] && version_greater "$glibc" "$MAX_GLIBC"; then
    printf 'GLIBC requirement too new: %s requires %s, maximum is %s\n' "$item" "$glibc" "$MAX_GLIBC" >&2
    FAILED=1
  fi
  if [ -n "$glibcxx" ] && version_greater "$glibcxx" "$MAX_GLIBCXX"; then
    printf 'GLIBCXX requirement too new: %s requires %s, maximum is %s\n' "$item" "$glibcxx" "$MAX_GLIBCXX" >&2
    FAILED=1
  fi
done < <(find "$ROOT" -type f -print0)

if ! find "$ROOT" \( -type f -o -type l \) -path '*/platforms/libqxcb.so' -print -quit | grep -q .; then
  echo "Qt XCB platform plugin is missing." >&2
  FAILED=1
fi

wayland_single="$(find "$ROOT" \( -type f -o -type l \) -path '*/platforms/libqwayland.so' -print -quit)"
wayland_egl="$(find "$ROOT" \( -type f -o -type l \) -path '*/platforms/libqwayland-egl.so' -print -quit)"
wayland_generic="$(find "$ROOT" \( -type f -o -type l \) -path '*/platforms/libqwayland-generic.so' -print -quit)"

if [ -z "$wayland_single" ] && { [ -z "$wayland_egl" ] || [ -z "$wayland_generic" ]; }; then
  echo "Qt Wayland platform plugin is missing." >&2
  FAILED=1
fi

if ! find "$ROOT" \( -type f -o -type l \) -name 'libswiftCore.so*' \
    -print -quit | grep -q .; then
  echo "Bundled Swift runtime is missing libswiftCore.so." >&2
  FAILED=1
fi

HOOK_ROOT="$ROOT/usr/libexec/soa-launcher/alicia-log-hook"
for hook_artifact in SoaAliciaLogInjector.exe SoaAliciaLogHook.dll; do
  hook_path="$HOOK_ROOT/$hook_artifact"
  if [ ! -s "$hook_path" ]; then
    echo "Bundled Alicia diagnostic component is missing: $hook_path" >&2
    FAILED=1
    continue
  fi
  hook_description="$(file -b "$hook_path" 2>/dev/null || true)"
  if [[ "$hook_description" != *"PE32 executable"* ]] \
      || [[ "$hook_description" == *"PE32+"* ]]; then
    echo "Alicia diagnostic component is not Windows x86 PE32: $hook_path ($hook_description)" >&2
    FAILED=1
  fi
done

while IFS= read -r -d '' item; do
  if ! file -b "$item" 2>/dev/null | grep -q '^ELF '; then
    continue
  fi



  dependencies="$(env -u LD_PRELOAD LD_LIBRARY_PATH="$BUNDLE_LIBRARY_PATH" ldd "$item" 2>&1 || true)"
  while IFS= read -r dependency; do
    [ -n "$dependency" ] || continue
    printf 'Unresolved AppImage dependency: %s: %s\n' "$item" "$dependency" >&2
    FAILED=1
  done < <(grep '=> not found' <<< "$dependencies" || true)

  while IFS= read -r dependency; do
    [ -n "$dependency" ] || continue
    if [[ "$dependency" != *"$ROOT"* ]]; then
      printf 'Swift dependency resolved outside AppDir: %s: %s\n' \
        "$item" "$dependency" >&2
      FAILED=1
    fi
  done < <(grep -E 'libswift|libdispatch|libFoundation' <<< "$dependencies" \
    | grep -v '=> not found' || true)
done < <(find "$ROOT" -type f -print0)

if [ "$FAILED" -ne 0 ]; then
  exit 1
fi
