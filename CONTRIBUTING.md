# Contributing

> [!Warning]
> This is a work in progress. Some links here won't work yet.

Thank you for wanting to help with the Story of Alicia Launcher.

If you're new to the codebase, start by reading [Launcher Architecture](https://github.com/Story-Of-Alicia/soa-launcher-qt/wiki/Launcher-Architecture) on the wiki. It maps out the modules and walks you through the main flows.

## Build And Test

Read [Building on Linux](https://github.com/Story-Of-Alicia/soa-launcher-qt/wiki/Building-on-Linux) or [Building on macOS](https://github.com/Story-Of-Alicia/soa-launcher-qt/wiki/Building-on-macOS).

## Translations

Want to add or improve a translation? See [Translating the Launcher](https://github.com/Story-Of-Alicia/soa-launcher-qt/wiki/Translating-the-Launcher).

## Rules

These are the things a reviewer will ask you to change.

- **Please keep network code in Swift.** New HTTP requests, downloads and update transport go in `src/network/` and are exposed to C++ through the C headers in `include/network/`. C++ handles state, UI and the results.
- **Layout goes through `soa::ui::layout`** (`include/ui/Layout.hpp`). Don't hard-code coordinates in a widget. The launcher has several window sizes and must work at each one and on high-DPI screens. But exceptions do exist.
- **Shell for platform commands, Python for parsing.** Build and packaging scripts are shell. Log and translation tools are Python. Don't rewrite one into the other.

## Pull Requests

- Keep each pull request to one change or feature, and make sure the tests pass on the platform you changed.
- For UI changes, check every window size and how the assets look.
- For Wine/Proton or macOS changes, say what runtime and Mac you tested on.
- Don't commit build folders, packages, credentials or local config.

## Code Style

- `{` goes on its own line.
- Functions and variables use `snake_case`. Classes, structs and enums use `PascalCase`. Qt overrides like `paintEvent` keep Qt's names.
- Document classes and public functions in the header with `//!`, like [alicia-server](https://github.com/Story-Of-Alicia/alicia-server) does.
- In `.cpp` files, use `//` to explain the *why*: workarounds, timeouts, platform `#if` blocks, and anything you'd forget the reason for.

## Licensing

The launcher is licensed under the GNU General Public License version 3 with the additional terms in [ADDITIONAL_TERMS.md](ADDITIONAL_TERMS.md).

By submitting a contribution for inclusion in this repository, you agree that:

- you have the right to submit the contribution;
- you keep the copyright in your contribution;
- you license your contribution under the GNU General Public License version 3;
- the additional terms in `ADDITIONAL_TERMS.md` apply to your contribution;
- you are not submitting material whose license or ownership prevents it from being distributed under these terms.

Submitting a contribution does not transfer your copyright to the Story of Alicia project or the repository maintainers.