# Contributing to the Story of Alicia Launcher

Thank you for contributing to the Story of Alicia Launcher.

## Licensing

The Story of Alicia Launcher is distributed under the GNU General Public License version 3 together with the additional terms described in `ADDITIONAL_TERMS.md`.

By submitting a contribution for inclusion in this repository, you agree that:

- you have the right to submit the contribution.
- you retain copyright in your contribution.
- you license your contribution under the GNU General Public License version 3.
- the additional terms in `ADDITIONAL_TERMS.md` apply to your contribution
- and you are not submitting material whose license or ownership prevents it from being distributed under these terms.

Submitting a contribution does not transfer ownership of your copyright to the Story of Alicia project or to the repository maintainers.

## Project layout

`src/` contains the launcher implementation.

`include/` contains the matching C++ headers.

`src/core/network/` contains the Swift network layer and its C ABI bridge.

`src/core/wine/` contains Wine, Proton, prefix, runtime, and launch handling.

`src/widgets/` contains the launcher screens and dialogs.

`src/util/` contains shared configuration, layout, logging, language, and UI helpers.

`translations/` contains the Norwegian and Dutch Qt translation catalogs.

`tests/` contains the C++ and Swift tests used by local builds and release builds.

`packaging/linux/` contains the Linux AppImage build and validation tools.

`packaging/macos/` contains the macOS app, DMG, signing, notarization, and verification tools.

## Development rules

### Keep network work in Swift

Network transport belongs in `src/core/network/`.

New HTTP requests, downloads, DNS work, update transport, and other network operations should stay in Swift and be exposed to C++ through the existing bridge. C++ should handle launcher state, UI, platform integration, and the results returned by the network layer.

This separation matters on both Linux and macOS.

### Keep headers and implementations together

If a C++ class gains a member, signal, slot, or helper, update its header and implementation in the same change.

A change that compiles only because a local header is newer than the submitted source is incomplete.

### Use Python and shell for different jobs

The repository intentionally uses both. Shell scripts coordinate platform commands for builds, packaging, signing, and diagnostics. Python handles structured parsing and analysis, such as translation checks and launcher logs.

Using both keeps each task in the simpler tool. Do not rewrite one into the other only for consistency.

### Use the shared layout system

Launcher geometry should go through `util::layout` and the existing layout helpers instead of adding unrelated fixed coordinates in individual widgets.

The launcher has several window size presets and the UI is expected to remain usable at each size and on high DPI displays.

### Comments are welcome

Comments are useful when they explain behavior that is not obvious from the code.

Much of the existing source still needs to be documented by me. This is why many files currently have few or no comments.

Good places for comments include Wine and Proton compatibility workarounds, macOS runtime behavior, signing assumptions, unusual process handling, and decisions that would otherwise look unnecessary to a future contributor.

Comments that only restate the next line of code are usually not useful.

### Keep platform behavior intentional

Linux does not bundle a Wine, Proton, or UMU runtime with the launcher.

macOS does not support Proton in this launcher.

Do not quietly change either rule as part of an unrelated contribution.

## Building on Linux

A normal Linux development build needs Qt 6, CMake, Ninja, Swift, a C++20 compiler, and OpenSSL 3.

The Qt installation needs Core, Gui, Widgets, Network, Concurrent, and LinguistTools.

spdlog is found locally when available. CMake can fetch it when `SOA_ALLOW_FETCHCONTENT` is enabled.

The Windows x86 diagnostic hook needs `i686-w64-mingw32-gcc` and `i686-w64-mingw32-g++`. Developer builds may omit the hook unless `SOA_REQUIRE_ALICIA_LOG_HOOK` is enabled. Release builds require it.

Configure and build with:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build
```

Run the tests with:

```sh
QT_QPA_PLATFORM=offscreen LANG=C.UTF-8 LC_ALL=C.UTF-8 \
ctest --test-dir build --output-on-failure
```

The Linux release path is documented in `PLATFORM_LINUX.md`.

## Building on macOS

The macOS build uses Qt 6, Xcode, Swift, CMake, and the Apple command line tools.

The provided local build script is the easiest development path:

```sh
./packaging/macos/build-local.sh
```

By default it builds the launcher for x86_64 and arm64 with a macOS 12 deployment target. The Qt installation must contain every requested architecture.

The local script produces an unsigned app and runs the test suite. Signing and notarization are separate release steps.

The Windows x86 diagnostic hook is also built for macOS packages, so the MinGW x86 compilers are required by the provided build script.

The macOS release path is documented in `PLATFORM_MACOS.md`.

## Testing changes

Run the test suite for the platform you changed before opening a pull request.

Changes to shared code should be treated as changes to both platforms even if the original bug appeared on only one of them.

For launcher UI changes, test every launcher size preset and check the normal screen, settings, menus, overlays, dialogs, and any state where controls become disabled.

For Wine or Proton changes, include the runtime used during testing and the relevant launcher log when reporting a problem.

For macOS runtime changes, include whether the Mac is Intel or Apple Silicon and whether the selected Wine runtime is x86_64, arm64, or universal.

## Translations

English strings live in the source code and are passed through the launcher translation helpers where appropriate.

The maintained translation catalogs are:

`translations/soa_launcher_nb.ts`

`translations/soa_launcher_nl.ts`

When changing visible text, update the affected translation entries in the same contribution when possible.

Qt Linguist is fine for editing the catalogs. Keep source strings exact and avoid creating duplicate entries for the same context and source text.

A translation change should still build through CMake so the generated Qt translation catalogs are validated as part of the normal build.

## Documentation changes

Documentation should describe behavior that exists in the current launcher or behavior that has already been agreed for the current release path.

Platform details belong in the platform documents rather than being copied into several places.

Commands should be tested before they are added to documentation.

If behavior differs between Linux and macOS, say so directly.

## Pull requests

Keep a pull request focused enough that the reason for each changed file is clear.

Include matching headers when C++ interfaces change.

Include tests when changing parsing, path handling, runtime detection, update validation, process handling, or other logic that can be tested without the full UI.

Include screenshots for visual changes when they help reviewers compare states or launcher sizes.

Do not commit build directories, generated packages, private signing material, account credentials, or local machine configuration.

The Ed25519 public update key is public project data. The matching private key is not.

## Releases

Contributors do not need release credentials for normal development.

Current public launcher builds for both Linux and macOS are distributed through the official Story of Alicia website at https://storyofalicia.com and through the project GitHub Releases.

Linux releases use the AppImage package. macOS releases use the DMG package.

Signing, notarization, final package verification, and publication are maintainer tasks.
