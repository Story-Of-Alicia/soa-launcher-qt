# Alicia log hook

Cloned from https://github.com/SergeantSerk/log-hook by SergeantSerk, then
extended so it builds and runs under Wine on macOS and Linux.

## Status

Modified with the help of AI. It has been lightly tested enough to get the
game running with logging and sound on macOS, but the code has not been
reviewed thoroughly. Treat it with caution.

## What was added

- cross-compiles for Windows x86 from a macOS or Linux host via MinGW
- a `LoadLibraryW` injection path that works under Wine
- captures the game's `printf`/`vsnprintf` output to a launcher-owned log
- Direct3D 9 instrumentation: `Direct3DCreate9` IAT patch, plus per-object and
  per-device COM vtable hooks for `CreateDevice`, `Reset` and `Present`
- adapter mode enumeration hooks and windowed-mode rewriting for macOS
- module load/unload tracing
- a DirectSound backend that plays audio outside Wine (macOS)

## The macOS audio backend

On macOS the game had no working sound. Wine's own audio path crashed the game
outright under Game Porting Toolkit, so there was nothing to tune, it had to be
avoided rather than configured.

Instead of letting Wine talk to CoreAudio, the hook implements DirectSound
itself, collects the audio the game writes, and sends it over a local socket to
`soa-audio-host`, a small native macOS program that does the playback. The game
gets working sound and Wine never touches the audio hardware.

Files: `pipe_dsound.cpp` (the DirectSound implementation), `soa_audio_bridge.hpp`
(buffering and format conversion), `soa-audio-host.c` (the native player),
`null_dsound.cpp` (a silent fallback).

Set `SOA_AUDIO_PIPE=1` to use it; the launcher does this on macOS. `SOA_AUDIO_NULL_BACKEND=1`
forces silence instead. Neither applies on Linux, where Wine's own audio works.

## Licensing

No licence is stated upstream. Included with thanks and the origin recorded. If
the author would prefer different treatment I will follow whatever they ask.

`minhook/` is MinHook by Tsuda Kageyu, BSD 2-Clause - see its licence file.

## Building

    ./build-x86.sh <output-dir> <mingw-gcc> <mingw-g++>
