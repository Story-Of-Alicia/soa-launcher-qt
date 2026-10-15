#!/usr/bin/env python3
"""Analyze Wine, Alicia and launcher timeline logs without loading logs into memory."""

#  Story of Alicia Launcher
#  SPDX-FileCopyrightText: 2026 Story Of Alicia Contributors
#  SPDX-License-Identifier: GPL-3.0-only
#  Additional terms apply, see ADDITIONAL_TERMS.md.

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter, OrderedDict, defaultdict, deque
from pathlib import Path

EXPECTED_MISSING = {
    "darwin": {
        "winepulse.drv", "winealsa.drv", "wineoss.drv", "wineandroid.drv",
        "wineps.drv", "winex11.drv",
    },
    "linux": {
        "winecoreaudio.drv", "wineandroid.drv", "winemac.drv", "wineps.drv", "wineoss.drv",
    },
}
EXPECTED_MISSING_ANY = {
    "wineps.drv", "winebus.sys", "winehid.sys", "mscoree", "mscoree.dll",
    "openvr_api_dxvk.dll", "wineopenxr.dll", "agperfmon.dll", "physxupdateloader.dll",
}
NTSTATUS = {
    "c0000005": "STATUS_ACCESS_VIOLATION",
    "c0000006": "STATUS_IN_PAGE_ERROR",
    "c000001d": "STATUS_ILLEGAL_INSTRUCTION",
    "c0000025": "STATUS_NONCONTINUABLE_EXCEPTION",
    "c0000029": "STATUS_INVALID_UNWIND_TARGET",
    "c000008c": "STATUS_ARRAY_BOUNDS_EXCEEDED",
    "c000008e": "STATUS_FLOAT_DIVIDE_BY_ZERO",
    "c0000094": "STATUS_INTEGER_DIVIDE_BY_ZERO",
    "c0000096": "STATUS_PRIVILEGED_INSTRUCTION",
    "c00000fd": "STATUS_STACK_OVERFLOW",
    "c0000135": "STATUS_DLL_NOT_FOUND",
    "c0000139": "STATUS_ENTRYPOINT_NOT_FOUND",
    "c0000142": "STATUS_DLL_INIT_FAILED",
    "c0000374": "STATUS_HEAP_CORRUPTION",
    "c0000409": "STATUS_STACK_BUFFER_OVERRUN",
    "80000003": "EXCEPTION_BREAKPOINT",
    "80000004": "EXCEPTION_SINGLE_STEP",
    "80000026": "STATUS_LONGJUMP",
    "80000101": "EXCEPTION_WINE_ASSERTION",
    "40010006": "DBG_PRINTEXCEPTION_C (OutputDebugString)",
    "406d1388": "MS_VC_EXCEPTION (SetThreadName)",
    "e06d7363": "C++ EH exception (throw)",
}
ROUTINE_EXCEPTIONS = {"80000026", "40010006", "406d1388", "e06d7363"}
FATAL_CLASS = {"c0000005", "c00000fd", "c0000374", "c000001d"}
TAIL = r"(?P<cls>trace|warn|err|fixme):(?P<chan>[a-z0-9_]+):(?P<fn>\S+)\s*(?P<msg>.*)$"
LINE_FORMS = (
    re.compile(r"^(?P<ts>\d+\.\d+):(?P<pid>[0-9a-f]+):(?P<tid>[0-9a-f]+):" + TAIL),
    re.compile(r"^(?P<pid>[0-9a-f]+):(?P<tid>[0-9a-f]+):" + TAIL),
    re.compile(r"^(?P<tid>[0-9a-f]+):" + TAIL),
    re.compile(r"^" + TAIL),
)
RE_LOAD_FOUND = re.compile(r'Found L"(?P<path>[^"]+)" for L"(?P<name>[^"]+)" at (?P<base>[0-9A-Fa-f]+)')
RE_LOAD_FAIL = re.compile(r'Failed to load module L"(?P<name>[^"]+)"; status=(?P<status>[0-9a-fA-F]+)')
RE_BUILTIN_MISS = re.compile(r'cannot find builtin library for L"(?P<name>[^"]+)"')
RE_EXC = re.compile(r"code=(?:0x)?(?P<code>[0-9a-fA-F]+)")
RE_SPLICE = re.compile(r"(trace|warn|err|fixme):[a-z0-9_]+:\S+\s")
RE_HOOK = re.compile(r"^\[(?P<ts>[0-9:.]+)\]\s+\[(?P<tag>[^\]]+)\]\s+(?P<msg>.*)$")
RE_HOOK_LOADER = re.compile(r"LoadLibrary[AW]\((?P<name>[^)]*)\)\s*->\s*(?P<result>OK|FAILED)")
TIMELINE_STAGES = (
    ("rendering_started", "d3d9_draw_observed"),
    ("after_present_before_draw", "d3d9_present_observed"),
    ("after_device_before_present", "d3d9_create_device_observed"),
    ("process_observed_graphics_unconfirmed", "alicia_process_observed"),
)


def base_name(path: str) -> str:
    return path.replace("\\", "/").rsplit("/", 1)[-1].lower()


def parse_line(line: str):
    for pattern in LINE_FORMS:
        match = pattern.match(line)
        if match:
            return match
    return None


class Report:
    def __init__(self, platform: str):
        self.platform = platform
        self.modules = OrderedDict()
        self.failed = OrderedDict()
        self.builtin_misses = set()
        self.exceptions = Counter()
        self.exception_first = {}
        self.assertions = []
        self.diagnostics = Counter()
        self.diag_example = {}
        self.last_per_thread = {}
        self.tail_per_thread = defaultdict(lambda: deque(maxlen=6))
        self.channels = Counter()
        self.renderer_hints = Counter()
        self.audio_driver = None
        self.first_ts = None
        self.last_ts = None
        self.total = 0
        self.sources = {}
        self.unparsed = 0
        self.spliced = 0


def scan_wine_log(path: Path, report: Report) -> None:
    before = report.total
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            report.total += 1
            line = raw.rstrip("\r\n")
            if line.startswith("Assertion failed:") or "libc++abi:" in line:
                report.assertions.append({"line": report.total, "text": line[:500]})
                continue
            match = parse_line(line)
            if not match:
                report.unparsed += 1
                if "moltenvk" in line.lower() or "mvk-info" in line.lower():
                    report.renderer_hints["MoltenVK / Vulkan"] += 1
                continue
            info = match.groupdict()
            ts = info.get("ts") or f"#{report.total}"
            pid = info.get("pid") or "----"
            tid = info.get("tid") or "----"
            cls, chan, fn, msg = (info[key] for key in ("cls", "chan", "fn", "msg"))
            if info.get("ts"):
                report.first_ts = report.first_ts or ts
                report.last_ts = ts
            report.channels[f"{cls}:{chan}"] += 1
            splice = RE_SPLICE.search(msg)
            if splice:
                report.spliced += 1
                msg = msg[:splice.start()].rstrip() + "  <<SPLICED>>"
            entry = {"ts": ts, "line": report.total, "text": f"{cls}:{chan}:{fn} {msg}"[:220]}
            thread = f"{pid}:{tid}"
            report.last_per_thread[thread] = entry
            report.tail_per_thread[thread].append(entry)

            found = RE_LOAD_FOUND.search(msg)
            if found:
                name = base_name(found.group("name"))
                path_text = found.group("path")
                report.modules.setdefault(name, {
                    "name": name,
                    "path": path_text,
                    "base": found.group("base"),
                    "first_ts": ts,
                    "line": report.total,
                    "kind": "native" if ":\\users\\" in path_text.lower() or "\\game\\" in path_text.lower() else "system",
                })
            failure = RE_LOAD_FAIL.search(msg)
            if failure:
                name = base_name(failure.group("name"))
                status = failure.group("status").lower()
                report.failed.setdefault(name, {
                    "name": name,
                    "status": status,
                    "status_name": NTSTATUS.get(status, "unknown"),
                    "ts": ts,
                    "line": report.total,
                })
            builtin = RE_BUILTIN_MISS.search(msg)
            if builtin:
                report.builtin_misses.add(base_name(builtin.group("name")))
            if chan == "seh" and "dispatch_exception" in fn:
                exc = RE_EXC.search(msg)
                if exc:
                    code = exc.group("code").lower()
                    report.exceptions[code] += 1
                    report.exception_first.setdefault(code, {"ts": ts, "line": report.total, "tid": tid})
            if cls in ("err", "fixme", "warn"):
                key = f"{cls}:{chan}:{fn}"
                report.diagnostics[key] += 1
                report.diag_example.setdefault(key, msg[:200])
            low = msg.lower()
            if "adapter_vk" in fn or "vulkan" in low:
                report.renderer_hints["Vulkan (wined3d adapter_vk)"] += 1
            if "wgl" in fn or "opengl" in low or "gl_vendor" in low:
                report.renderer_hints["OpenGL (wined3d adapter_gl)"] += 1
            if chan == "coreaudio":
                report.audio_driver = "winecoreaudio"
            elif chan in ("pulse", "alsa", "oss"):
                report.audio_driver = f"wine{chan}"
    report.sources[path.name] = report.total - before


def scan_timeline(path: Path) -> dict:
    names = set()
    count = 0
    finished = None
    duration_ms = 0
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue
            if not isinstance(event, dict):
                continue
            count += 1
            name = str(event.get("event", ""))
            names.add(name)
            try:
                duration_ms = max(duration_ms, int(event.get("elapsed_ms", 0)))
            except (ValueError, TypeError, OverflowError):
                pass
            if name == "session_finished":
                finished = event
    stage = next((label for label, marker in TIMELINE_STAGES if marker in names), "unknown")
    if stage == "unknown" and "wine_wrapper_finished" in names:
        stage = "process_creation"
    details = str(finished.get("details", "")) if finished else ""
    outcome = "active_or_incomplete"
    for token in details.split():
        if token.startswith("outcome="):
            outcome = token.split("=", 1)[1]
            break
    return {
        "events": count,
        "stage": stage,
        "milestones": {marker: marker in names for _, marker in TIMELINE_STAGES},
        "wrapper_finished": "wine_wrapper_finished" in names,
        "session_details": details,
        "outcome": outcome,
        "duration_s": duration_ms / 1000.0,
        "exception_observed": "fatal_exception_observed" in names,
    }


def summarize_timelines(directory: Path) -> int:
    if not directory.is_dir():
        print(f"error: no such directory: {directory}", file=sys.stderr)
        return 2
    paths = [directory / "timeline.jsonl"] if (directory / "timeline.jsonl").is_file() else []
    paths.extend(
        child / "timeline.jsonl"
        for child in directory.iterdir()
        if child.is_dir() and (child / "timeline.jsonl").is_file()
    )

    def run_order(path: Path):
        prefix = path.parent.name.split("-", 1)[0]
        if prefix.isdecimal():
            return 0, int(prefix), ""
        return 1, -path.stat().st_mtime_ns, path.parent.name

    paths.sort(key=run_order)
    if not paths:
        print(f"No launch timelines found in {directory}")
        return 1

    rows = []
    for path in paths:
        timeline = scan_timeline(path)
        milestones = timeline["milestones"]
        rows.append({
            "run": path.parent.name,
            "outcome": timeline["outcome"],
            "duration_s": timeline["duration_s"],
            "process": "yes" if milestones["alicia_process_observed"] else "no",
            "create_device": "yes" if milestones["d3d9_create_device_observed"] else "no",
            "present": "yes" if milestones["d3d9_present_observed"] else "no",
            "draw": "yes" if milestones["d3d9_draw_observed"] else "no",
            "exception": "yes" if timeline["exception_observed"] else "no",
        })

    headers = ("run", "outcome", "duration_s", "process", "create_device", "present", "draw", "exception")

    def display(row: dict, column: str) -> str:
        return f"{row[column]:.1f}" if column == "duration_s" else str(row[column])

    widths = {column: max(len(column), *(len(display(row, column)) for row in rows)) for column in headers}
    print(f"Launch timelines compared: {len(rows)} (newest collected run first)")
    print("Milestone 'no' means not observed, not necessarily a failure.\n")
    print("  ".join(column.ljust(widths[column]) for column in headers))
    print("  ".join("-" * widths[column] for column in headers))
    for row in rows:
        print("  ".join(display(row, column).ljust(widths[column]) for column in headers))
    return 0


def scan_alicia_log(path: Path) -> dict:
    result = {"loaded": [], "failed": [], "tags": Counter(), "last": None, "lines": 0}
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            line = raw.rstrip("\r\n")
            if not line:
                continue
            result["lines"] += 1
            result["last"] = line[:220]
            match = RE_HOOK.match(line)
            if not match:
                continue
            result["tags"][match.group("tag")] += 1
            loader = RE_HOOK_LOADER.search(match.group("msg"))
            if loader:
                name = base_name(loader.group("name"))
                bucket = "loaded" if loader.group("result") == "OK" else "failed"
                if name not in result[bucket]:
                    result[bucket].append(name)
    return result


def expected_missing(name: str, platform: str) -> bool:
    return name in EXPECTED_MISSING_ANY or name in EXPECTED_MISSING.get(platform, set())


def emit(report: Report, alicia: dict | None, timeline: dict | None) -> dict:
    summary = {"platform": report.platform, "lines": report.total, "log_sources": report.sources}
    print("=" * 78)
    print("STORY OF ALICIA — LAUNCH LOG ANALYSIS")
    print("=" * 78)
    print(f"platform assumed : {report.platform}")
    print(f"Wine/Proton lines: {report.total} ({report.unparsed} not in Wine format)")
    for name, count in report.sources.items():
        print(f"  source {name}: {count} lines")
    if report.spliced:
        print(f"CORRUPTED LINES  : {report.spliced}; concurrent Wine output may be interleaved")
    if report.first_ts and report.last_ts:
        span = float(report.last_ts) - float(report.first_ts)
        print(f"log span         : {report.first_ts} -> {report.last_ts} ({span:.1f}s)")
    else:
        print("log span         : no timestamps; ordering is by line number")
    if report.renderer_hints:
        renderer, count = report.renderer_hints.most_common(1)[0]
        print(f"graphics hints   : {renderer} ({count} references; not definitive)")
        summary["renderer"] = renderer
        summary["renderer_hint"] = renderer
    if report.audio_driver:
        print(f"audio hints      : {report.audio_driver} (channel activity; not definitive)")
        summary["audio"] = report.audio_driver
        summary["audio_hint"] = report.audio_driver
    print("\nMODULES LOADED (%d)" % len(report.modules))
    for item in report.modules.values():
        print(f"  {item['first_ts']:>12}  {item['name']:<28} @{item['base']:>8}  {item['kind']}")
    unexpected = [item for name, item in report.failed.items() if not expected_missing(name, report.platform)]
    benign = [item for name, item in report.failed.items() if expected_missing(name, report.platform)]
    print("\nMODULES THAT FAILED TO LOAD")
    for item in unexpected:
        print(f"  investigate: {item['name']} status={item['status']} ({item['status_name']})")
    if not unexpected:
        print("  none unexpected")
    if benign:
        print("  often expected (optional component probes): " + ", ".join(sorted(i["name"] for i in benign)))
    if report.builtin_misses:
        print("  builtin library notices: " + ", ".join(sorted(report.builtin_misses)))
    summary["failed_unexpected"] = [item["name"] for item in unexpected]
    print("\nEXCEPTIONS AND ASSERTIONS")
    for item in report.assertions:
        print(f"  assertion (line {item['line']}): {item['text']}")
    for code, count in report.exceptions.most_common():
        first = report.exception_first[code]
        severity = "routine" if code in ROUTINE_EXCEPTIONS else "investigate"
        print(f"  {severity}: {code} {NTSTATUS.get(code, 'unknown')} x{count} first line {first['line']} tid {first['tid']}")
    if not report.assertions and not report.exceptions:
        print("  none recorded")
    summary["assertions"] = [a["text"] for a in report.assertions]
    summary["exceptions"] = dict(report.exceptions)
    print("\nLAST ACTIVITY PER THREAD (latest recorded message first; inactivity does not mean death)")
    ordered = sorted(report.last_per_thread.items(), key=lambda pair: pair[1]["line"], reverse=True)
    for thread, entry in ordered[:12]:
        print(f"  {entry['ts']:>12}  {thread}  {entry['text'][:120]}")
    if ordered:
        oldest_thread, oldest_entry = ordered[-1]
        print(f"  earliest last-recorded message: {oldest_thread} at {oldest_entry['ts']}")
        summary["first_silent_thread"] = oldest_thread
    if report.diagnostics:
        print("\nerr / fixme / warn (top 20)")
        for key, count in report.diagnostics.most_common(20):
            print(f"  x{count:<6} {key}")
            print(f"        {report.diag_example[key][:140]}")
    if timeline is not None:
        print("\nLAUNCH TIMELINE")
        print(f"  events recorded: {timeline['events']}")
        print(f"  reached stage:   {timeline['stage']}")
        for marker, seen in timeline["milestones"].items():
            print(f"    {'reached' if seen else '   -   '}  {marker}")
        if timeline["session_details"]:
            print(f"  session details: {timeline['session_details'][:150]}")
        summary["stage"] = timeline["stage"]
    if alicia is not None:
        print("\nALICIA HOOK LOG")
        print(f"  lines: {alicia['lines']}")
        if alicia["failed"]:
            print("  LoadLibrary FAILED: " + ", ".join(alicia["failed"]))
        print(f"  last line: {alicia['last']}")
        summary["alicia_last"] = alicia["last"]
    print("\n" + "=" * 78)
    print("VERDICT")
    print("=" * 78)
    verdict = []
    if report.assertions:
        verdict.append("Wine assertion recorded. Investigate the Wine build and surrounding context.")
    fatal = [code for code in report.exceptions if code in FATAL_CLASS]
    if fatal:
        verdict.append("Crash-class exceptions observed: " + ", ".join(
            f"{code} ({NTSTATUS[code]})" for code in fatal) + ". Some can be caught or handled.")
    clean_exit = bool(timeline and
                      "exit_code=0" in timeline["session_details"] and
                      "crashed=0" in timeline["session_details"] and
                      timeline["outcome"] not in ("launch_failed", "crash", "monitoring_lost"))
    if clean_exit:
        verdict.append("The recorded Alicia process exited with code 0 and crashed=0; no crash confirmed.")
    if unexpected:
        label = "Non-fatal module load failures in this completed session: " if clean_exit else "Potentially relevant module load failures: "
        verdict.append(label + ", ".join(item["name"] for item in unexpected))
    if not verdict:
        verdict.append("No clear failure identified in the available debug logs. Check the timeline and launcher log before increasing logging.")
    if report.spliced:
        verdict.append(f"{report.spliced} interleaved Wine log lines were detected; some messages may be truncated.")
    for item in verdict:
        print("  * " + item)
    summary["verdict"] = verdict
    summary["spliced_lines"] = report.spliced
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze a Story of Alicia Wine log.")
    parser.add_argument("wine_log", nargs="?", type=Path, help="wine.log or a diagnostic run directory")
    parser.add_argument("--compare-timelines", type=Path, metavar="DIRECTORY",
                        help="compare launch milestones across collected run directories")
    parser.add_argument("--alicia", type=Path, help="path to alicia.log")
    parser.add_argument("--proton", type=Path, help="path to a Proton steam-*.log")
    parser.add_argument("--timeline", type=Path, help="path to timeline.jsonl")
    parser.add_argument("--platform", choices=["darwin", "linux", "auto"], default="auto")
    parser.add_argument("--json", type=Path, help="write a machine-readable summary")
    args = parser.parse_args()
    if args.compare_timelines is not None:
        if args.wine_log is not None or args.alicia or args.proton or args.timeline or args.json:
            parser.error("--compare-timelines cannot be combined with single-run arguments")
        return summarize_timelines(args.compare_timelines)
    if args.wine_log is None:
        parser.error("wine_log is required unless --compare-timelines is specified")
    wine = args.wine_log
    alicia = args.alicia
    proton = args.proton
    timeline = args.timeline
    if wine.is_dir():
        run = wine
        wine = run / "wine.log"
        alicia = alicia or run / "alicia.log"
        timeline = timeline or run / "timeline.jsonl"
        if proton is None:
            proton_candidates = [
                path for path in [run / "proton.log", *run.glob("steam-*.log")]
                if path.is_file()
            ]
            if proton_candidates:
                proton = max(proton_candidates, key=lambda path: path.stat().st_mtime_ns)
    if not wine.is_file() and not (proton is not None and proton.is_file()):
        print(f"error: no Wine or Proton debug log found: {wine}", file=sys.stderr)
        return 2
    platform = args.platform if args.platform != "auto" else ("darwin" if sys.platform == "darwin" else "linux")
    report = Report(platform)
    if wine.is_file():
        scan_wine_log(wine, report)
    if proton is not None:
        if not proton.is_file():
            print(f"error: no such Proton log: {proton}", file=sys.stderr)
            return 2
        if not wine.is_file() or proton.resolve() != wine.resolve():
            scan_wine_log(proton, report)
    if args.platform == "auto":
        present = set(report.modules)
        if "winemac.drv" in present or "winecoreaudio.drv" in present:
            report.platform = "darwin"
        elif "winex11.drv" in present or "winealsa.drv" in present:
            report.platform = "linux"
    alicia_data = scan_alicia_log(alicia) if alicia and alicia.is_file() else None
    timeline_data = scan_timeline(timeline) if timeline and timeline.is_file() else None
    summary = emit(report, alicia_data, timeline_data)
    if args.json:
        with args.json.open("w", encoding="utf-8") as handle:
            json.dump(summary, handle, indent=2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
