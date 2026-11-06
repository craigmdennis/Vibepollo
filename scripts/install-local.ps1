<#
.SYNOPSIS
  Install a local build of this Vibepollo fork and keep the existing pairings.

.DESCRIPTION
  Runs VibepolloSetup.exe, which replaces an installed Apollo or upgrades Vibepollo. When it
  replaces Apollo, the setup program uninstalls Apollo first and carries over no settings.
  So this script copies the config folder to %ProgramData%\ApolloConfigBackup\<time> first.
  After setup finishes, it puts back the files that keep paired devices working:

    credentials\cacert.pem and credentials\cakey.pem   the host identity the clients trust
    sunshine_state.json                                paired clients and the web UI login
    mic_state.json                                     paired Calliope microphones

  Settings and the app list stay in the backup folder. Copy them back by hand if needed.
  The script asks for administrator rights itself.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\install-local.ps1

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\install-local.ps1 -Installer D:\VibepolloSetup.exe
#>
[CmdletBinding()]
param(
  [string]$Installer  = "",                        # defaults to the build output of build-windows.ps1
  [string]$InstallDir = "C:\Program Files\Apollo", # Vibepollo and Apollo both install here
  [switch]$NoRestore                               # keep whatever setup leaves in the config folder
)

$ErrorActionPreference = "Stop"

if (-not $Installer) {
  $repoRoot = (git rev-parse --show-toplevel).Trim()
  $Installer = Join-Path $repoRoot "cmake-build-release\cpack_artifacts\VibepolloSetup.exe"
}
$Installer = (Resolve-Path $Installer).Path

# --- self-elevate: setup, the service, and Program Files need administrator rights ---
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  $argList = @("-ExecutionPolicy", "Bypass", "-NoExit", "-File", "`"$PSCommandPath`"", "-Installer", "`"$Installer`"", "-InstallDir", "`"$InstallDir`"")
  if ($NoRestore) { $argList += "-NoRestore" }
  Start-Process powershell -Verb RunAs -ArgumentList $argList
  return
}

$config = Join-Path $InstallDir "config"
$keep = @("credentials\cacert.pem", "credentials\cakey.pem", "sunshine_state.json", "mic_state.json")

# --- back up the whole config folder ---
$backup = $null
if (Test-Path $config) {
  $backup = Join-Path $env:ProgramData ("ApolloConfigBackup\" + (Get-Date -Format "yyyyMMdd-HHmmss"))
  New-Item -ItemType Directory -Path (Split-Path $backup) -Force | Out-Null
  Copy-Item -Path $config -Destination $backup -Recurse
  Write-Host "Backed up $config to $backup"
}
else {
  Write-Host "No config folder at $config; nothing to back up."
}

# --- run setup and wait for it ---
Write-Host "Running $Installer"
$setup = Start-Process -FilePath $Installer -Wait -PassThru
if ($setup.ExitCode -ne 0) {
  throw "Setup exited with code $($setup.ExitCode). The backup is at $backup."
}

if ($NoRestore -or -not $backup) {
  Write-Host "Done."
  return
}

# --- put back the host identity and the pairings ---
$restore = @($keep | Where-Object { Test-Path (Join-Path $backup $_) })
if (-not $restore) {
  Write-Host "The backup has none of the pairing files. Done."
  return
}

Write-Host "Stopping ApolloService to restore the pairings"
Stop-Service -Name ApolloService -ErrorAction SilentlyContinue
# The service stops its sunshine process; wait so no file is still open.
for ($i = 0; $i -lt 20 -and (Get-Process -Name sunshine -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 500 }

foreach ($relative in $restore) {
  $target = Join-Path $config $relative
  New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
  Copy-Item -Path (Join-Path $backup $relative) -Destination $target -Force
  Write-Host "Restored $relative"
}

Start-Service -Name ApolloService
Write-Host "Done. Paired devices and microphones keep working. Settings and apps are in $backup."
