# SignPath

Release builds are signed through [SignPath Foundation](https://signpath.org), which signs open source projects for free. The workflow ([.github/workflows/build.yml](../../.github/workflows/build.yml)) signs on version tags once these are set up. Until then it builds unsigned.

## In SignPath

A project with the slug `astrodimmer`, linked to this GitHub repository through the GitHub trusted build system, with a `release-signing` policy and two artifact configurations:

`app`: the app's own exe, both platforms (everything else in the build is signed by Microsoft already):

```xml
<?xml version="1.0" encoding="utf-8"?>
<artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1">
  <zip-file>
    <pe-file path="x64/Release/AstroDimmer/AstroDimmer.exe">
      <authenticode-sign />
    </pe-file>
    <pe-file path="ARM64/Release/AstroDimmer/AstroDimmer.exe">
      <authenticode-sign />
    </pe-file>
  </zip-file>
</artifact-configuration>
```

`installers`: the setup exes Inno Setup makes from the signed app:

```xml
<?xml version="1.0" encoding="utf-8"?>
<artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1">
  <zip-file>
    <pe-file path="AstroDimmer-setup-*.exe" max-matches="unbounded">
      <authenticode-sign />
    </pe-file>
  </zip-file>
</artifact-configuration>
```

## In GitHub

Under Settings › Secrets and variables › Actions:

- Variable `SIGNPATH_ORGANIZATION_ID`: the organization ID from SignPath. Signing is on while it is set.
- Secret `SIGNPATH_API_TOKEN`: the API token of a SignPath CI user that may submit to the project.

## Releasing

Push a `v<version>` tag matching the version in `AstroDimmer.rc`. The run waits for the two signing requests to be approved in SignPath (at most an hour each), then creates a draft GitHub release with the signed setup exes. Publishing the draft submits it to winget ([.github/workflows/winget.yml](../../.github/workflows/winget.yml)).
