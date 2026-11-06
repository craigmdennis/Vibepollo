<#
.SYNOPSIS
  Update this machine to the latest GitHub Release of the Vibepollo fork.

.DESCRIPTION
  Downloads the newest release's VibepolloSetup-v<Version>.exe, checks its SHA-256 against the
  digest GitHub records for the asset, and runs it silently. Skips work when the latest
  release is already installed (tracked in %ProgramData%\VibepolloUpdater\installed-tag.txt).

  Use it for upgrades of an installed Vibepollo. To replace Apollo the first time, run
  scripts\install-local.ps1 instead: it keeps the pairings that the setup program discards.

  Prerequisites on each machine (one-time):
    - GitHub CLI:  winget install GitHub.cli   then   gh auth login

.EXAMPLE
  # Run on demand (asks for administrator rights itself):
  powershell -ExecutionPolicy Bypass -File update-vibepollo.ps1

.EXAMPLE
  # Reinstall the latest release even if it is already installed:
  powershell -ExecutionPolicy Bypass -File update-vibepollo.ps1 -Force
#>
[CmdletBinding()]
param(
  [string]$Repo = "craigmdennis/Vibepollo",
  [string]$AssetPattern = "VibepolloSetup*.exe",
  [string]$ServiceName = "ApolloService",   # Vibepollo keeps Apollo's service name
  [switch]$Force
)
$ErrorActionPreference = "Stop"

# --- self-elevate (setup needs administrator rights for the service and drivers) ---
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)
if (-not $isAdmin) {
  Write-Host "Re-launching elevated..."
  $a = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"", "-Repo", "`"$Repo`"", "-AssetPattern", "`"$AssetPattern`"")
  if ($Force) { $a += "-Force" }
  Start-Process powershell.exe -Verb RunAs -ArgumentList $a
  return
}

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
  throw "GitHub CLI 'gh' not found. Install it (winget install GitHub.cli) and run 'gh auth login'."
}

# --- find the latest release and its installer asset ---
Write-Host "Checking latest release in $Repo ..."
$release = gh api "repos/$Repo/releases/latest" | ConvertFrom-Json
$tag = $release.tag_name
if (-not $tag) { throw "No releases found in $Repo." }

$markerDir  = Join-Path $env:ProgramData "VibepolloUpdater"
$markerFile = Join-Path $markerDir "installed-tag.txt"
$installed  = if (Test-Path $markerFile) { (Get-Content $markerFile -Raw).Trim() } else { "" }

if (($installed -eq $tag) -and -not $Force) {
  Write-Host "Already up to date (installed = $installed). Use -Force to reinstall."
  return
}
Write-Host "Updating: installed='$installed' -> latest='$tag'"

$asset = $release.assets | Where-Object { $_.name -like $AssetPattern } | Select-Object -First 1
if (-not $asset) { throw "Release $tag has no asset matching '$AssetPattern'." }
if ($asset.digest -notmatch '^sha256:([0-9a-fA-F]{64})$') {
  throw "Release $tag records no SHA-256 digest for $($asset.name); refusing to install an unverified file."
}
$expected = $Matches[1].ToLowerInvariant()

# --- download and verify ---
$work = Join-Path $env:TEMP "vibepollo-update-$tag"
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Path $work | Out-Null
gh release download $tag --repo $Repo --pattern $asset.name --dir $work --clobber
$installer = Join-Path $work $asset.name
if (-not (Test-Path $installer)) { throw "Download of $($asset.name) failed." }

$actual = (Get-FileHash -Algorithm SHA256 -Path $installer).Hash.ToLowerInvariant()
if ($actual -ne $expected) {
  Remove-Item $installer -Force
  throw "SHA-256 mismatch for $($asset.name): expected $expected, got $actual. Nothing was installed."
}
Write-Host "Verified SHA-256 $actual"

# --- silent install; setup stops and restarts the service itself ---
Write-Host "Installing $($asset.name) ..."
$p = Start-Process -FilePath $installer -ArgumentList '/quiet' -Wait -PassThru
if ($p.ExitCode -ne 0) { throw "Setup exited with code $($p.ExitCode)." }

$svc = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
if ($svc -and $svc.Status -ne 'Running') {
  Write-Host "Starting $ServiceName ..."
  Start-Service $ServiceName
}

# --- record what was installed ---
New-Item -ItemType Directory -Path $markerDir -Force | Out-Null
Set-Content -Path $markerFile -Value $tag -Encoding ascii
Write-Host "Vibepollo updated to $tag."
