# Local release guide

GitHub Actions is not used for launcher releases. Releases are built locally
with the provided scripts.

Build Linux from the same committed source. Keep the private update
signing key outside the repository and never include it in a source archive.

## Linux

```bash
export SOA_UPDATE_SIGNING_KEY="/absolute/path/to/soa-update-key.pem"
./packaging/linux/build-release-linux-local.sh
```

This script builds and tests the launcher, performs the portability checks,
creates the AppImage, and generates the signed update metadata.