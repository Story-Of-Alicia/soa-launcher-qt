# Diagnostics

Developer tools for investigating failed Alicia launches on Linux and macOS.

## Tools

- `wine-debug-profiles.sh` defines the `WINEDEBUG` profiles used by the launcher and scripts.
- `soa-log-analyze.py` analyzes Wine/Proton logs, `alicia.log`, and launch timelines. It can also compare multiple runs.
- `collect-linux.sh` and `collect-macos.sh` bundle recent diagnostic runs, system details, and analyzer reports.

## Debug profiles

```bash
source tools/diagnostics/wine-debug-profiles.sh
export WINEDEBUG="$(soa_wine_debug verbose)"
```

Available profiles: `off`, `normal`, `verbose`, `audio`, `forensic`, and `relay`. The last two can create very large logs and alter launch timing.

## Collect

Enable **Diagnostic Mode** in the launcher and reproduce the issue first. Then, from the repository root:

```bash
bash tools/diagnostics/collect-linux.sh
bash tools/diagnostics/collect-macos.sh
```

Run the collector for your platform. Archives contain separate launch runs, `analysis.txt`/`analysis.json` when available, and `timeline-summary.txt`. Proton runs can include `steam-*.log` alongside `wine.log` and the injector's `alicia.log`.

Use `--full-logs` to disable game-log size limits or `--include-registry` to collect Wine registry files. Collection redacts recognizable credentials on a best-effort basis, **review archives before sharing**.

## Analyze manually

```bash
python3 tools/diagnostics/soa-log-analyze.py /path/to/run-directory
python3 tools/diagnostics/soa-log-analyze.py --compare-timelines /path/to/runs
```

The analyzer streams large logs. Wine warnings, optional module failures, and missing timeline milestones do not necessarily indicate a crash.
