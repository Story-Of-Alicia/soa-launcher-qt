# Local release guide

GitHub Actions is not used for launcher releases. Releases are built locally
with the provided scripts.

Build Linux and macOS from the same committed source. Keep the private update
signing key outside the repository and never include it in a source archive.

## Linux

```bash
export SOA_UPDATE_SIGNING_KEY="/absolute/path/to/soa-update-key.pem"
./packaging/linux/build-release-linux-local.sh
```

This script builds and tests the launcher, performs the portability checks,
creates the AppImage, and generates the signed update metadata.

## macOS

Configure the Apple signing and notarization credentials described in
`macos/SIGNING.md`, then run:

```bash
export SOA_UPDATE_SIGNING_KEY="/absolute/path/to/soa-update-key.pem"
./packaging/macos/build-release-macos-local.sh
```

This script builds and tests the launcher, signs and notarizes the app, creates
and verifies the DMG, and generates the signed update metadata.
