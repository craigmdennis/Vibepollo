<#
.SYNOPSIS
  Build VibepolloSetup.exe and publish it as a GitHub Release pinned to a release commit.

.DESCRIPTION
  Builds with scripts\build-windows.ps1 and creates the tag on GitHub at the exact commit
  that was built, or publishes an installer built elsewhere with -InstallerPath.

    1. Fetches the fork's remote and resolves the release commit, by default the tip of the
       remote default branch.
    2. Refuses unless local HEAD is exactly that commit and tracked files are clean, so the
       installer is built from the tree the tag points at.
    3. Builds VibepolloSetup.exe with the version baked in.
    4. Runs `gh release create v<Version> --target <sha>`, which creates the tag at that commit
       on GitHub and attaches VibepolloSetup-v<Version>.exe in one step.

  -Version is required. Choose a version above the installed one (the fork is based on
  Vibepollo 2.0.6, so start at 2.0.7): the setup program treats a lower version as a downgrade.

.EXAMPLE
  powershell -File scripts\publish-release.ps1 -Version 2.0.7

.EXAMPLE
  # Already built the installer (e.g. on another machine); tag and upload it:
  powershell -File scripts\publish-release.ps1 -Version 2.0.7 -InstallerPath .\VibepolloSetup.exe

.EXAMPLE
  # Release an older commit that is still on master (e.g. a hotfix):
  powershell -File scripts\publish-release.ps1 -Version 2.0.8 -Target 1a2b3c4
#>
#Requires -Version 5.1
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$Version,                         # e.g. 2.0.7; leading 'v' optional
  [string]$Repo = "craigmdennis/Vibepollo",
  [string]$Target = "",                     # commit/ref to tag; default: the remote default branch tip (origin/master)
  [string]$BuildDir = "cmake-build-release",
  [string]$Msys2 = "C:\msys64\msys2_shell.cmd",
  [string]$InstallerPath = "",              # skip the build and publish this .exe instead
  [string]$Notes = "",                      # release notes; default: commits since the previous tag
  [switch]$AllowDirty,                      # build with uncommitted tracked changes (not recommended)
  [switch]$DryRun                           # resolve everything and stage the .exe, but do not create the release
)

# Native commands (git / gh / the MSYS2 cmd wrapper) legitimately write progress to
# stderr; under EAP=Stop PowerShell would turn that into a terminating error. So run
# with Continue and gate explicitly on $LASTEXITCODE / artifact checks instead.
$ErrorActionPreference = "Continue"

$Version = $Version.Trim() -replace '^[vV]', ''

# --- preflight ---
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
  throw "gh not found; install GitHub CLI and run 'gh auth login'."
}
& gh auth status 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { throw "gh is not authenticated; run 'gh auth login' (or set GH_TOKEN)." }

$repoRoot = (& git rev-parse --show-toplevel 2>$null)
if ($LASTEXITCODE -ne 0 -or -not $repoRoot) { throw "Not inside a git repo." }
$repoRoot = $repoRoot.Trim()

# Resolve a relative -InstallerPath before changing location.
if ($InstallerPath) {
  if (-not (Test-Path -LiteralPath $InstallerPath)) { throw "Installer not found at $InstallerPath." }
  $InstallerPath = (Resolve-Path -LiteralPath $InstallerPath).ProviderPath
}

Push-Location $repoRoot
try {
  # The tag goes to whichever remote points at the fork, so its name does not matter
  # (`fork`, `origin`, or anything else). gh is told the repo explicitly below.
  function Resolve-PushRemote {
    foreach ($name in @(& git remote)) {
      $url = (& git remote get-url $name 2>$null)
      if ($url -and ($url -imatch [regex]::Escape($Repo))) { return $name }
    }
    if (@(& git remote) -contains "origin") { return "origin" }
    return $null
  }

  $pushRemote = Resolve-PushRemote
  if (-not $pushRemote) {
    $known = (@(& git remote) -join ", ")
    throw "No git remote points at $Repo (found: $known). Add one, e.g. git remote add fork https://github.com/$Repo.git"
  }

  Write-Host "Fetching tags and branches from '$pushRemote'..."
  & git fetch $pushRemote --tags --prune
  if ($LASTEXITCODE -ne 0) { throw "git fetch $pushRemote failed." }

  # --- resolve the release commit (default: tip of the remote default branch) ---
  $defaultBranch = "master"
  $symref = ((& git ls-remote --symref $pushRemote HEAD 2>$null) -join "`n")
  if ($LASTEXITCODE -eq 0 -and $symref -match 'ref:\s+refs/heads/(\S+)\s+HEAD') {
    $defaultBranch = $Matches[1]
  }

  $defaultTip = (& git rev-parse "$pushRemote/$defaultBranch" 2>$null)
  if ($LASTEXITCODE -ne 0 -or -not $defaultTip) {
    throw "Cannot resolve $pushRemote/$defaultBranch. Is '$defaultBranch' the right default branch?"
  }
  $defaultTip = $defaultTip.Trim()

  if ($Target) {
    $targetSha = (& git rev-parse "$Target^{commit}" 2>$null)
    if ($LASTEXITCODE -ne 0 -or -not $targetSha) { throw "Target '$Target' is not a commit in this repo." }
    $targetSha = $targetSha.Trim()
  }
  else {
    $targetSha = $defaultTip
  }

  & git merge-base --is-ancestor $targetSha $defaultTip
  if ($LASTEXITCODE -ne 0) {
    throw "Target $targetSha is not on $pushRemote/$defaultBranch ($defaultTip). Refusing to tag a commit that is not part of the release branch."
  }

  # --- the build must come from the exact commit being tagged ---
  if (-not $InstallerPath) {
    $headSha = (& git rev-parse HEAD).Trim()
    if ($headSha -ne $targetSha) {
      throw @"
Local HEAD ($headSha) is not the release commit ($targetSha).
Fetch and check out the release commit first:
  git fetch $pushRemote
  git checkout $defaultBranch
  git pull --ff-only
"@
    }

    $trackedChanges = @(& git status --porcelain --untracked-files=no)
    if ($trackedChanges.Count -gt 0 -and -not $AllowDirty) {
      throw "Tracked files have uncommitted changes; the installer would not match $targetSha. Commit/stash them, or pass -AllowDirty."
    }

    $untracked = @(& git status --porcelain --untracked-files=normal | Where-Object { $_ -match '^\?\?' })
    if ($untracked.Count -gt 0) {
      Write-Warning "Untracked files are present (e.g. build dirs); make sure none affect the build."
    }
  }

  # --- version + tag ---
  # Only this fork's releases (v2.x and later). The fork also carries Apollo's v0.x tags.
  $allTags = @(& git tag --list 'v*' --sort=-v:refname | Where-Object { $_ -match '^v(\d+)\.' -and [int]$Matches[1] -ge 2 })

  if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+([-.][0-9A-Za-z.-]+)?$') {
    throw "Version '$Version' is not semver (e.g. 2.0.7 or 2.0.7-rc.1)."
  }
  $tag = "v$Version"

  & git rev-parse --quiet --verify "refs/tags/$tag" | Out-Null
  if ($LASTEXITCODE -eq 0) { throw "Tag $tag already exists locally. Delete it or pass a different -Version." }

  $remoteTag = (& git ls-remote --tags $pushRemote "refs/tags/$tag" 2>$null)
  if ($remoteTag) { throw "Tag $tag already exists on '$pushRemote'. Pass a different -Version." }

  & gh release view $tag --repo $Repo 2>$null | Out-Null
  if ($LASTEXITCODE -eq 0) { throw "Release $tag already exists on $Repo." }

  Write-Host "Release commit: $targetSha ($defaultBranch)"
  Write-Host "Version:        $tag"

  # --- build + package inside MSYS2 UCRT64 (cygpath converts the Windows repo path) ---
  if ($InstallerPath) {
    $source = $InstallerPath
    Write-Warning "Publishing a prebuilt installer. Confirm it was built from $targetSha."
  }
  else {
    $installer = Join-Path $repoRoot "$BuildDir\cpack_artifacts\VibepolloSetup.exe"
    if (Test-Path $installer) { Remove-Item $installer -Force -ErrorAction Stop }  # so a fresh build is detectable

    Write-Host "Building Release $tag..."
    $started = Get-Date
    & (Join-Path $repoRoot "scripts\build-windows.ps1") -Version $Version -BuildDir $BuildDir -Msys2 $Msys2

    if (-not (Test-Path $installer) -or (Get-Item $installer).LastWriteTime -lt $started) {
      throw "The build did not produce a fresh installer at $installer (see output above)."
    }
    $source = $installer
  }

  # --- stage as VibepolloSetup-vX.Y.Z.exe (the name update-vibepollo.ps1 looks for) ---
  $stage = Join-Path ([System.IO.Path]::GetTempPath()) "vibepollo-release-$tag"
  New-Item -ItemType Directory -Path $stage -Force | Out-Null
  $asset = Join-Path $stage "VibepolloSetup-$tag.exe"
  if ([System.IO.Path]::GetFullPath($source) -ne [System.IO.Path]::GetFullPath($asset)) {
    Copy-Item -LiteralPath $source -Destination $asset -Force -ErrorAction Stop
  }
  if (-not (Test-Path -LiteralPath $asset)) { throw "Failed to stage installer at $asset." }
  Write-Host "Installer: $asset ($([math]::Round((Get-Item $asset).Length/1MB,1)) MB)"

  # --- release notes: commits since the previous tag ---
  if ([string]::IsNullOrWhiteSpace($Notes)) {
    $prevTag = $allTags | Where-Object { $_ -ne $tag } | Select-Object -First 1
    if (-not $prevTag) {
      # First release of the fork: list the commits on top of the upstream base release.
      & git rev-parse --quiet --verify "refs/tags/2.0.6" | Out-Null
      if ($LASTEXITCODE -eq 0) { $prevTag = "2.0.6" }
    }
    if ($prevTag) {
      $commits = @(& git log --pretty=format:'- %s (%h)' --no-merges "$prevTag..$targetSha")
      $Notes = "Changes since ${prevTag}:" + "`n`n" + ($commits -join "`n")
    }
    else {
      $Notes = "Vibepollo $tag"
    }
  }

  if ($DryRun) {
    Write-Host "Dry run: would create tag $tag at $targetSha and upload $asset."
    $Notes | ForEach-Object { Write-Host "Release notes:`n$_" }
    return
  }

  # --- tag + release in one call, pinned to the release commit ---
  Write-Host "Creating $tag at $targetSha on $Repo and uploading the installer..."
  & gh release create $tag --repo $Repo --target $targetSha --title "Vibepollo $tag" --notes $Notes $asset
  if ($LASTEXITCODE -ne 0) {
    throw "gh release create failed. Re-run it manually or delete the tag, then retry."
  }

  & git fetch $pushRemote --tags --prune | Out-Null
  $tagSha = (& git rev-parse "refs/tags/$tag" 2>$null)
  if ($tagSha) {
    $tagSha = $tagSha.Trim()
    if ($tagSha -ne $targetSha) { Write-Warning "Tag $tag resolves to $tagSha, expected $targetSha." }
    else { Write-Host "Verified: $tag -> $tagSha" }
  }

  Write-Host "Published release $tag with VibepolloSetup-$tag.exe."
  Write-Host "Release page: https://github.com/$Repo/releases/tag/$tag"
  Write-Host "On each machine run: scripts\update-vibepollo.ps1"
}
finally {
  Pop-Location
}
