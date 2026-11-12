<#
.SYNOPSIS
Publishes a GitHub release for each game, and for the kit, whose release files changed since its last release.

.DESCRIPTION
For every game under games: builds it stamped "unversioned" in place of the commit, so the DLLs depend only on the
code; a game with no release.md yet stops there. Otherwise it takes the release's fingerprint (layout.ps1). A game
whose newest release carries the same fingerprint in its notes is left alone. Otherwise it is built again stamped with
the checked-out commit, packed by package.ps1 into releases\ankh-plugins-<game>.zip and, with -publish,
released as <game>-<yyyy.MM.dd>-<commit> with its release.md as title and notes.

The kit, the download for a game with no release of its own, goes the same way: every loader and central plugin of
x86 and of x64, fingerprinted as the two zips' contents, packed into releases\ankh-plugins-x86.zip and
releases\ankh-plugins-x64.zip and released as plugin-kit-<yyyy.MM.dd>-<commit> with kit\release.md.

-test runs the Python client's tests with each game's build and, first, builds and tests the fixture games
(fixtureRoot\games), which run the loader's own tests and are never released. Without -publish it only reports what
would be released. gh must be logged in.
#>
param(
  [switch]$test,
  [switch]$publish
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "layout.ps1") -rootFolder $PSScriptRoot
$repository = "AmmolitePreservation/ankh-plugins"
$unversioned = "unversioned"
$fingerprintPattern = '<!-- fingerprint ([0-9a-f]{64}) -->'
$kitTag = "plugin-kit"
$kitArchitectures = @("x86", "x64")
$releasesFolder = Join-Path $PSScriptRoot "releases"

function Write-ReleaseLog {
  param([string]$message)
  Write-Host "[release] $message"
}

function Invoke-Script {
  param([string]$scriptName, [string[]]$arguments)
  & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot $scriptName) @arguments
  if ($LASTEXITCODE -ne 0) {
    throw "$scriptName $($arguments -join ' ') failed (exit code $LASTEXITCODE)"
  }
}

function Get-PublishedFingerprint {
  # The fingerprint in the notes of the newest release whose tag starts with <tagName>-; none when there is no release.
  param([string]$tagName)
  $releases = & gh release list -R $repository --limit 1000 --json tagName,createdAt | ConvertFrom-Json
  if ($LASTEXITCODE -ne 0) {
    throw "gh release list failed"
  }
  $newest = $releases | Where-Object { $_.tagName.StartsWith("$tagName-") } | Sort-Object createdAt -Descending | Select-Object -First 1
  if (-not $newest) {
    return $null
  }
  $body = & gh release view $newest.tagName -R $repository --json body -q .body
  if ($LASTEXITCODE -ne 0) {
    throw "gh release view $($newest.tagName) failed"
  }
  if (($body -join "`n") -match $fingerprintPattern) {
    return $Matches[1]
  }
  return "none in $($newest.tagName)"
}

function New-ReleaseZip {
  param([string]$name, [string[]]$packageArguments)
  New-Item -ItemType Directory -Path $releasesFolder -Force | Out-Null
  $zipPath = Join-Path $releasesFolder "ankh-plugins-$name.zip"
  if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath
  }
  # Out-Host: the script's lines would otherwise be this function's value beside the path.
  Invoke-Script "package.ps1" ($packageArguments + @("-outputPath", $zipPath)) | Out-Host
  return $zipPath
}

function Publish-Release {
  # Releases the zips as <tagName>-<yyyy.MM.dd>-<commit>, with the notes file's # title and text and the fingerprint.
  param([string]$tagName, [string]$notesSourcePath, [string[]]$zipPaths, [string]$fingerprint)
  $commit = (& git -C $PSScriptRoot rev-parse --short HEAD)
  if ($LASTEXITCODE -ne 0) {
    throw "git rev-parse failed"
  }
  $notes = @(Get-Content -Encoding UTF8 -LiteralPath $notesSourcePath)
  if ($notes[0] -notmatch '^# (.+)$') {
    throw "$notesSourcePath must start with a # title"
  }
  $stamp = (Get-Date).ToUniversalTime().ToString("yyyy.MM.dd")
  $title = "$($Matches[1]) $stamp ($commit)"
  $notesPath = Join-Path $releasesFolder "$tagName-notes.md"
  $body = (($notes | Select-Object -Skip 1) + @("", "<!-- fingerprint $fingerprint -->")) -join "`n"
  [System.IO.File]::WriteAllText($notesPath, $body, (New-Object System.Text.UTF8Encoding $false))
  $fullCommit = (& git -C $PSScriptRoot rev-parse HEAD)
  & gh release create "$tagName-$stamp-$commit" @zipPaths -R $repository --target $fullCommit --title $title --notes-file $notesPath
  if ($LASTEXITCODE -ne 0) {
    throw "gh release create failed for $tagName"
  }
  Write-ReleaseLog "released $tagName-$stamp-$commit"
}

function Get-KitFingerprint {
  # The fingerprint of both architectures' kits as their zips lay them out.
  $stagingRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("ankhPluginsKit" + [System.Guid]::NewGuid().ToString("N"))
  try {
    foreach ($architecture in $kitArchitectures) {
      $kitFolder = Join-Path $stagingRoot $architecture
      New-Item -ItemType Directory -Path $kitFolder -Force | Out-Null
      Copy-KitFiles $architecture $kitFolder | Out-Null
    }
    return Get-FolderFingerprint $stagingRoot
  } finally {
    if (Test-Path -LiteralPath $stagingRoot) {
      Remove-Item -LiteralPath $stagingRoot -Recurse -Force
    }
  }
}

if ($test) {
  $fixtureRoot = Join-Path $PSScriptRoot "fixtureRoot"
  foreach ($game in (Get-ChildItem -Directory -LiteralPath (Join-Path $fixtureRoot "games")).Name) {
    Invoke-Script "build.ps1" @("-root", $fixtureRoot, "-game", $game, "-buildCommit", $unversioned, "-test")
  }
}

# Everything is fingerprinted on the unversioned build first, then what changed is built with the commit stamped in:
# a build tree compiles all of its code again when the stamp changes.
$changedGames = @{}
foreach ($game in (Get-ChildItem -Directory -LiteralPath (Join-Path $PSScriptRoot "games")).Name) {
  $arguments = @("-game", $game, "-buildCommit", $unversioned)
  if ($test) {
    $arguments += "-test"
  }
  Invoke-Script "build.ps1" $arguments
  if (-not (Test-Path -LiteralPath (Join-Path (Get-GameFolder $game) "release.md"))) {
    Write-ReleaseLog "$game has no release.md: built, not released"
    continue
  }
  $fingerprint = Get-ReleaseFingerprint $game
  $published = Get-PublishedFingerprint $game
  if ($published -eq $fingerprint) {
    Write-ReleaseLog "$game unchanged (fingerprint $fingerprint)"
    continue
  }
  Write-ReleaseLog "$game changed: fingerprint $fingerprint, last release $published"
  $changedGames[$game] = $fingerprint
}
foreach ($architecture in $kitArchitectures) {
  Invoke-Script "build.ps1" @("-architecture", $architecture, "-buildCommit", $unversioned)
}
$kitFingerprint = Get-KitFingerprint
$publishedKit = Get-PublishedFingerprint $kitTag
$isKitChanged = $publishedKit -ne $kitFingerprint
if ($isKitChanged) {
  Write-ReleaseLog "kit changed: fingerprint $kitFingerprint, last release $publishedKit"
} else {
  Write-ReleaseLog "kit unchanged (fingerprint $kitFingerprint)"
}

foreach ($game in ($changedGames.Keys | Sort-Object)) {
  Invoke-Script "build.ps1" @("-game", $game)
  $zipPath = New-ReleaseZip $game @("-game", $game)
  if ($publish) {
    Publish-Release $game (Join-Path (Get-GameFolder $game) "release.md") @($zipPath) $changedGames[$game]
  }
}
if ($isKitChanged) {
  $kitZipPaths = @()
  foreach ($architecture in $kitArchitectures) {
    Invoke-Script "build.ps1" @("-architecture", $architecture)
    $kitZipPaths += New-ReleaseZip $architecture @("-architecture", $architecture)
  }
  if ($publish) {
    Publish-Release $kitTag (Join-Path (Join-Path $PSScriptRoot "kit") "release.md") $kitZipPaths $kitFingerprint
  }
}
