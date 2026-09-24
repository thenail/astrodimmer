# winget

The manifests for [winget-pkgs](https://github.com/microsoft/winget-pkgs), laid out as they are there.

To publish a version: fork `microsoft/winget-pkgs`, copy `manifests/k/KristianNagel/AstroDimmer/<version>` into the same path in the fork, and open a pull request. The bots validate it and install it in a sandbox; a moderator then merges it.

Check locally first:

```powershell
winget validate --manifest packaging\winget\manifests\k\KristianNagel\AstroDimmer\1.4.0.0
```

For later releases, [wingetcreate](https://github.com/microsoft/winget-create) updates the version, URLs and hashes and opens the pull request:

```powershell
wingetcreate update KristianNagel.AstroDimmer --version <version> --urls `
  https://github.com/thenail/astrodimmer/releases/download/v<tag>/AstroDimmer-setup-<version>.exe `
  https://github.com/thenail/astrodimmer/releases/download/v<tag>/AstroDimmer-setup-ARM64-<version>.exe --submit
```
