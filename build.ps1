<#
.SYNOPSIS
  Builds ankh-plugins for one game: DLLs for its architecture (game.cfg) from Visual Studio's MSVC, CMake
  and Ninja.
.DESCRIPTION
  -game names a folder under the root's games. The root is this repository, or with -root another folder that holds
  games and, optionally, plugins of its own, which are built beside this repository's. The root's build\<architecture>
  is one build for every game of that architecture, so what the games share is compiled once however many of them are
  built; this game's files land in its games\<game>\bin: the loader (named for the system DLL game.cfg's loader= has
  it stand in for), the plugin DLLs, which carry their settings files, and in game\ the game.cfg and plugins.txt the
  loader reads in an install. -test runs the Python client's tests in the
  repository's .venv and, for a game that builds its own executable (a fixture), runs the loader and a test plugin in
  it. -buildCommit stamps the DLLs with that text in place of the root's checked-out commit. -gameFolder installs the
  build into the game's install folder as a release lays it out (layout.ps1), leaving the folder's settings files to
  the plugins; -diagnostics adds the diagnostic plugins to it.
.EXAMPLE
  .\build.ps1 -game soma -test -diagnostics -gameFolder "<SOMA install folder>"
.EXAMPLE
  .\build.ps1 -root fixtureRoot -game fixture-x64 -test
#>
param(
  [Parameter(Mandatory = $true)][string]$game,
  [string]$root,
  [switch]$test,
  [string]$gameFolder,
  [switch]$diagnostics,
  [string]$buildCommit
)
if ($diagnostics -and -not $gameFolder) {
  throw "-diagnostics adds the diagnostic plugins to the -gameFolder install"
}
$ErrorActionPreference = "Stop"
# Not a parameter default: run with powershell -File, $PSScriptRoot is still empty when the defaults are evaluated.
if (-not $root) {
  $root = $PSScriptRoot
}
$root = (Resolve-Path -LiteralPath $root).Path
. (Join-Path $PSScriptRoot "layout.ps1") -rootFolder $root
$buildDirectory = Get-BuildFolder $game
$gameBuildDirectory = Get-GameBuildFolder $game
$binaryDirectory = Get-GameBinaryFolder $game
$loader = Get-GameLoader $game
$requiredExports = @{ "$loader.dll" = @(Get-Content -LiteralPath (Join-Path (Get-LoaderProxyFolder) "$loader.txt")) }
$launcher = @(Get-GameLauncher $game)
if ($launcher.Count -eq 1) {
  $requiredExports["msimg32.dll"] = @("AlphaBlend")
}
$architecture = Get-GameArchitecture $game
# Read here so that a game.cfg the build could not use stops it with the reason.
Get-GameCompanions $game | Out-Null
Get-GameTimeDateStamp $game | Out-Null
foreach ($plugin in @(Get-ShippedPlugins $game) + @(Get-DiagnosticPlugins)) {
  $requiredExports[$plugin] = @("Load", "PluginInterfaceVersion")
}

function Write-BuildLog {
  param([string]$message)
  Write-Host "[build] $message"
}

function Import-VisualStudioEnvironment {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path $vswhere)) {
    throw "Visual Studio is not installed (no vswhere.exe)"
  }
  $installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if (-not $installation) {
    throw "No Visual Studio installation has the C++ x86/x64 build tools"
  }
  $vcvarsName = @{ "x64" = "vcvars64.bat"; "x86" = "vcvars32.bat" }[$architecture]
  $assembler = @{ "x64" = "ml64.exe"; "x86" = "ml.exe" }[$architecture]
  $vcvars = Join-Path $installation "VC\Auxiliary\Build\$vcvarsName"
  # vcvars runs vswhere.exe itself and expects to find it on PATH.
  $env:PATH = (Split-Path $vswhere) + ";" + $env:PATH
  $environmentLines = & cmd.exe /c "`"$vcvars`" >nul && set"
  if ($LASTEXITCODE -ne 0) {
    throw "$vcvars failed"
  }
  foreach ($line in $environmentLines) {
    $separator = $line.IndexOf("=")
    if ($separator -gt 0) {
      Set-Item -Path ("env:" + $line.Substring(0, $separator)) -Value $line.Substring($separator + 1)
    }
  }
  foreach ($program in @("cl.exe", $assembler, "cmake.exe", "ninja.exe", "dumpbin.exe")) {
    if (-not (Get-Command $program -ErrorAction SilentlyContinue)) {
      throw "$program is not on the Visual Studio $architecture environment's PATH"
    }
  }
  Write-BuildLog "Visual Studio $architecture environment from $installation"
}

function Get-BuildCommit {
  $commit = & git -C $root rev-parse --short HEAD
  if ($LASTEXITCODE -ne 0) {
    throw "git rev-parse failed"
  }
  $changes = & git -C $root status --porcelain
  if ($changes) {
    $commit = "$commit-dirty"
  }
  return $commit
}

function Invoke-NativeStep {
  param([string]$description, [scriptblock]$step)
  & $step
  if ($LASTEXITCODE -ne 0) {
    throw "$description failed (exit code $LASTEXITCODE)"
  }
}

function Test-LibraryExport {
  param([string]$fileName, [string[]]$exportNames)
  $path = Join-Path $binaryDirectory $fileName
  $exported = New-Object System.Collections.Hashtable
  foreach ($line in & dumpbin.exe /nologo /exports $path) {
    if ($line -cmatch '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]{8}\s+(\S+)') {
      $exported[$Matches[1]] = $true
    }
  }
  if ($LASTEXITCODE -ne 0) {
    throw "dumpbin failed on $path (exit code $LASTEXITCODE)"
  }
  foreach ($exportName in $exportNames) {
    if (-not $exported.ContainsKey($exportName)) {
      throw "$fileName does not export $exportName"
    }
  }
}

function Install-Build {
  param([string]$folder)
  Write-BuildLog ("Installed " + (Copy-PluginLayout $game $folder -diagnostics:$diagnostics) + " into $folder")
}

Import-VisualStudioEnvironment
if (-not $buildCommit) {
  $buildCommit = Get-BuildCommit
}
Write-BuildLog "Building commit $buildCommit for $game in the $architecture build"
$diagnosticPluginNames = @(Get-DiagnosticPlugins | ForEach-Object { [System.IO.Path]::GetFileNameWithoutExtension($_) })
Invoke-NativeStep "CMake configure" {
  & cmake.exe -S $PSScriptRoot -B $buildDirectory -G Ninja -DCMAKE_BUILD_TYPE=Release "-DankhPluginsBuildCommit=$buildCommit" "-DrootDirectory=$root" "-DbuildArchitecture=$architecture" "-DdiagnosticPlugins=$($diagnosticPluginNames -join ';')"
}
Invoke-NativeStep "CMake build" { & cmake.exe --build $buildDirectory --target $game }
foreach ($fileName in $requiredExports.Keys) {
  Test-LibraryExport $fileName $requiredExports[$fileName]
  $hash = (Get-FileHash -Algorithm SHA256 (Join-Path $binaryDirectory $fileName)).Hash.ToLowerInvariant()
  Write-BuildLog "$fileName has its $($requiredExports[$fileName].Count) required exports (sha256 $hash)"
}

if ($test) {
  $python = Join-Path (Join-Path (Join-Path $PSScriptRoot ".venv") "Scripts") "python.exe"
  if (-not (Test-Path $python)) {
    throw "The tests run in the repository's .venv, which is missing: python -m venv .venv, then .venv\Scripts\python.exe -m pip install -e ."
  }
  Invoke-NativeStep "Python client tests" {
    & $python -m unittest discover -s (Join-Path (Join-Path $PSScriptRoot "python") "tests") -p "test*.py" -v
  }
  $builtGame = Join-Path $binaryDirectory (Split-Path -Leaf (Get-GameExecutable $game))
  if (Test-Path -LiteralPath $builtGame) {
    # Only a fixture game builds its own executable: the loader and a test plugin are then run in it.
    $fixtureFolder = Join-Path $gameBuildDirectory "fixtureInstall"
    if (Test-Path -LiteralPath $fixtureFolder) {
      Remove-Item -LiteralPath $fixtureFolder -Recurse -Force
    }
    New-Item -ItemType Directory -Path $fixtureFolder | Out-Null
    Copy-Item -LiteralPath $builtGame -Destination $fixtureFolder
    foreach ($builtLauncher in $launcher) {
      Copy-Item -LiteralPath (Join-Path $binaryDirectory $builtLauncher) -Destination $fixtureFolder
    }
    Write-BuildLog ("Fixture install: " + (Copy-PluginLayout $game $fixtureFolder -diagnostics))
    $env:ankhPluginsFixtureRoot = $root
    $env:ankhPluginsFixtureGame = $game
    $env:ankhPluginsFixtureFolder = $fixtureFolder
    Invoke-NativeStep "Fixture tests" {
      & $python -m unittest discover -s (Join-Path (Join-Path $PSScriptRoot "python") "fixtureTests") -p "test*.py" -v
    }
  }
}

if ($gameFolder) {
  Install-Build $gameFolder
}
