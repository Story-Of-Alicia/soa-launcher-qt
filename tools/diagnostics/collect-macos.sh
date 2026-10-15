#!/usr/bin/env bash

# Story of Alicia Launcher
# SPDX-FileCopyrightText: 2026 Story of Alicia contributors
# SPDX-License-Identifier: GPL-3.0-only
# Additional terms apply, see ADDITIONAL_TERMS.md.

set -euo pipefail
export LC_ALL=C
umask 077

APP_SUPPORT="$HOME/Library/Application Support/Story of Alicia"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT_DIR="${SOA_DIAGNOSTICS_DIR:-$HOME/Desktop/SOA-macOS-diagnostics-$STAMP}"
ARCHIVE="${OUT_DIR%/}.zip"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ANALYSER="$SCRIPT_DIR/soa-log-analyze.py"
FULL_LOGS=0
INCLUDE_REGISTRY=0
MAX_RUNS="${SOA_DIAGNOSTIC_MAX_RUNS:-12}"
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


for arg in "$@"; do
    case "$arg" in
        --full-logs) FULL_LOGS=1 ;;
        --include-registry) INCLUDE_REGISTRY=1 ;;
        -h|--help) printf 'Usage: %s [--full-logs] [--include-registry]\n' "${0##*/}"; exit 0 ;;
        *) printf 'Unknown argument: %s\n' "$arg" >&2; exit 2 ;;
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
    /usr/bin/sed -E \
        -e "s|$HOME|~|g" \
        -e 's/(-OP[[:space:]]+)(\[[^]]*\]|[^[:space:]]+)/\1[REDACTED]/g' \
        -e "s/(-OP['\"],[[:space:]]*['\"])[^'\"]+/\1[REDACTED]/g" \
        -e "s/(-OP[[:space:]]*=[[:space:]]*['\"]?)[^'\",[:space:]]+/\1[REDACTED]/g" \
        -e 's/([?&](token|code|state|oauth_state|launcher_state)=)[^&[:space:]]+/\1[REDACTED]/g' \
        -e 's/(^|[[:space:]])((token|access_token|refresh_token|auth_token)=)[^[:space:]]+/\1\2[REDACTED]/g' \
        -e 's/(Bearer[[:space:]]+)[A-Za-z0-9._~+\/-]+/\1[REDACTED]/g' \
        -e 's/("([Tt][Oo][Kk][Ee][Nn]|[Pp][Aa][Ss][Ss][Ww][Oo][Rr][Dd]|[Ss][Ee][Cc][Rr][Ee][Tt])"[[:space:]]*:[[:space:]]*")[^"]*/\1[REDACTED]/g'
}

{
    echo 'Story of Alicia macOS diagnostic summary'
    echo "Created (UTC): $STAMP"
    echo
    /usr/bin/sw_vers 2>/dev/null || true
    echo "Host architecture: $(/usr/bin/uname -m)"
    echo "Hardware model: $(/usr/sbin/sysctl -n hw.model 2>/dev/null || echo unknown)"
    echo "Logical CPUs: $(/usr/sbin/sysctl -n hw.logicalcpu 2>/dev/null || echo unknown)"
    echo "Memory bytes: $(/usr/sbin/sysctl -n hw.memsize 2>/dev/null || echo unknown)"
    if /usr/bin/arch -x86_64 /usr/bin/true >/dev/null 2>&1; then
        echo 'Rosetta x86_64 execution: available'
    else
        echo 'Rosetta x86_64 execution: unavailable'
    fi
    echo
    echo 'Display summary:'
    /usr/sbin/system_profiler SPDisplaysDataType -detailLevel mini 2>/dev/null \
        | /usr/bin/grep -E 'Chipset Model:|Resolution:|Main Display:|Retina:|Metal Support:' || true
} > "$OUT_DIR/system.txt"

LOG="$APP_SUPPORT/logs/launcher.log"
if [[ -f "$LOG" && ! -L "$LOG" ]]; then
    max_log_bytes=$((64 * 1024 * 1024))
    log_bytes="$(file_bytes "$LOG")"
    if (( log_bytes > max_log_bytes )); then
        /usr/bin/tail -c "$max_log_bytes" "$LOG" | redact > "$OUT_DIR/launcher-tail-redacted.log"
        printf 'launcher.log: copied the last 64 MiB of a %s-byte log before redaction\n' "$log_bytes" > "$OUT_DIR/TRUNCATED.txt"
    else
        redact < "$LOG" > "$OUT_DIR/launcher-tail-redacted.log"
    fi
else
    echo 'No launcher.log found in the application support directory.' > "$OUT_DIR/launcher-tail-redacted.log"
fi

{
    echo 'Current Alicia/Wine host processes (PID, parent PID, elapsed time, command):'
    /bin/ps -axo pid=,ppid=,etime=,command= 2>/dev/null \
        | /usr/bin/grep -Ei '[A]licia\.exe|[w]ine(64)?-preloader|[w]ineserver' \
        | redact || echo 'No matching Alicia/Wine process was present.'
} > "$OUT_DIR/game-processes.txt"

DIAGNOSTICS="$APP_SUPPORT/logs/diagnostics"
mkdir -p -- "$OUT_DIR/runs"
NEWEST_RUN=''
if [[ -d "$DIAGNOSTICS" ]]; then
    collected=0
    while IFS= read -r run; do
        [[ -d "$run" ]] || continue
        if [[ ! -f "$run/wine.log" && ! -f "$run/timeline.jsonl" && ! -f "$run/alicia.log" && ! -f "$run/proton.log" && -z "$(find "$run" -maxdepth 1 -type f -name 'steam-*.log' -print -quit)" ]]; then
            continue
        fi
        ((collected += 1))
        destination="$OUT_DIR/runs/$collected-$(basename -- "$run")"
        mkdir -p -- "$destination"
        printf 'Source: %s\n' "$run" > "$destination/SOURCE.txt"
        while IFS= read -r -d '' artifact; do
            copy_run_file "$artifact" "$destination/$(basename -- "$artifact")"
        done < <(find "$run" -mindepth 1 -maxdepth 1 -type f \
            \( -name '*.log' -o -name '*.jsonl' -o -name 'summary.txt' -o -name '*sample*.txt' \) -print0)
        if [[ -z "$NEWEST_RUN" && ( -f "$destination/wine.log" || -f "$destination/proton.log" || -n "$(find "$destination" -maxdepth 1 -type f -name 'steam-*.log' -print -quit)" ) ]]; then
            NEWEST_RUN="$destination"
        fi
        (( collected < 10#$MAX_RUNS )) || break
    done < <(/bin/ls -1dt "$DIAGNOSTICS"/run-* 2>/dev/null || true)
fi

PREFIX="$APP_SUPPORT/prefixes/shared"
{
    echo "Prefix exists: $([[ -d "$PREFIX/drive_c" ]] && echo yes || echo no)"
    echo "system.reg exists: $([[ -f "$PREFIX/system.reg" ]] && echo yes || echo no)"
    echo "user.reg exists: $([[ -f "$PREFIX/user.reg" ]] && echo yes || echo no)"
    echo "WoW64 syswow64 exists: $([[ -d "$PREFIX/drive_c/windows/syswow64" ]] && echo yes || echo no)"
    if [[ -f "$PREFIX/system.reg" ]]; then
        /usr/bin/grep -m 1 -E '^#arch=' "$PREFIX/system.reg" || true
    fi
    echo
    echo 'Relevant game and compatibility files found under drive_c:'
    if [[ -d "$PREFIX/drive_c" ]]; then
        /usr/bin/find "$PREFIX/drive_c" -type f \
            \( -iname 'Alicia.exe' -o -iname 'alice.cfg' -o -iname 'cudart32_30_9.dll' \
               -o -iname 'PhysXCore.dll' -o -iname 'PhysXLoader.dll' -o -iname '*.dmp' \) \
            -print 2>/dev/null | /usr/bin/sed "s|$HOME|~|g" || true
    fi
} > "$OUT_DIR/prefix-summary.txt"

if (( INCLUDE_REGISTRY )); then
    mkdir -p -- "$OUT_DIR/prefix/1"
    for name in system.reg user.reg userdef.reg; do
        if [[ -f "$PREFIX/$name" && ! -L "$PREFIX/$name" ]]; then
            cp -- "$PREFIX/$name" "$OUT_DIR/prefix/1/$name"
        fi
    done
fi

if [[ -n "$NEWEST_RUN" ]]; then
    if [[ -f "$ANALYSER" ]] && command -v python3 >/dev/null 2>&1; then
        if python3 "$ANALYSER" "$NEWEST_RUN" --platform darwin \
            --json "$OUT_DIR/analysis.json" > "$OUT_DIR/analysis.txt" 2>&1; then
            printf 'Analysis completed for %s\n' "$(basename -- "$NEWEST_RUN")"
        else
            printf 'Log analysis failed; see analysis.txt\n' >&2
        fi
    else
        printf 'Log analyzer unavailable; diagnostic logs were collected.\n' > "$OUT_DIR/analysis.txt"
    fi
else
    printf 'No Wine or Proton debug log found in the collected diagnostic runs.\n' > "$OUT_DIR/analysis.txt"
fi

if [[ -n "$NEWEST_RUN" && -s "$NEWEST_RUN/TRUNCATED.txt" ]]; then
    printf '\nWARNING: Selected launch logs are incomplete. See runs/*/TRUNCATED.txt for details.\n' >> "$OUT_DIR/analysis.txt"
fi

if [[ -f "$ANALYSER" ]] && command -v python3 >/dev/null 2>&1; then
    if ! python3 "$ANALYSER" --compare-timelines "$OUT_DIR/runs" > "$OUT_DIR/timeline-summary.txt" 2>&1; then
        printf 'No usable launch timeline comparison was generated; see timeline-summary.txt.\n' >&2
    fi
else
    printf 'Timeline comparison unavailable: Python 3 or the analyzer is missing.\n' > "$OUT_DIR/timeline-summary.txt"
fi

cat > "$OUT_DIR/REVIEW_BEFORE_SHARING.txt" <<'TEXT'
Review every file in this archive before sharing it.
The collector does not copy config.json, .env, Keychain data or the full
Wine prefix. Redaction is best effort; tokens, local paths and other private
information may remain in logs, timelines and analysis output.
Large game logs may be truncated or omitted; review TRUNCATED.txt in each run.
Use --full-logs only if complete game logs are needed.
The collector does not copy Wine registry files without --include-registry.
TEXT

if command -v ditto >/dev/null 2>&1; then
    ditto -c -k --keepParent "$OUT_DIR" "$ARCHIVE"
elif command -v zip >/dev/null 2>&1; then
    (cd -- "$(dirname -- "$OUT_DIR")" && zip -q -r "$ARCHIVE" "$(basename -- "$OUT_DIR")")
else
    printf 'Cannot create archive: ditto or zip is required\n' >&2
    exit 1
fi
printf 'Created diagnostic bundle:\n  %s\n\nReview it before sharing.\n' "$ARCHIVE"
