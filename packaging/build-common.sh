#!/usr/bin/env bash

soa_is_project_root() {
  [ -f "$1/CMakeLists.txt" ] \
    && [ -f "$1/cmake/ProjectSettings.cmake" ] \
    && [ -f "$1/src/app/CMakeLists.txt" ] \
    && [ -f "$1/packaging/soa-update-public-key.hex" ]
}

soa_resolve_project_root() {
  local requested="$1"
  local script_dir="$2"
  local start candidate

  if [ -n "$requested" ]; then
    candidate="$(cd "$requested" 2>/dev/null && pwd -P)" || {
      printf 'Launcher source directory does not exist: %s\n' "$requested" >&2
      return 1
    }
    if soa_is_project_root "$candidate"; then
      printf '%s\n' "$candidate"
      return 0
    fi
    printf 'Not a launcher source root: %s\n' "$candidate" >&2
    return 1
  fi

  for start in "$PWD" "$script_dir"; do
    candidate="$(cd "$start" && pwd -P)" || return 1
    while :; do
      if soa_is_project_root "$candidate"; then
        printf '%s\n' "$candidate"
        return 0
      fi
      [ "$candidate" != / ] || break
      candidate="$(dirname "$candidate")"
    done
  done

  echo "Could not find the launcher root. Set SOA_SOURCE_DIR to the complete source directory." >&2
  return 1
}

soa_absolute_directory() {
  mkdir -p "$1" || return 1
  (cd "$1" && pwd -P)
}

soa_validate_output_directory() {
  local directory="$1"
  local project_root="$2"
  if [ "$directory" = / ]; then
    echo "The filesystem root cannot be used as an output directory." >&2
    return 1
  fi
  case "$project_root/" in
    "$directory/"*)
      printf 'Output directory must not contain the launcher sources: %s\n' "$directory" >&2
      return 1
      ;;
  esac
  if [ -f "$directory/CMakeLists.txt" ]; then
    printf 'Output directory contains CMake sources: %s\n' "$directory" >&2
    return 1
  fi
  case "$directory/" in
    "$project_root/src/"*|"$project_root/include/"*|"$project_root/cmake/"*|"$project_root/assets/"*|"$project_root/tests/"*|"$project_root/third_party/"*|"$project_root/tools/"*|"$project_root/translations/"*|"$project_root/docker/"*|"$project_root/packaging/"|"$project_root/packaging/linux/"|"$project_root/packaging/macos/")
      printf 'Output directory overlaps launcher source files: %s\n' "$directory" >&2
      return 1
      ;;
  esac
}

soa_validate_build_cache() {
  local build_dir="$1"
  local project_root="$2"
  local generator="$3"
  local key value

  [ -f "$build_dir/CMakeCache.txt" ] || return 0
  while IFS='=' read -r key value; do
    case "$key" in
      CMAKE_HOME_DIRECTORY:INTERNAL) [ "$value" = "$project_root" ] && continue ;;
      CMAKE_CACHEFILE_DIR:INTERNAL) [ "$value" = "$build_dir" ] && continue ;;
      CMAKE_GENERATOR:INTERNAL) [ "$value" = "$generator" ] && continue ;;
      *) continue ;;
    esac
    printf 'CMake cache does not match this build: %s=%s\nChoose a fresh SOA_BUILD_DIR.\n' "$key" "$value" >&2
    return 1
  done < "$build_dir/CMakeCache.txt"
}

soa_build_value() {
  local build_dir="$1"
  local configuration="$2"
  local key="$3"
  local info="$build_dir/soa-packaging/$configuration.txt"
  local line

  if [ ! -f "$info" ]; then
    printf 'CMake packaging information is missing: %s\nReconfigure the complete launcher project.\n' "$info" >&2
    return 1
  fi
  while IFS= read -r line; do
    case "$line" in
      "$key="*) printf '%s\n' "${line#*=}"; return 0 ;;
    esac
  done < "$info"
  printf 'CMake packaging information has no %s entry: %s\n' "$key" "$info" >&2
  return 1
}

soa_swift_runtime_library_path() {
  local compiler="${1:-swiftc}"
  local consumer="${2:-}"
  local target_info parser output runtime_path reported probe dependencies
  local name path directory result core_found

  if [ -n "${SOA_SWIFT_RUNTIME_LIBRARY_PATH:-}" ]; then
    printf '%s\n' "$SOA_SWIFT_RUNTIME_LIBRARY_PATH"
    return 0
  fi

  reported=""
  target_info="$(mktemp "${TMPDIR:-/tmp}/soa-swift-target-info.XXXXXX")" || return 1
  parser="${target_info}.cmake"
  output="${target_info}.paths"

  if "$compiler" -print-target-info >"$target_info" 2>/dev/null; then
    cat >"$parser" <<'EOF'
if(NOT DEFINED SOA_TARGET_INFO OR NOT DEFINED SOA_OUTPUT)
  message(FATAL_ERROR "Missing Swift target-info parser input.")
endif()
file(READ "${SOA_TARGET_INFO}" target_info)
string(JSON runtime_count ERROR_VARIABLE runtime_error LENGTH "${target_info}" paths runtimeLibraryPaths)
if(runtime_error STREQUAL "NOTFOUND" AND runtime_count GREATER 0)
  math(EXPR runtime_last "${runtime_count} - 1")
  foreach(index RANGE 0 ${runtime_last})
    string(JSON runtime_path GET "${target_info}" paths runtimeLibraryPaths ${index})
    file(APPEND "${SOA_OUTPUT}" "${runtime_path}\n")
  endforeach()
endif()
EOF
    cmake \
      -DSOA_TARGET_INFO="$target_info" \
      -DSOA_OUTPUT="$output" \
      -P "$parser" >/dev/null 2>&1 || true
  fi

  if [ -f "$output" ]; then
    while IFS= read -r runtime_path; do
      [ -n "$runtime_path" ] || continue
      [ -d "$runtime_path" ] || continue
      case ":$reported:" in
        *":$runtime_path:"*) ;;
        *) reported="${reported:+$reported:}$runtime_path" ;;
      esac
    done <"$output"
  fi

  rm -f "$target_info" "$parser" "$output"

  if [ -n "$consumer" ] && [ -e "$consumer" ]; then
    probe="$reported"
    if [ -n "${LD_LIBRARY_PATH:-}" ]; then
      probe="${probe:+$probe:}$LD_LIBRARY_PATH"
    fi

    if [ -n "$probe" ]; then
      dependencies="$(LD_LIBRARY_PATH="$probe" ldd "$consumer" 2>/dev/null || true)"
    else
      dependencies="$(ldd "$consumer" 2>/dev/null || true)"
    fi

    result=""
    core_found=0
    while IFS=$'\t' read -r name path; do
      [ -n "$name" ] || continue
      [ -n "$path" ] || continue
      [ -e "$path" ] || [ -L "$path" ] || continue
      directory="$(dirname "$path")"
      case ":$result:" in
        *":$directory:"*) ;;
        *) result="${result:+$result:}$directory" ;;
      esac
      case "$name" in
        libswiftCore.so*) core_found=1 ;;
      esac
    done < <(
      awk '
        $2 == "=>" && $3 ~ /^\// &&
        $1 ~ /^(lib.*swift.*\.so|libFoundation.*\.so|libdispatch\.so|libBlocksRuntime\.so|lib_FoundationICU\.so)/ {
          print $1 "\t" $3
        }
      ' <<<"$dependencies"
    )

    if [ "$core_found" = 1 ] && [ -n "$result" ]; then
      printf '%s\n' "$result"
      return 0
    fi
  fi

  if [ -n "$reported" ] && soa_swift_runtime_has_core "$reported"; then
    printf '%s\n' "$reported"
    return 0
  fi

  if [ -n "$consumer" ]; then
    printf 'Could not resolve libswiftCore.so for: %s\n' "$consumer" >&2
  else
    echo "Could not resolve libswiftCore.so from the active Swift toolchain." >&2
  fi
  echo "Set SOA_SWIFT_RUNTIME_LIBRARY_PATH only when the runtime is intentionally outside the loader and compiler search paths." >&2
  return 1
}

soa_swift_runtime_has_core() {
  local path_list="$1"
  local directory match
  local directories=()
  local matches=()

  IFS=':' read -r -a directories <<<"$path_list"
  for directory in "${directories[@]}"; do
    [ -d "$directory" ] || continue
    shopt -s nullglob
    matches=("$directory"/libswiftCore.so* "$directory"/libswiftCore.dylib)
    shopt -u nullglob
    for match in "${matches[@]}"; do
      [ -e "$match" ] || [ -L "$match" ] || continue
      return 0
    done
  done
  return 1
}

