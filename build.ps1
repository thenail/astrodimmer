# Restores NuGet packages, builds everything and runs the tests.
#
#   .\build.ps1                      Debug, x64
#   .\build.ps1 -Configuration Release -Platform ARM64
#   .\build.ps1 -Installer           Release, plus the per-user setup exe
#
# Output: build\<Platform>\<Configuration>\AstroDimmer\AstroDimmer.exe
#         build\installer\AstroDimmer-<version>-<Platform>-setup.exe
#
# Needs Visual Studio 2026 with the C++ desktop workload and the Windows App
# SDK C++ component (Microsoft.VisualStudio.Component.WindowsAppSdkSupport.Cpp).
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Debug',
    [ValidateSet('x64', 'ARM64')] [string] $Platform = 'x64',
    [switch] $SkipTests,
    [switch] $Installer
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

# A Debug build needs the debug CRT, which only developer machines have.
if ($Installer) { $Configuration = 'Release' }

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
