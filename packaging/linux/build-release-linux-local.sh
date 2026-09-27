#!/usr/bin/env bash

set -euo pipefail

echo "SOA Linux release AppImage builder"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$SCRIPT_DIR/../build-common.sh"

if [ "$#" -gt 1 ]; then
  echo "Usage: $0 [launcher-source-directory]" >&2
  exit 2
fi

PROJECT_ROOT="$(soa_resolve_project_root "${1:-${SOA_SOURCE_DIR:-}}" "$SCRIPT_DIR")"
SCRIPT_DIR="$PROJECT_ROOT/packaging/linux"

echo "Launcher source directory: $PROJECT_ROOT"

BUILD_DIR="$(soa_absolute_directory "${SOA_BUILD_DIR:-$SCRIPT_DIR/build-appimage}")"
APPDIR="$(soa_absolute_directory "${SOA_APPDIR:-$SCRIPT_DIR/AppDir}")"
BUILD_TYPE="${SOA_BUILD_TYPE:-Release}"
soa_validate_output_directory "$BUILD_DIR" "$PROJECT_ROOT"
soa_validate_output_directory "$APPDIR" "$PROJECT_ROOT"
case "$BUILD_DIR/" in
  "$APPDIR/"*) echo "SOA_APPDIR must not contain SOA_BUILD_DIR." >&2; exit 1 ;;
esac
case "$APPDIR/" in
  "$BUILD_DIR/"*) echo "SOA_BUILD_DIR must not contain SOA_APPDIR." >&2; exit 1 ;;
esac
soa_validate_build_cache "$BUILD_DIR" "$PROJECT_ROOT" Ninja

if [ -n "${SOA_UPDATE_SIGNING_KEY:-}" ] && [ -f "$SOA_UPDATE_SIGNING_KEY" ]; then
  SOA_UPDATE_SIGNING_KEY="$(cd "$(dirname "$SOA_UPDATE_SIGNING_KEY")" && pwd -P)/$(basename "$SOA_UPDATE_SIGNING_KEY")"
  export SOA_UPDATE_SIGNING_KEY
fi

cd "$SCRIPT_DIR"

LINUXDEPLOY_TAG="${LINUXDEPLOY_TAG:-1-alpha-20250213-2}"
LINUXDEPLOY_SHA256="${LINUXDEPLOY_SHA256:-4648f278ab3ef31f819e67c30d50f462640e5365a77637d7e6f2ad9fd0b4522a}"

require_command() {
  local command_name="$1"
  if ! command -v "$command_name" >/dev/null 2>&1; then
    echo "Required command not found: $command_name" >&2
    exit 1
  fi
}

run_portability_check() {
  local root="$1"
  "$SCRIPT_DIR/check-linux-portability.sh" "$root"
}

get_tool() {
  local name="$1"
  local url="$2"
  local expected="$3"
  local temporary="${name}.download"

  if [ -z "$expected" ]; then
    echo "A SHA-256 checksum is required for $name." >&2
    exit 1
  fi

  if [ -f "$name" ]; then
    local actual
    actual="$(sha256sum "$name" | awk '{print $1}')"
    if [ "$actual" != "$expected" ]; then
      rm -f "$name"
    fi
  fi

  if [ ! -f "$name" ]; then
    echo "Downloading $name..."
    wget -q "$url" -O "$temporary"
    echo "$expected  $temporary" | sha256sum --check --status
    mv "$temporary" "$name"
    chmod +x "$name"
  fi
}

require_command cmake
require_command ninja
require_command wget
require_command file
require_command readelf
require_command find
require_command sha256sum
require_command swiftc
require_command timeout
require_command desktop-file-validate
require_command jq
require_command openssl
require_command patchelf
require_command i686-w64-mingw32-gcc
require_command i686-w64-mingw32-g++


if [ -z "${SOA_UPDATE_SIGNING_KEY:-}" ] || [ ! -f "$SOA_UPDATE_SIGNING_KEY" ]; then
  echo "SOA_UPDATE_SIGNING_KEY must point to soa-update-key.pem." >&2
  echo "Create one with: ./packaging/create-update-key.sh /path/to/private-directory" >&2
  exit 1
fi

EXPECTED_UPDATE_PUBLIC_KEY_HEX="$(tr -d '[:space:]' < "$PROJECT_ROOT/packaging/soa-update-public-key.hex" | tr '[:upper:]' '[:lower:]')"
if [[ ! "$EXPECTED_UPDATE_PUBLIC_KEY_HEX" =~ ^[0-9a-f]{64}$ ]]; then
  echo "packaging/soa-update-public-key.hex must contain exactly 64 hexadecimal characters." >&2
  exit 1
fi
DERIVED_UPDATE_PUBLIC_KEY_HEX="$(
  openssl pkey -in "$SOA_UPDATE_SIGNING_KEY" -pubout -outform DER \
    | tail -c 32 \
    | od -An -v -tx1 \
    | tr -d ' \n' \
    | tr '[:upper:]' '[:lower:]'
)"
if [ "$DERIVED_UPDATE_PUBLIC_KEY_HEX" != "$EXPECTED_UPDATE_PUBLIC_KEY_HEX" ]; then
  echo "SOA_UPDATE_SIGNING_KEY does not match packaging/soa-update-public-key.hex." >&2
  exit 1
fi

if [ -z "${QMAKE:-}" ]; then
  QMAKE="$(command -v qmake6 || true)"
fi

if [ -z "$QMAKE" ] || [ ! -x "$QMAKE" ]; then
  echo "Could not find a Qt 6 qmake executable. Set QMAKE=/path/to/qmake6." >&2
  exit 1
fi

export QMAKE

unset CFLAGS
unset CXXFLAGS
unset CPPFLAGS
unset LDFLAGS

rm -rf "$BUILD_DIR"
rm -rf "$APPDIR"
rm -rf "$SCRIPT_DIR/squashfs-root"

cmake \
  -S "$PROJECT_ROOT" \
  -B "$BUILD_DIR" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_INSTALL_BINDIR=bin \
  -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_INSTALL_DATADIR=share \
  -DCMAKE_INSTALL_LIBEXECDIR=libexec \
  -DBUILD_TESTING=OFF \
  -DSOA_REQUIRE_ALICIA_LOG_HOOK=ON \
  -DSOA_PORTABLE_BUILD=ON

LAUNCHER_VERSION="$(soa_build_value "$BUILD_DIR" "$BUILD_TYPE" version)"
if [ -n "${SOA_LAUNCHER_VERSION:-}" ] && [ "$SOA_LAUNCHER_VERSION" != "$LAUNCHER_VERSION" ]; then
  echo "SOA_LAUNCHER_VERSION must match the configured project version: $LAUNCHER_VERSION" >&2
  exit 1
fi

cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel

DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --config "$BUILD_TYPE" --prefix /usr

NETWORK_NAME="$(soa_build_value "$BUILD_DIR" "$BUILD_TYPE" network_name)"
for required_artifact in "$APPDIR/usr/bin/soa_launcher" "$APPDIR/usr/lib/$NETWORK_NAME"; do
  if [ ! -s "$required_artifact" ]; then
    echo "Required installed launcher artifact is missing: $required_artifact" >&2
    exit 1
  fi
done

mkdir -p "$APPDIR/usr/share/soa-launcher/update"
jq -n \
  --arg version "$LAUNCHER_VERSION" \
  --arg manifest_url "https://r2.storyofalicia.com/launcher/linux/manifest.json" \
  --arg fallback_manifest_url \
    "https://raw.githubusercontent.com/Story-Of-Alicia/soa-launcher-qt/launcher-updates/linux/manifest.json" \
  --arg signing_public_key "$EXPECTED_UPDATE_PUBLIC_KEY_HEX" \
  '{schema: 1, version: $version, platform: "linux-x86_64",
    manifest_url: $manifest_url, fallback_manifest_url: $fallback_manifest_url,
    signing_public_key: $signing_public_key}' \
  >"$APPDIR/usr/share/soa-launcher/update/manifest.json"

cp "$SCRIPT_DIR/soa-launcher.png" "$APPDIR/soa-launcher.png"
cp "$SCRIPT_DIR/soa-launcher.desktop" "$APPDIR/soa-launcher.desktop"

desktop-file-validate "$APPDIR/usr/share/applications/soa-launcher.desktop"

get_tool \
  linuxdeploy-x86_64.AppImage \
  "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_TAG/linuxdeploy-x86_64.AppImage" \
  "$LINUXDEPLOY_SHA256"

SWIFT_LIB="$(soa_swift_runtime_library_path "$(command -v swiftc)" "$APPDIR/usr/lib/$NETWORK_NAME")"

QT_VERSION="$("$QMAKE" -query QT_VERSION)"
QT_PLUGIN_DIR="$("$QMAKE" -query QT_INSTALL_PLUGINS)"
QT_TRANSLATIONS_DIR="$("$QMAKE" -query QT_INSTALL_TRANSLATIONS)"
QT_LIB_DIR="$("$QMAKE" -query QT_INSTALL_LIBS)"
QT_PLATFORM_DIR="$QT_PLUGIN_DIR/platforms"

if [[ "$QT_VERSION" != 6.* ]]; then
  echo "Qt 6 is required, but $QMAKE reports Qt $QT_VERSION." >&2
  exit 1
fi

if [ ! -d "$QT_LIB_DIR" ]; then
  echo "Qt library directory reported by qmake does not exist: $QT_LIB_DIR" >&2
  exit 1
fi

if [ ! -e "$QT_LIB_DIR/libQt6Core.so.6" ]; then
  echo "Qt 6 runtime libraries were not found in: $QT_LIB_DIR" >&2
  exit 1
fi

if ! soa_swift_runtime_has_core "$SWIFT_LIB"; then
  echo "Swift Linux runtime libraries were not found: $SWIFT_LIB" >&2
  exit 1
fi

export LD_LIBRARY_PATH="${QT_LIB_DIR}:${SWIFT_LIB}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export NO_STRIP=1

if command -v locale >/dev/null 2>&1 \
    && LC_ALL=C.UTF-8 locale charmap >/dev/null 2>&1; then
  export LANG=C.UTF-8
  export LC_ALL=C.UTF-8
fi

copy_qt_library_family() {
  local stem="$1"
  local required="${2:-1}"
  local matches=()

  shopt -s nullglob
  matches=("$QT_LIB_DIR/$stem".so*)
  shopt -u nullglob

  if [ "${#matches[@]}" -eq 0 ]; then
    if [ "$required" = "1" ]; then
      echo "Required Qt library not found: $QT_LIB_DIR/$stem.so*" >&2
      exit 1
    fi
    return 0
  fi

  mkdir -p "$APPDIR/usr/lib"
  cp -a "${matches[@]}" "$APPDIR/usr/lib/"
}

copy_qt_platform_dependency() {
  local consumer="$1"
  local soname="$2"
  local source

  source="$(
    ldd "$consumer" 2>/dev/null \
      | awk -v expected="$soname" \
          '$1 == expected && $2 == "=>" && $3 ~ /^\// { print $3; exit }'
  )"
  if [ -z "$source" ] || [ ! -f "$source" ]; then
    echo "Required Qt platform dependency not found: $soname (needed by $consumer)" >&2
    exit 1
  fi

  mkdir -p "$APPDIR/usr/lib"
  install -m 755 "$source" "$APPDIR/usr/lib/$soname"
  echo "Included Qt platform dependency: $soname"
}

copy_qt_plugin() {
  local category="$1"
  local filename="$2"
  local required="${3:-0}"
  local source="$QT_PLUGIN_DIR/$category/$filename"
  local destination="$APPDIR/usr/plugins/$category"
  local dependencies

  if [ ! -f "$source" ]; then
    if [ "$required" = "1" ]; then
      echo "Required Qt plugin not found: $source" >&2
      exit 1
    fi
    return 0
  fi

  dependencies="$(ldd "$source" 2>/dev/null || true)"
  if grep -qE '=> not found|libheif\.so\.1' <<< "$dependencies"; then
    if [ "$required" = "1" ]; then
      echo "Required Qt plugin has unresolved dependencies: $source" >&2
      grep '=> not found' <<< "$dependencies" >&2 || true
      exit 1
    fi
    echo "Skipping optional Qt plugin with unresolved dependencies: $source" >&2
    grep '=> not found' <<< "$dependencies" >&2 || true
    return 0
  fi

  mkdir -p "$destination"
  install -m 755 "$source" "$destination/$filename"
  patchelf --set-rpath '$ORIGIN/../../lib' "$destination/$filename"
  echo "Included Qt plugin: $category/$filename"
}

copy_qt_plugin_directory() {
  local category="$1"
  local source

  if [ ! -d "$QT_PLUGIN_DIR/$category" ]; then
    return 0
  fi

  while IFS= read -r -d '' source; do
    copy_qt_plugin "$category" "$(basename "$source")"
  done < <(find "$QT_PLUGIN_DIR/$category" -maxdepth 1 -type f -name '*.so' -print0)
}

copy_qt_plugin platforms libqxcb.so 1
copy_qt_platform_dependency "$QT_PLATFORM_DIR/libqxcb.so" libxcb-cursor.so.0
copy_qt_platform_dependency "$QT_PLATFORM_DIR/libqxcb.so" libxkbcommon-x11.so.0

if [ -f "$QT_PLATFORM_DIR/libqwayland.so" ]; then
  copy_qt_plugin platforms libqwayland.so 1
elif [ -f "$QT_PLATFORM_DIR/libqwayland-egl.so" ] \
    && [ -f "$QT_PLATFORM_DIR/libqwayland-generic.so" ]; then
  copy_qt_plugin platforms libqwayland-egl.so 1
  copy_qt_plugin platforms libqwayland-generic.so 1
else
  echo "Required Qt Wayland platform plugin not found in: $QT_PLATFORM_DIR" >&2
  exit 1
fi

copy_qt_plugin imageformats libqgif.so
copy_qt_plugin imageformats libqico.so
copy_qt_plugin imageformats libqjpeg.so
copy_qt_plugin imageformats libqsvg.so
copy_qt_plugin imageformats libqwebp.so
copy_qt_plugin iconengines libqsvgicon.so

copy_qt_library_family libQt6Core
copy_qt_library_family libQt6Gui
copy_qt_library_family libQt6Widgets
copy_qt_library_family libQt6Network
copy_qt_library_family libQt6Concurrent
copy_qt_library_family libQt6Svg
copy_qt_library_family libQt6OpenGL
copy_qt_library_family libQt6XcbQpa

copy_qt_library_family libQt6WaylandClient
copy_qt_library_family libQt6WaylandEglClientHwIntegration 0
copy_qt_library_family libQt6WlShellIntegration 0

copy_qt_plugin_directory platforminputcontexts
copy_qt_plugin_directory xcbglintegrations
copy_qt_plugin_directory wayland-decoration-client
copy_qt_plugin_directory wayland-graphics-integration-client
copy_qt_plugin_directory wayland-shell-integration
copy_qt_plugin_directory networkinformation
copy_qt_plugin_directory tls

mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/translations"
cat >"$APPDIR/usr/bin/qt.conf" <<'EOF'
[Paths]
Prefix = ..
Plugins = plugins
Translations = translations
EOF

cat >"$APPDIR/AppRun" <<'EOF'
#!/usr/bin/env sh
set -eu

SOA_BUNDLED_QT_RUNTIME=1
export SOA_BUNDLED_QT_RUNTIME

if [ "${LD_LIBRARY_PATH+x}" = x ]; then
  SOA_HOST_LD_LIBRARY_PATH_SET=1
  SOA_HOST_LD_LIBRARY_PATH="$LD_LIBRARY_PATH"
  export SOA_HOST_LD_LIBRARY_PATH_SET SOA_HOST_LD_LIBRARY_PATH
else
  SOA_HOST_LD_LIBRARY_PATH_SET=0
  export SOA_HOST_LD_LIBRARY_PATH_SET
  unset SOA_HOST_LD_LIBRARY_PATH
fi

if [ "${QT_PLUGIN_PATH+x}" = x ]; then
  SOA_HOST_QT_PLUGIN_PATH_SET=1
  SOA_HOST_QT_PLUGIN_PATH="$QT_PLUGIN_PATH"
  export SOA_HOST_QT_PLUGIN_PATH_SET SOA_HOST_QT_PLUGIN_PATH
else
  SOA_HOST_QT_PLUGIN_PATH_SET=0
  export SOA_HOST_QT_PLUGIN_PATH_SET
  unset SOA_HOST_QT_PLUGIN_PATH
fi

if [ "${QT_QPA_PLATFORM_PLUGIN_PATH+x}" = x ]; then
  SOA_HOST_QT_QPA_PLATFORM_PLUGIN_PATH_SET=1
  SOA_HOST_QT_QPA_PLATFORM_PLUGIN_PATH="$QT_QPA_PLATFORM_PLUGIN_PATH"
  export SOA_HOST_QT_QPA_PLATFORM_PLUGIN_PATH_SET SOA_HOST_QT_QPA_PLATFORM_PLUGIN_PATH
else
  SOA_HOST_QT_QPA_PLATFORM_PLUGIN_PATH_SET=0
  export SOA_HOST_QT_QPA_PLATFORM_PLUGIN_PATH_SET
  unset SOA_HOST_QT_QPA_PLATFORM_PLUGIN_PATH
fi

SOA_APPIMAGE_ENV_ACTIVE=1
export SOA_APPIMAGE_ENV_ACTIVE

APPDIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
export LD_LIBRARY_PATH="$APPDIR/usr/lib:$APPDIR/usr/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$APPDIR/usr/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$APPDIR/usr/plugins/platforms"
exec "$APPDIR/usr/bin/soa_launcher" "$@"
EOF
chmod 755 "$APPDIR/AppRun"

if [ -d "$QT_TRANSLATIONS_DIR" ]; then
  find "$QT_TRANSLATIONS_DIR" -maxdepth 1 -type f \
    \( -name 'qt_*.qm' -o -name 'qtbase_*.qm' \) \
    -exec cp -f '{}' "$APPDIR/usr/translations/" \;
fi

while IFS= read -r -d '' packaged_plugin; do
  if ldd "$packaged_plugin" 2>/dev/null | grep -q 'libheif\.so\.1'; then
    echo "A curated Qt plugin unexpectedly references libheif.so.1: $packaged_plugin" >&2
    exit 1
  fi
done < <(find "$APPDIR/usr/plugins" -type f -name '*.so' -print0)

GENERATED_OUTPUT="$SCRIPT_DIR/Story_Of_Alicia-x86_64.AppImage"
OUTPUT="$SCRIPT_DIR/Story_Of_Alicia-${LAUNCHER_VERSION}-x86_64.AppImage"
rm -f "$GENERATED_OUTPUT" "$OUTPUT"

./linuxdeploy-x86_64.AppImage \
  --appdir "$APPDIR" \
  --output appimage \
  --desktop-file "$APPDIR/usr/share/applications/soa-launcher.desktop" \
  --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/soa-launcher.png"

run_portability_check "$APPDIR"

if [ ! -f "$GENERATED_OUTPUT" ]; then
  echo "The expected AppImage was not produced: $GENERATED_OUTPUT" >&2
  exit 1
fi
mv "$GENERATED_OUTPUT" "$OUTPUT"

outer_isa="$(readelf -nW "$OUTPUT" 2>/dev/null | grep -E 'x86 ISA needed|x86 feature needed' || true)"
if grep -qE 'x86-64-v[234]' <<< "$outer_isa"; then
  printf 'The AppImage runtime has a nonportable ISA requirement:\n%s\n' "$outer_isa" >&2
  exit 1
fi

(
  cd "$SCRIPT_DIR"
  APPIMAGE_EXTRACT_AND_RUN=1 "$OUTPUT" --appimage-extract >/dev/null
)

if ! grep -q 'SOA_BUNDLED_QT_RUNTIME' "$SCRIPT_DIR/squashfs-root/AppRun" \
    || ! grep -q 'SOA_APPIMAGE_ENV_ACTIVE' "$SCRIPT_DIR/squashfs-root/AppRun"; then
  echo "The bundled-library AppRun wrapper or host-environment handoff is missing from the AppImage." >&2
  exit 1
fi

for required_platform_library in \
    libQt6WaylandClient.so.6 \
    libQt6XcbQpa.so.6 \
    libxcb-cursor.so.0 \
    libxkbcommon-x11.so.0; do
  if [ ! -s "$SCRIPT_DIR/squashfs-root/usr/lib/$required_platform_library" ]; then
    echo "Required Qt platform library is missing from the AppImage: $required_platform_library" >&2
    exit 1
  fi
done

run_portability_check "$SCRIPT_DIR/squashfs-root"

SMOKE_ROOT="$(mktemp -d)"
SMOKE_LOG="$(mktemp)"

cleanup_smoke() {
  rm -rf "$SMOKE_ROOT"
  rm -f "$SMOKE_LOG"
}

trap cleanup_smoke EXIT

mkdir -p \
  "$SMOKE_ROOT/home" \
  "$SMOKE_ROOT/config" \
  "$SMOKE_ROOT/data" \
  "$SMOKE_ROOT/cache" \
  "$SMOKE_ROOT/runtime"

SMOKE_STATUS=0
set +e

if command -v xvfb-run >/dev/null 2>&1; then
  timeout 8 env \
    LD_LIBRARY_PATH= \
    HOME="$SMOKE_ROOT/home" \
    XDG_CONFIG_HOME="$SMOKE_ROOT/config" \
    XDG_DATA_HOME="$SMOKE_ROOT/data" \
    XDG_CACHE_HOME="$SMOKE_ROOT/cache" \
    XDG_RUNTIME_DIR="$SMOKE_ROOT/runtime" \
    QT_QPA_PLATFORM=xcb \
    xvfb-run -a -s "-screen 0 1280x800x24" \
    "$SCRIPT_DIR/squashfs-root/AppRun" >"$SMOKE_LOG" 2>&1
  SMOKE_STATUS=$?
elif [ -n "${DISPLAY:-}" ]; then
  timeout 8 env \
    LD_LIBRARY_PATH= \
    HOME="$SMOKE_ROOT/home" \
    XDG_CONFIG_HOME="$SMOKE_ROOT/config" \
    XDG_DATA_HOME="$SMOKE_ROOT/data" \
    XDG_CACHE_HOME="$SMOKE_ROOT/cache" \
    XDG_RUNTIME_DIR="$SMOKE_ROOT/runtime" \
    QT_QPA_PLATFORM=xcb \
    "$SCRIPT_DIR/squashfs-root/AppRun" >"$SMOKE_LOG" 2>&1
  SMOKE_STATUS=$?
elif [ "${CI:-false}" = "true" ]; then
  echo "xvfb-run is required for the headless AppImage smoke test in CI." >&2
  SMOKE_STATUS=1
else
  echo "Skipping GUI smoke test because xvfb-run is unavailable and DISPLAY is unset." >&2
fi

set -e

if [ "$SMOKE_STATUS" -ne 0 ] && [ "$SMOKE_STATUS" -ne 124 ]; then
  cat "$SMOKE_LOG" >&2
  exit "$SMOKE_STATUS"
fi

cleanup_smoke

rm -rf "$SCRIPT_DIR/squashfs-root"

"$SCRIPT_DIR/generate-linux-update-metadata.sh" "$LAUNCHER_VERSION" "$OUTPUT"


echo "Done. The AppImage and signed update metadata are in: $SCRIPT_DIR"
