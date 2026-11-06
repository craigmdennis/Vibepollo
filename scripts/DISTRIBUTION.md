# Distributing the Vibepollo fork to your own machines

Releases go to GitHub Releases on `craigmdennis/Vibepollo`. One Windows machine with the MSYS2 toolchain builds the setup program; every machine installs from the release.

## First install on a machine that runs Apollo

Run `scripts\install-local.ps1` with a setup program from a local build or a release:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\install-local.ps1 -Installer .\VibepolloSetup-v2.0.7.exe
```

The setup program uninstalls Apollo and keeps no settings. The script backs up `C:\Program Files\Apollo\config` to `%ProgramData%\ApolloConfigBackup\<time>` first, then restores the host certificate, `sunshine_state.json`, and `mic_state.json`, so Selene, Moonlight, and Calliope stay paired.

## Cut a release

On the build machine, from the repository root, on `master`, after the change to ship is merged:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\publish-release.ps1 -Version 2.0.7
```

1. The script fetches the fork's remote and resolves the release commit: the tip of the remote default branch, or `-Target <sha>`.
2. It refuses unless local `HEAD` is that commit and tracked files are clean.
3. It builds `VibepolloSetup.exe` with `scripts\build-windows.ps1`.
4. It runs `gh release create v2.0.7 --target <sha>`, which creates the tag at that commit and attaches `VibepolloSetup-v2.0.7.exe`.

`-Version` is required. The fork is based on Vibepollo 2.0.6, so the first release is 2.0.7 or later: the setup program treats a lower version than the installed one as a downgrade. `-DryRun` resolves everything without publishing. `-InstallerPath` publishes a setup program built elsewhere.

Merging a pull request does not tag or release anything.

## Update a machine

```powershell
powershell -ExecutionPolicy Bypass -File scripts\update-vibepollo.ps1
```

The script asks for administrator rights, downloads the latest release's setup program, checks its SHA-256 against the digest GitHub records, runs it with `/quiet`, and records the installed tag in `%ProgramData%\VibepolloUpdater\installed-tag.txt`, so a second run does nothing until the next release. `-Force` reinstalls the latest release.

Vibepollo's own update notice reads the same releases, because `cmake/prep/build_version.cmake` points it at `craigmdennis/Vibepollo`.

### Hands-off updates

```powershell
$action  = New-ScheduledTaskAction -Execute powershell.exe `
  -Argument "-NoProfile -ExecutionPolicy Bypass -File C:\path\to\update-vibepollo.ps1"
$trigger = New-ScheduledTaskTrigger -Daily -At 4am
Register-ScheduledTask -TaskName "Vibepollo Update" -Action $action -Trigger $trigger `
  -RunLevel Highest -User "SYSTEM"
```
