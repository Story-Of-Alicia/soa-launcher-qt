#!/usr/bin/env bash

# Story of Alicia Launcher
# SPDX-FileCopyrightText: 2026 Story Of Alicia Contributors
# SPDX-License-Identifier: GPL-3.0-only
# Additional terms apply, see ADDITIONAL_TERMS.md.

set -euo pipefail
umask 077

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ANALYSER="$SCRIPT_DIR/soa-log-analyze.py"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT_DIR="${SOA_DIAGNOSTICS_DIR:-$HOME/soa-linux-diagnostics-$STAMP}"
ARCHIVE="${OUT_DIR%/}.tar.gz"
DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
STATE_HOME="${XDG_STATE_HOME:-$HOME/.local/state}"
MAX_RUNS="${SOA_DIAGNOSTIC_MAX_RUNS:-12}"
INCLUDE_REGISTRY=0
FULL_LOGS=0
LOG_BUDGET_MIB="${SOA_DIAGNOSTIC_LOG_BUDGET_MIB:-256}"
LOG_BUDGET_USED=0

validate_budget() {
    if [[ ! "$LOG_BUDGET_MIB" =~ ^[0-9]+$ ]] || (( 10#$LOG_BUDGET_MIB < 1 || 10#$LOG_BUDGET_MIB > 4096 )); then
        printf 'SOA_DIAGNOSTIC_LOG_BUDGET_MIB must be between 1 and 4096\n' >&2
        exit 2
    fi
    LOG_BUDGET_BYTES=$((10#$LOG_BUDGET_MIB * 1024 * 1024))
}

file_bytes() {
    local value
    value="$(stat -f %z "$1" 2>/dev/null || true)"
    if [[ "$value" =~ ^[0-9]+$ ]]; then
        printf '%s\n' "$value"
    else
        stat -c %s -- "$1"
    fi
}

copy_run_file() {
    local source="$1" target="$2" name bytes allowance head_bytes tail_bytes limit
    name="$(basename -- "$source")"
    bytes="$(file_bytes "$source")"
    case "$name" in
        wine.log|steam-*.log|proton.log) limit=$((64 * 1024 * 1024)) ;;
        *.log) limit=$((16 * 1024 * 1024)) ;;
        *) limit=$((2 * 1024 * 1024)) ;;
    esac

    if [[ "$name" == *.log ]] && (( FULL_LOGS )); then
        redact < "$source" > "$target"
        return
    fi

    allowance="$limit"
    if [[ "$name" == *.log ]]; then
        if (( LOG_BUDGET_BYTES - LOG_BUDGET_USED < allowance )); then
            allowance=$((LOG_BUDGET_BYTES - LOG_BUDGET_USED))
        fi
    fi
    if (( allowance <= 0 )); then
        printf '%s: omitted (%s bytes); diagnostic log budget exhausted\n' "$name" "$bytes" >> "$(dirname -- "$target")/TRUNCATED.txt"
        return
    fi
    if (( bytes <= allowance )); then
        head -c "$bytes" -- "$source" | redact > "$target"
        [[ "$name" != *.log ]] || LOG_BUDGET_USED=$((LOG_BUDGET_USED + bytes))
        return
    fi

    head_bytes=$((allowance / 4))
    if (( head_bytes > 8 * 1024 * 1024 )); then
        head_bytes=$((8 * 1024 * 1024))
    fi
    tail_bytes=$((allowance - head_bytes))
    if (( head_bytes > 0 )); then
        head -c "$head_bytes" -- "$source" | sed '$d' | redact > "$target"
    else
        : > "$target"
    fi
    printf '\n[SOA COLLECTOR: middle of %s omitted]\n' "$name" >> "$target"
    if (( tail_bytes > 0 )); then
        tail -c "$tail_bytes" -- "$source" | sed '1d' | redact >> "$target"
    fi
    [[ "$name" != *.log ]] || LOG_BUDGET_USED=$((LOG_BUDGET_USED + allowance))
    printf '%s: original %s bytes; retained up to %s bytes from start/end before redaction\n' "$name" "$bytes" "$allowance" >> "$(dirname -- "$target")/TRUNCATED.txt"
}


usage() {
    printf 'Usage: %s [--include-registry] [--full-logs]\n' "${0##*/}"
}

for arg in "$@"; do
    case "$arg" in
        --include-registry) INCLUDE_REGISTRY=1 ;;
        --full-logs) FULL_LOGS=1 ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done

if [[ ! "$MAX_RUNS" =~ ^[0-9]+$ ]] || (( 10#$MAX_RUNS < 1 || 10#$MAX_RUNS > 50 )); then
    printf 'SOA_DIAGNOSTIC_MAX_RUNS must be between 1 and 50\n' >&2
    exit 2
fi

validate_budget

if [[ -e "$OUT_DIR" || -e "$ARCHIVE" ]]; then
    printf 'Destination already exists: %s or %s\n' "$OUT_DIR" "$ARCHIVE" >&2
    exit 1
fi

mkdir -p -- "$OUT_DIR"

redact() {
    sed -E \
        -e 's/(-OP[[:space:]]+)(\[[^]]*\]|[^[:space:]]+)/\1[REDACTED]/g' \
        -e "s/(-OP['\"],[[:space:]]*['\"])[^'\"]+/\1[REDACTED]/g" \
        -e "s/(-OP[[:space:]]*=[[:space:]]*['\"]?)[^'\",[:space:]]+/\1[REDACTED]/g" \
        -e 's/([?&](token|code|state|oauth_state|launcher_state)=)[^&[:space:]]+/\1[REDACTED]/g' \
        -e 's/(^|[[:space:]])((token|access_token|refresh_token|auth_token)=)[^[:space:]]+/\1\2[REDACTED]/g' \
        -e 's/(Bearer[[:space:]]+)[A-Za-z0-9._~+\/-]+/\1[REDACTED]/g' \
        -e 's/("([Tt][Oo][Kk][Ee][Nn]|[Pp][Aa][Ss][Ss][Ww][Oo][Rr][Dd]|[Ss][Ee][Cc][Rr][Ee][Tt])"[[:space:]]*:[[:space:]]*")[^"]*/\1[REDACTED]/g'
}

{
    echo 'Story of Alicia Linux diagnostic summary'
    echo "Created (UTC): $STAMP"
    echo
    echo "Kernel: $(uname -srmo 2>/dev/null || echo unknown)"
    if [[ -r /etc/os-release ]]; then
        ( . /etc/os-release; echo "Distribution: ${PRETTY_NAME:-unknown}" )
    fi
    echo "glibc: $(getconf GNU_LIBC_VERSION 2>/dev/null || echo unknown)"
    echo "Session type: ${XDG_SESSION_TYPE:-unknown}"
    echo "Desktop: ${XDG_CURRENT_DESKTOP:-unknown}"
    echo
    echo 'Wine executables on PATH:'
    for name in wine wine64 wineserver proton; do
        if path="$(command -v "$name" 2>/dev/null)"; then
            echo "  $name: $path"
        fi
    done
    echo
    echo 'Graphics hardware and kernel drivers:'
    if command -v lspci >/dev/null 2>&1; then
        lspci -k 2>/dev/null | grep -A 3 -E 'VGA|3D|Display' || true
    else
        echo '  lspci unavailable'
    fi
    echo
    echo 'OpenGL:'
    if command -v glxinfo >/dev/null 2>&1; then
        glxinfo -B 2>/dev/null | grep -E 'OpenGL renderer|OpenGL version|Device:' || echo '  Could not query OpenGL'
    else
        echo '  glxinfo unavailable'
    fi
    echo
    echo 'Vulkan:'
    if command -v vulkaninfo >/dev/null 2>&1; then
        vulkaninfo --summary 2>/dev/null | grep -E 'deviceName|driverInfo|driverName' || echo '  Could not query Vulkan'
    else
        echo '  vulkaninfo unavailable'
    fi
    echo
    echo 'Audio:'
    if command -v pactl >/dev/null 2>&1; then
        LC_ALL=C pactl info 2>/dev/null | grep -E 'Server Name|Default Sink' || echo '  Could not query PulseAudio/PipeWire'
    else
        echo '  pactl unavailable'
    fi
    echo
    echo '32-bit system library candidates (not a Proton compatibility test):'
    if command -v ldconfig >/dev/null 2>&1; then
        ldconfig -p 2>/dev/null | grep -E '^[[:space:]]+lib(c|vulkan)\.so(\.[0-9]+)? \(libc6\)' | head -n 24 || true
    else
        echo '  ldconfig unavailable'
    fi
} > "$OUT_DIR/summary.txt" 2>&1

cat > "$OUT_DIR/REVIEW_BEFORE_SHARING.txt" <<'TEXT'
Review the entire archive before sharing it.
Logs, launch timelines, Wine registry files and command lines can contain
usernames, file paths, authentication tokens or other private information.
The collector redacts recognizable tokens in collected launcher and game logs.
This is best effort; sensitive values can remain, so inspect the archive.
Original launcher and game logs are not modified by the collector.
Large game logs may be truncated or omitted; read TRUNCATED.txt in each run.
Use --full-logs only when complete game logs are specifically needed.
The collector does not copy config.json or complete Wine prefixes.
Registry files are copied only when --include-registry was supplied.
TEXT

sources=()
add_source() {
    local candidate="$1" previous
    [[ -d "$candidate" ]] || return 0
    for previous in "${sources[@]-}"; do
        [[ "$previous" == "$candidate" ]] && return 0
    done
    sources+=("$candidate")
}

for base in "$DATA_HOME" "$CONFIG_HOME" "$STATE_HOME"; do
    add_source "$base/Story of Alicia"
    add_source "$base/Story Of Alicia"
    add_source "$base/Story of Alicia/Story of Alicia Launcher"
    add_source "$base/Story Of Alicia/Story Of Alicia Launcher"
done

candidate_list="$(mktemp)"
trap 'rm -f -- "$candidate_list"' EXIT

for index in "${!sources[@]}"; do
    source_dir="${sources[$index]}"
    source_id="source-$((index + 1))"
    logs_dir="$source_dir/logs"
    destination="$OUT_DIR/launcher/$source_id"
    copied=0
    for file in "$source_dir/launcher.log" "$logs_dir/launcher.log" "$logs_dir/soa-launcher.log"; do
        if [[ -f "$file" && ! -L "$file" ]]; then
            mkdir -p -- "$destination"
            filename="$(basename -- "$file")"
            if [[ -e "$destination/$filename" ]]; then
                filename="$(basename -- "$(dirname -- "$file")")-$filename"
            fi
            max_log_bytes=$((64 * 1024 * 1024))
            log_bytes="$(stat -c %s -- "$file" 2>/dev/null || echo 0)"
            if (( log_bytes > max_log_bytes )); then
                tail -c "$max_log_bytes" -- "$file" | redact > "$destination/$filename"
                printf '%s: copied the last 64 MiB of a %s-byte log
' "$filename" "$log_bytes" >> "$destination/TRUNCATED.txt"
            else
                redact < "$file" > "$destination/$filename"
            fi
            copied=1
        fi
    done
    if (( copied )); then
        printf 'Source: %s\n' "$source_dir" > "$destination/SOURCE.txt"
    fi
    for diagnostic_dir in "$source_dir/logs/diagnostics" "$source_dir/diagnostics"; do
        [[ -d "$diagnostic_dir" ]] || continue
        while IFS= read -r -d '' run_dir; do
            printf '%s %s\0' "$(stat -c %Y -- "$run_dir" 2>/dev/null || echo 0)" "$run_dir" >> "$candidate_list"
        done < <(find "$diagnostic_dir" -mindepth 1 -maxdepth 1 -type d -name 'run-*' -print0 2>/dev/null)
    done
done

mkdir -p -- "$OUT_DIR/runs"
selected=0
newest_run=''
while IFS= read -r -d '' record; do
    (( selected < 10#$MAX_RUNS )) || break
    run_dir="${record#* }"
    [[ -d "$run_dir" ]] || continue
    if [[ ! -f "$run_dir/wine.log" && ! -f "$run_dir/timeline.jsonl" && ! -f "$run_dir/alicia.log" && ! -f "$run_dir/proton.log" && -z "$(find "$run_dir" -maxdepth 1 -type f -name 'steam-*.log' -print -quit)" ]]; then
        continue
    fi
    ((selected += 1))
    destination="$OUT_DIR/runs/$selected-$(basename -- "$run_dir")"
    mkdir -p -- "$destination"
    printf 'Source: %s\n' "$run_dir" > "$destination/SOURCE.txt"
    while IFS= read -r -d '' artifact; do
        copy_run_file "$artifact" "$destination/$(basename -- "$artifact")"
    done < <(find "$run_dir" -mindepth 1 -maxdepth 1 -type f \
        \( -name '*.log' -o -name '*.jsonl' -o -name 'summary.txt' -o -name '*sample*.txt' \) -print0)
    if [[ -z "$newest_run" && ( -f "$destination/wine.log" || -f "$destination/proton.log" || -n "$(find "$destination" -maxdepth 1 -type f -name 'steam-*.log' -print -quit)" ) ]]; then
        newest_run="$destination"
    fi
done < <(sort -z -rn -- "$candidate_list")

{
    echo "Runs collected: $selected"
    echo "Game log budget (MiB): $LOG_BUDGET_MIB (disabled with --full-logs)"
    echo "Game log bytes retained before redaction: $LOG_BUDGET_USED"
    echo "Launcher sources found: ${#sources[@]}"
    echo
    echo 'Prefixes found (contents not collected by default):'
    prefix_count=0
    prefix_candidates=()
    [[ -n "${WINEPREFIX:-}" ]] && prefix_candidates+=("$WINEPREFIX")
    for source_dir in "${sources[@]-}"; do
        for prefix_base in "$source_dir/prefixes" "$source_dir/prefix"; do
            [[ -d "$prefix_base" ]] || continue
            while IFS= read -r -d '' registry; do
                prefix_candidates+=("$(dirname -- "$registry")")
            done < <(find "$prefix_base" -maxdepth 3 -name system.reg -type f -print0 2>/dev/null)
        done
    done
    for prefix_dir in "${prefix_candidates[@]-}"; do
        [[ -d "$prefix_dir" ]] || continue
        ((prefix_count += 1))
        echo "  $prefix_count: $prefix_dir"
        echo "    system.reg: $([[ -f "$prefix_dir/system.reg" ]] && echo yes || echo no)"
        echo "    user.reg: $([[ -f "$prefix_dir/user.reg" ]] && echo yes || echo no)"
        echo "    syswow64: $([[ -d "$prefix_dir/drive_c/windows/syswow64" ]] && echo yes || echo no)"
        if (( INCLUDE_REGISTRY )); then
            mkdir -p -- "$OUT_DIR/prefix/$prefix_count"
            for name in system.reg user.reg userdef.reg; do
                if [[ -f "$prefix_dir/$name" && ! -L "$prefix_dir/$name" ]]; then
                    cp -- "$prefix_dir/$name" "$OUT_DIR/prefix/$prefix_count/$name"
                fi
            done
        fi
    done
    (( prefix_count > 0 )) || echo '  No Wine prefix was detected automatically'
} > "$OUT_DIR/prefix-summary.txt"

if [[ -n "$newest_run" ]]; then
    if [[ -f "$ANALYSER" ]] && command -v python3 >/dev/null 2>&1; then
        if python3 "$ANALYSER" "$newest_run" --platform linux --json "$OUT_DIR/analysis.json" > "$OUT_DIR/analysis.txt" 2>&1; then
            printf 'Analysis completed for %s\n' "$(basename -- "$newest_run")"
        else
            printf 'Log analysis failed; see analysis.txt\n' >&2
        fi
    else
        printf 'Log analyzer unavailable; wine.log files were preserved.\n' > "$OUT_DIR/analysis.txt"
    fi
else
    printf 'No Wine or Proton debug log found in the collected diagnostic runs.\n' > "$OUT_DIR/analysis.txt"
fi

mkdir -p -- "$(dirname -- "$ARCHIVE")"
if [[ -n "$newest_run" && -s "$newest_run/TRUNCATED.txt" ]]; then
    printf '\nWARNING: Selected launch logs are incomplete. See runs/*/TRUNCATED.txt for details.\n' >> "$OUT_DIR/analysis.txt"
fi

if [[ -f "$ANALYSER" ]] && command -v python3 >/dev/null 2>&1; then
    if ! python3 "$ANALYSER" --compare-timelines "$OUT_DIR/runs" > "$OUT_DIR/timeline-summary.txt" 2>&1; then
        printf 'No usable launch timeline comparison was generated; see timeline-summary.txt.\n' >&2
    fi
else
    printf 'Timeline comparison unavailable: Python 3 or the analyzer is missing.\n' > "$OUT_DIR/timeline-summary.txt"
fi

tar -czf "$ARCHIVE" -C "$(dirname -- "$OUT_DIR")" "$(basename -- "$OUT_DIR")"
printf 'Diagnostics directory: %s\nArchive: %s\n\nReview the archive before sharing.\n' "$OUT_DIR" "$ARCHIVE"
if [[ -f "$OUT_DIR/analysis.txt" ]]; then
    sed -n '/^VERDICT/,$p' "$OUT_DIR/analysis.txt" | head -n 16 || true
fi
