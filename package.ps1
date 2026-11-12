<#
.SYNOPSIS
Builds a release zip: a game's, to extract straight into the game's install folder, or the kit of an architecture.

.DESCRIPTION
A game's zip holds what layout.ps1 lays out for the game, at the executable's place in its install folder: the loader DLL
and an ankh-plugins folder with every plugin the game's plugins.txt names, the licenses and, in its game folder, the
game.cfg and plugins.txt the loader reads. It needs build.ps1 -game run first. It carries no settings files: each plugin
writes its own with the shipped defaults the first time the game starts, and a later release only adds the settings it
introduces, so an update never resets a player's.

A kit's zip is for a game with no release of its own: every loader and every central plugin of the architecture, the
licenses, a README, and a game.cfg and plugins.txt to fill in (layout.ps1, Copy-KitFiles). It needs
build.ps1 -architecture run first.

.PARAMETER game
A folder under this repository's games.

.PARAMETER architecture
x64 or x86, for the kit in place of a game.

.PARAMETER outputPath
The zip to write. It must not exist yet.
#>
param(
  [string]$game,
  [ValidateSet("x64", "x86")][string]$architecture,
  [Parameter(Mandatory = $true)][string]$outputPath
)

$ErrorActionPreference = "Stop"
if ([bool]$game -eq [bool]$architecture) {
  throw "Name a game with -game, or an architecture with -architecture for its kit"
}
. (Join-Path $PSScriptRoot "layout.ps1") -rootFolder $PSScriptRoot

function Write-PackageLog {
  param([string]$message)
  Write-Host "[package] $message"
}

function Write-Zip {
  param([string]$sourceFolder)
  Add-Type -AssemblyName System.IO.Compression
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  # Entries are added one by one: Windows PowerShell's CreateFromDirectory names them with backslashes, which the zip
  # format does not allow and some extractors keep as part of the file name.
  $archive = [System.IO.Compression.ZipFile]::Open($outputPath, [System.IO.Compression.ZipArchiveMode]::Create)
  try {
    foreach ($entry in Get-ChildItem -LiteralPath $sourceFolder -Recurse -Force -File) {
      $entryName = $entry.FullName.Substring($sourceFolder.Length + 1).Replace("\", "/")
      [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $entry.FullName, $entryName,
        [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
  } finally {
    $archive.Dispose()
  }
}

$outputPath = [System.IO.Path]::GetFullPath($outputPath)
if (Test-Path -LiteralPath $outputPath) {
  throw "$outputPath already exists"
}
$stagingRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("ankhPluginsPackage" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $stagingRoot -Force | Out-Null
try {
  if ($game) {
    $clientFolder = $stagingRoot
    $executableFolder = Split-Path (Get-GameExecutable $game)
    if ($executableFolder) {
      $clientFolder = Join-Path $stagingRoot $executableFolder
    }
    Write-PackageLog ("added " + (Copy-ReleaseFiles $game $clientFolder))
  } else {
    Write-PackageLog ("added " + (Copy-KitFiles $architecture $stagingRoot))
  }
  Write-Zip $stagingRoot
  Write-PackageLog ("wrote {0}: {1:N0} bytes" -f $outputPath, (Get-Item -LiteralPath $outputPath).Length)
} finally {
  Remove-Item -LiteralPath $stagingRoot -Recurse -Force
}
