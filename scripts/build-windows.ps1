<#
.SYNOPSIS
  Build this Vibepollo fork on Windows in one command.

.DESCRIPTION
  Runs the Windows build through the MSYS2 UCRT64 toolchain: configure with CMake, compile with
  Ninja (C++ core, both web UIs, and the Playnite plugin), package the WiX MSI, and wrap it in
  VibepolloSetup.exe. Install with VibepolloSetup.exe: it is the program that replaces an
  installed Apollo. scripts\install-local.ps1 runs it and keeps the existing pairings.

  The build downloads the pinned Vibepollo display driver release. It does not bundle the
  Vibepollo gamepad driver, so controllers use ViGEmBus, as Apollo did.

  Prerequisites (one-time, see docs/building.md):
    - MSYS2 at C:\msys64 with the UCRT64 packages that docs/building.md lists, plus
      mingw-w64-ucrt-x86_64-nlohmann-json, -sqlite3, -ninja, and -tools.
    - Official Node.js (nodejs.org or nvm-windows) on PATH. Do not use MSYS2's nodejs package.
    - .NET SDK 10 (dotnet on PATH) for the Playnite plugin and the setup program.
    - WiX Toolset v3.14 (candle.exe and light.exe on PATH, or the WIX environment variable set).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 -Version 2.0.7 -Tests

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 -NoPackage   # build only
#>
[CmdletBinding()]
param(
  # Raise it for each build you install, so the setup program treats the build as an upgrade.
  [string]$Version   = "2.0.6",
  [string]$BuildDir  = "cmake-build-release",
  [string]$Msys2     = "C:\msys64\msys2_shell.cmd",
  [string]$NodeDir   = "",                  # directory containing node.exe; auto-detected when blank
  [string]$BuildType = "Release",
  [switch]$Tests,                           # build and run the unit tests
  [switch]$NoPackage                        # skip the MSI and setup program
)

# Native tools (cmake/ninja through the MSYS2 cmd wrapper) write progress to stderr; under
# EAP=Stop PowerShell would turn that into a terminating error. So run with Continue and gate
# explicitly on a success marker and artifact checks instead.
$ErrorActionPreference = "Continue"

if (-not (Test-Path $Msys2)) { throw "MSYS2 not found at $Msys2. Install it from https://www.msys2.org." }

$repoRoot = git rev-parse --show-toplevel
if ($LASTEXITCODE -ne 0 -or -not $repoRoot) { throw "Not inside a git repo." }
$repoRoot = $repoRoot.Trim()

# --- locate node.exe (the web UI targets run npm through CMake) ---
if ($NodeDir) {
  if (-not (Test-Path (Join-Path $NodeDir "node.exe"))) { throw "node.exe not found in $NodeDir." }
}
else {
  $nodeCmd = Get-Command node -ErrorAction SilentlyContinue
  if ($nodeCmd) {
    $NodeDir = Split-Path $nodeCmd.Source -Parent
  }
  else {
    $NodeDir = "C:\Program Files\nodejs"
    Write-Warning "node.exe is not on PATH; assuming '$NodeDir'. Use -NodeDir if it lives elsewhere."
  }
}
Write-Host "Node directory: $NodeDir"

# --- locate dotnet.exe (Playnite plugin and setup program) ---
$dotnetCmd = Get-Command dotnet -ErrorAction SilentlyContinue
if (-not $dotnetCmd) { throw "dotnet is not on PATH. Install the .NET SDK 10 from https://dotnet.microsoft.com/download." }
$dotnetPath = $dotnetCmd.Source
Write-Host "dotnet: $dotnetPath"

# --- check WiX before a long build ---
if (-not $NoPackage) {
  $candle = Get-Command candle.exe -ErrorAction SilentlyContinue
  if (-not $candle -and $env:WIX) { $candle = Get-Item (Join-Path $env:WIX "bin\candle.exe") -ErrorAction SilentlyContinue }
  if (-not $candle) { throw "WiX Toolset v3 not found. Install it (choco install wixtoolset) or pass -NoPackage." }
  $wixBin = Split-Path $(if ($candle.Source) { $candle.Source } else { $candle.FullName }) -Parent
  Write-Host "WiX: $wixBin"
}

$msysRoot    = Split-Path $Msys2 -Parent
$buildScript = Join-Path $msysRoot "tmp\vibepollo_build.sh"
$okMarker    = Join-Path $msysRoot "tmp\vibepollo_build_ok"

$testsOn  = if ($Tests) { "ON" } else { "OFF" }
$testStep = if ($Tests) { "`n`necho '=== test ==='`nctest --test-dir $BuildDir --output-on-failure" } else { "" }
$pkgStep  = if ($NoPackage) { "" } else { "`n`necho '=== package (MSI) ==='`ncmake --build $BuildDir --target package_msi" }
$wixPath  = if ($NoPackage) { "" } else { ":`$(cygpath -u '$wixBin')" }

# --- build inside MSYS2 UCRT64 (cygpath converts the Windows paths) ---
# The flags follow .github/workflows/ci-windows.yml, without WebRTC, the gamepad driver bundle,
# and the TrueHDR runtime, which need release-only downloads.
$bash = @"
#!/usr/bin/env bash
set -eo pipefail
export BUILD_VERSION='$Version'
export BRANCH='manual'
export PATH="`$(cygpath -u '$NodeDir'):/ucrt64/bin${wixPath}:`$PATH"
cd "`$(cygpath -u '$repoRoot')"
echo "=== configure ($BuildType, version $Version, tests=$testsOn) ==="
cmake -B $BuildDir -G Ninja -S . \
  -DCMAKE_BUILD_TYPE=$BuildType \
  -DBUILD_TESTS=$testsOn \
  -DBUILD_DOCS=OFF \
  -DBUILD_SUNSHINE_VIRTUAL_DISPLAY_DRIVER=OFF \
  -DBUILD_VIRTUALDISPLAY_PROBE=OFF \
  -DBUILD_VIRTUALDISPLAY_TOOLS=OFF \
  -DBUILD_VIRTUALDISPLAY_VULKAN_LAYER=OFF \
  -DSUNSHINE_ASSETS_DIR=assets \
  -DSUNSHINE_DOTNET_EXECUTABLE="`$(cygpath -m '$dotnetPath')" \
  -DSUNSHINE_NPM_EXECUTABLE="`$(cygpath -m '$NodeDir\npm.cmd')" \
  -DSUNSHINE_PUBLISHER_NAME=craigmdennis \
  -DSUNSHINE_PUBLISHER_WEBSITE=https://github.com/craigmdennis/Vibepollo \
  -DSUNSHINE_PUBLISHER_ISSUE_URL=https://github.com/craigmdennis/Vibepollo/issues
echo "=== build ==="
cmake --build $BuildDir --target all$testStep$pkgStep
touch /tmp/vibepollo_build_ok
"@
Set-Content -Path $buildScript -Value ($bash -replace "`r`n","`n") -Encoding ascii -NoNewline -ErrorAction Stop

if (Test-Path $okMarker) { Remove-Item $okMarker -Force -ErrorAction Stop }  # so a fresh run is detectable

Write-Host "Building Vibepollo $Version ($BuildType)..."
& $Msys2 -defterm -here -no-start -ucrt64 -c "bash /tmp/vibepollo_build.sh"

# The MSYS2 cmd wrapper does not reliably propagate the inner exit code, so verify the run
# completed through a success marker the bash script touches at the very end.
if (-not (Test-Path $okMarker)) {
  throw "Build did not complete (see output above)."
}

if ($NoPackage) {
  Write-Host "Build finished (packaging skipped)."
  return
}

$artifacts = Join-Path $repoRoot "$BuildDir\cpack_artifacts"
$msi = Get-ChildItem -Path $artifacts -Filter *.msi -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $msi) { throw "Packaging did not produce an MSI in $artifacts (see output above)." }
Write-Host "MSI: $($msi.FullName)"

# --- wrap the MSI in the setup program, unsigned ---
$ErrorActionPreference = "Stop"
& (Join-Path $repoRoot "packaging\windows\bootstrapper\build_bootstrapper.ps1") `
  -BuildDir (Join-Path $repoRoot $BuildDir) `
  -MsiPath $msi.FullName `
  -OutputName "VibepolloSetup.exe" `
  -DisableSignPath

$setup = Join-Path $artifacts "VibepolloSetup.exe"
if (-not (Test-Path $setup)) { throw "The setup program was not produced at $setup (see output above)." }
Write-Host "Setup: $setup ($([math]::Round((Get-Item $setup).Length/1MB,1)) MB)"
Write-Host "Install it with: powershell -ExecutionPolicy Bypass -File scripts\install-local.ps1"
