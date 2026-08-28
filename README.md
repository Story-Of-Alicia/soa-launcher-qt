# Story of Alicia Launcher

[![License: GPL v3](https://img.shields.io/badge/License-GPL_v3-blue.svg)](LICENSE)
[![Latest release](https://img.shields.io/github/v/release/Story-Of-Alicia/soa-launcher-qt?label=release&color=success)](https://github.com/Story-Of-Alicia/soa-launcher-qt/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/Story-Of-Alicia/soa-launcher-qt/total?color=orange)](https://github.com/Story-Of-Alicia/soa-launcher-qt/releases)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS-lightgrey)](#supported-platforms)
[![Qt 6](https://img.shields.io/badge/Qt-6-41CD52?logo=qt&logoColor=white)](https://www.qt.io/)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)](CMakeLists.txt)
[![Swift](https://img.shields.io/badge/Swift-5-F05138?logo=swift&logoColor=white)](src/core/network/)

The official **Story of Alicia** launcher for Linux and macOS.

![Launcher screenshot](/docs/soa-launcher-screenshot.png)

> **AI development disclosure:** AI was used as a development tool alongside human direction, testing, review, and decision-making

The launcher can:

- Install and update the game
- Verify and repair damaged files
- Manage both supported game versions
- Set up Wine or Proton through UMU on Linux
- Use Game Porting Toolkit on macOS
- Sign in through Discord
- Start the game and collect useful diagnostics when something goes wrong

The launcher is designed to work for regular players without requiring knowledge of Wine, Proton, prefixes, or command-line tools.

> This project is a launcher only. It does not contain the game itself.

## Supported platforms

| Platform            | Status                                             |
|---------------------|----------------------------------------------------|
| Linux x86_64        | AppImage in releases                               |
| Linux ARM64         | On the todo list                                   |
| macOS Apple Silicon | DMG in releases - experimental, requires Rosetta 2 |
| macOS Intel         | DMG in releases - untested                         |
| Windows             | Use the original Windows launcher                  |

> [!WARNING]
> **macOS support is experimental.**
>
> The macOS build has had far less testing than the Linux build. Expect rough
> edges, and please report anything you hit.
>
> The DMG is a universal binary, so the launcher itself runs natively on both
> Apple Silicon and Intel Macs. **However, it has only been tested on Apple
> Silicon. Intel Macs have not been tested at all** the build should run, but
> nobody has confirmed it yet.
>
> Running the game on macOS also depends on Game Porting Toolkit, which is a
> compatibility layer with its own limitations. Some things that work on Linux
> may not work on macOS.

## Downloading the launcher

Download the newest launcher release from either:

- The [official Story of Alicia website](https://storyofalicia.com/)
- The project's [GitHub Releases](https://github.com/Story-Of-Alicia/soa-launcher-qt/releases)

## Runtime requirements

The launcher manages the game and its prefix, but it still needs a compatible Windows runtime installed on your system.

### What you need, at a glance

| Requirement | Linux + Wine | Linux + Proton (UMU) | macOS                                                  |
|---|---|---|--------------------------------------------------------|
| Runtime | Wine | Proton | Apple Silicon: GPTK. Intel: untested, try normal wine? |
| `umu-launcher` | No | Yes | No                                                     |
| `winetricks` | Yes, for setup | Usually no | No                                                     |
| Rosetta 2 | No | No | Apple Silicon only                                     |

Compatible GE/UMU Proton runs Winetricks through UMU. Other Proton builds may require standalone Winetricks.

> [!NOTE]
> **Known issue: `winetricks` is required more often than it needs to be.**
>
> The prerequisite check asks for `winetricks` on every platform and with every
> runtime, including the two cases above where the launcher never actually calls
> it. That is a bug, not intended behavior.
>
> It will be fixed in the next release. The current release cannot be changed.

### Linux

Install `winetricks`, then choose one of these runtime options:

- **Wine** - use your distribution's Wine package.
- **Proton through UMU** - install `umu-launcher` and provide a compatible Proton build. GE-Proton and UMU-Proton builds work best.
- **Custom runtime** - select an existing Wine-compatible runtime in the launcher.

You do not need to install both Wine and Proton. Package names vary between Linux distributions, so use the packages provided by your distribution whenever possible.

### macOS

**1. Install Rosetta 2 (Apple Silicon only) via the terminal.** Game Porting Toolkit runs the game
as x86_64 code, so it will not work without it. The launcher itself is universal
and does not need Rosetta, but the game will not start without it.

```sh
softwareupdate --install-rosetta --agree-to-license
```

**2. Install Homebrew and `winetricks` via the terminal.** macOS does not ship `winetricks`, and
[Homebrew](https://brew.sh/) is the simplest way to get it:

```sh
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
brew install winetricks
```

**3. Install Game Porting Toolkit.** Use the
[distribution maintained by Gcenx](https://github.com/Gcenx/game-porting-toolkit/releases).
This runtime is what actually runs Alicia on macOS, and it is not bundled with
the launcher, so it has to be installed separately. 

Double clicking the tar.xz opens it with Archive Utility which extracts the app. Afterwards drag the application to /Applications for runtime auto-detection in the launcher. The app can potentially be blocked from running due to macOS complaining that it can't verify that it's not harmful; go to privacy settings, scroll down and then select ''run anyway''.

Follow the requirements and installation notes for the specific Game Porting Toolkit release you download.

## Installing the launcher

### Linux

1. Download the AppImage.
2. Allow it to run via the terminal:

   ```sh
   chmod +x <path to Story_Of_Alicia-<version>-x86_64.appimage>
   ```
You can drag the appimage in the terminal, it'll put the full path in automatically.

3. Open it via the terminal:

   ```sh
   <path to Story_Of_Alicia-<version>-x86_64.appimage>
   ```
Or double click the appimage

Linux users can choose between Wine, Proton through UMU, or a custom runtime from the launcher.

### macOS

1. Complete the [runtime requirements](#macos) first - Rosetta 2, `winetricks`, and Game Porting Toolkit.
2. Download the DMG.
3. Open it and move the launcher to Applications.
4. Open the launcher.

## Known issues
If you encounter an error/problem, check [known issues](Known_Issues.md) first if your issue is already known and if it has a solution.

## Documentation

- [`BUILDING.md`](docs/BUILDING.md) - building the launcher from source
- [`CONTRIBUTING.md`](docs/CONTRIBUTING.md) - contributing code, translations, or documentation
- [`CONFIG_REFERENCE.md`](docs/CONFIG_REFERENCE.md) - persisted launcher configuration
- [`PLATFORM_LINUX.md`](docs/PLATFORM_LINUX.md) - Linux behavior and packaging
- [`PLATFORM_MACOS.md`](docs/PLATFORM_MACOS.md) - macOS behavior and packaging

## License and assets

The launcher source code is distributed under the license in [`LICENSE`](LICENSE).

Artwork, logos, fonts, game files, and modified textless versions of existing artwork remain owned by their respective copyright holders and are not automatically covered by the launcher's source-code license.

Licenses for the bundled fonts are in [`assets/fonts/`](assets/fonts). I'm still looking into these licences.

## Acknowledgements

- Thank you to the SOA development team for the assets and the great help.
- Thank you Katsu for the beautiful custom art piece in the launcher and testing work.