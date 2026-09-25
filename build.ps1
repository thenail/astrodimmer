# Restores NuGet packages, builds everything and runs the tests.
#
#   .\build.ps1                      Debug, x64
#   .\build.ps1 -Configuration Release -Platform ARM64
#   .\build.ps1 -Installer           Release, plus the per-user setup exe
#   .\build.ps1 -Zip                 Release, plus a zip of the app to run without installing
#
# Output: build\<Platform>\<Configuration>\AstroDimmer\AstroDimmer.exe
#         build\installer\AstroDimmer-setup-<version>.exe (x64), AstroDimmer-setup-ARM64-<version>.exe
#         build\zip\AstroDimmer-zip-<version>.zip (x64), AstroDimmer-zip-ARM64-<version>.zip
#
# Needs Visual Studio 2026 with the C++ desktop workload and the Windows App
# SDK C++ component (Microsoft.VisualStudio.Component.WindowsAppSdkSupport.Cpp).
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Debug',
    [ValidateSet('x64', 'ARM64')] [string] $Platform = 'x64',
    [switch] $SkipTests,
    [switch] $Installer,
    [switch] $Zip,
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

# A Debug build needs the debug CRT, which only developer machines have.
if ($Installer -or $Zip) { $Configuration = 'Release' }

if (-not $SkipBuild) {
    # C++ projects restore packages.config with nuget.exe; fetch it on first use.
    $nuget = Join-Path $root 'tools\nuget.exe'
    if (-not (Test-Path $nuget)) {
        New-Item -ItemType Directory -Force (Split-Path $nuget) | Out-Null
        Invoke-WebRequest 'https://dist.nuget.org/win-x86-commandline/latest/nuget.exe' -OutFile $nuget
    }
    & $nuget restore (Join-Path $root 'src\AstroDimmer\packages.config') `
        -PackagesDirectory (Join-Path $root 'packages') -NonInteractive -Verbosity quiet

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $msbuild = & $vswhere -latest -prerelease -requires Microsoft.Component.MSBuild `
        -find 'MSBuild\**\Bin\amd64\MSBuild.exe' | Select-Object -First 1

    & $msbuild (Join-Path $root 'AstroDimmer.slnx') `
        -p:Configuration=$Configuration -p:Platform=$Platform -nologo -v:minimal -m
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    # The tests exercise the Core library, which is the same on every platform;
    # they can only run where the build's platform matches the machine's.
    if (-not $SkipTests -and $Platform -eq 'x64') {
        & (Join-Path $root "build\$Platform\$Configuration\AstroDimmer.Tests.exe")
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
}

if ($Installer) {
    # Inno Setup, pinned, installed portably into tools\ on first use: no admin,
    # nothing registered on the machine.
    $innoVersion = '7.1.0'
    $inno = Join-Path $root 'tools\InnoSetup'
    $iscc = Join-Path $inno 'ISCC.exe'
    if (-not (Test-Path $iscc) -or (Get-Item $iscc).VersionInfo.ProductVersion -notlike "$innoVersion*") {
        $setup = Join-Path $env:TEMP "innosetup-$innoVersion-x64.exe"
        Invoke-WebRequest ("https://github.com/jrsoftware/issrc/releases/download/is-$($innoVersion -replace '\.', '_')/" +
            "innosetup-$innoVersion-x64.exe") -OutFile $setup
        $process = Start-Process $setup -Wait -PassThru -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES',
            '/NORESTART', '/CURRENTUSER', '/PORTABLE=1', "/DIR=`"$inno`""
        Remove-Item $setup
        if ($process.ExitCode -ne 0) { throw "Inno Setup install failed ($($process.ExitCode))" }
    }

    & $iscc /Q "/DPlatform=$Platform" `
        "/DSourceDir=$(Join-Path $root "build\$Platform\$Configuration\AstroDimmer")" `
        "/DOutputDir=$(Join-Path $root 'build\installer')" `
        (Join-Path $root 'installer\AstroDimmer.iss')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if ($Zip) {
    # The same files the installer takes (installer\AstroDimmer.iss), in an
    # AstroDimmer folder, so extracting gives one folder and not 150 files.
    $app = Join-Path $root "build\$Platform\$Configuration\AstroDimmer"
    $version = (Get-Item (Join-Path $app 'AstroDimmer.exe')).VersionInfo.FileVersion
    $name = if ($Platform -eq 'ARM64') { "AstroDimmer-zip-ARM64-$version.zip" } else { "AstroDimmer-zip-$version.zip" }
    $outDir = Join-Path $root 'build\zip'
    New-Item -ItemType Directory -Force $outDir | Out-Null
    $zipPath = Join-Path $outDir $name
    Remove-Item $zipPath -ErrorAction SilentlyContinue

    $skip = 'trace.txt', 'diagnostics.txt', 'ddc-probe.txt', 'display-info.txt'
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::Open($zipPath, 'Create')
    try {
        Get-ChildItem $app -Recurse -File |
            Where-Object { $_.Extension -notin '.pdb', '.lib', '.exp' } |
            Where-Object { -not ($_.DirectoryName -eq $app -and $_.Name -in $skip) } |
            ForEach-Object {
                $entry = 'AstroDimmer/' + $_.FullName.Substring($app.Length + 1).Replace('\', '/')
                [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                    $archive, $_.FullName, $entry, 'Optimal')
            }
    }
    finally { $archive.Dispose() }
}
