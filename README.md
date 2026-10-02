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

## Supported platforms

| Platform | Status |
|---|---|
| Linux x86_64 | Supported |
| Linux ARM64 | Planned |
| macOS Apple Silicon | Experimental |
| macOS Intel | Experimental / untested |
| Windows | Use the original Windows launcher |

The macOS launcher is distributed as a universal binary supporting both Apple Silicon and Intel. Game compatibility depends on the configured Wine-compatible runtime.

> **Note:** macOS support is experimental.
> If there is not found any new compatible wine runtime that can provide acceptable game performance
> then macOS support will be discontinued in December 2026.

## Download

Download the latest release from:

- [Story of Alicia](https://storyofalicia.com/)
- [GitHub Releases](https://github.com/Story-Of-Alicia/soa-launcher-qt/releases)

## Documentation

Setup, configuration, troubleshooting, and build documentation is available in the [project Wiki](https://github.com/Story-Of-Alicia/soa-launcher-qt/wiki).

- [Installing the Launcher](https://github.com/Story-Of-Alicia/soa-launcher-qt/wiki/Installing-the-Launcher)
- [Linux Setup](https://github.com/Story-Of-Alicia/soa-launcher-qt/wiki/Linux-Setup)
- [macOS Setup](https://github.com/Story-Of-Alicia/soa-launcher-qt/wiki/macOS-Setup)
- [Configuration Reference](https://github.com/Story-Of-Alicia/soa-launcher-qt/wiki/Configuration-Reference)
- [Known Issues](https://github.com/Story-Of-Alicia/soa-launcher-qt/wiki/Known-Issues)
- [Building on Linux](https://github.com/Story-Of-Alicia/soa-launcher-qt/wiki/Building-on-Linux)
- [Building on macOS](https://github.com/Story-Of-Alicia/soa-launcher-qt/wiki/Building-on-macOS)

For contribution guidelines, see [CONTRIBUTING.md](CONTRIBUTING.md).

## License

The Story of Alicia Launcher is licensed under the [GNU General Public License version 3](LICENSE).

Additional attribution terms permitted under GPLv3 Section 7 apply. See [ADDITIONAL_TERMS.md](ADDITIONAL_TERMS.md).

Story of Alicia branding and project assets are subject to [BRANDING.md](BRANDING.md).

## Acknowledgements

- Thank you to the SOA development team for the assets and the great help.
- Thank you Katsu for the beautiful custom art piece in the launcher and testing work.