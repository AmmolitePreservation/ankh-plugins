<#
.SYNOPSIS
  Builds ankh-plugins for one game: DLLs for its architecture (game.cfg) from Visual Studio's MSVC, CMake
  and Ninja.
.DESCRIPTION
  -game names a folder under games. Everything lands in build\<game>\bin: the loader (carrying the game's plugin
  list, named for the system DLL game.cfg's loader= has it stand in for) and the plugin DLLs, which carry their
  settings files. -test runs the Python client's tests
  in the repository's .venv and, for a game that builds its own executable (a fixture), runs the loader and a test
  plugin in it. -buildCommit stamps the DLLs with that text in place of the checked-out commit. -gameFolder
  installs the build into the game's install folder as a release lays it out (layout.ps1), leaving the folder's
  settings files to the plugins; -diagnostics adds the diagnostic plugins to it.
.EXAMPLE
  .\build.ps1 -game soma -test -diagnostics -gameFolder "<SOMA install folder>"
#>
param(
  [Parameter(Mandatory = $true)][string]$game,
  [switch]$test,
  [string]$gameFolder,
  [switch]$diagnostics,
  [string]$buildCommit
)
if ($diagnostics -and -not $gameFolder) {
  throw "-diagnostics adds the diagnostic plugins to the -gameFolder install"
}
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "layout.ps1")
$buildDirectory = Join-Path (Join-Path $PSScriptRoot "build") $game
$binaryDirectory = Join-Path $buildDirectory "bin"
$loader = Get-GameLoader $game
$requiredExports = @{ "$loader.dll" = @(Get-Content -LiteralPath (Join-Path (Get-LoaderProxyFolder) "$loader.txt")) }
$launcher = @(Get-GameLauncher $game)
if ($launcher.Count -eq 1) {
  $requiredExports["msimg32.dll"] = @("AlphaBlend")
}
$architecture = Get-GameArchitecture $game
$timeDateStamp = @(Get-GameTimeDateStamp $game)
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
  $commit = & git -C $PSScriptRoot rev-parse --short HEAD
  if ($LASTEXITCODE -ne 0) {
    throw "git rev-parse failed"
  }
  $changes = & git -C $PSScriptRoot status --porcelain
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
Write-BuildLog "Building commit $buildCommit for $game"
Invoke-NativeStep "CMake configure" {
  & cmake.exe -S $PSScriptRoot -B $buildDirectory -G Ninja -DCMAKE_BUILD_TYPE=Release "-DankhPluginsBuildCommit=$buildCommit" "-Dgame=$game" "-DgameArchitecture=$architecture" "-DgameExecutables=$((@(Split-Path -Leaf (Get-GameExecutable $game)) + @(Get-GameCompanions $game)) -join '|')" "-DgameLauncher=$($launcher -join '')" "-DgameLoader=$loader" "-DgameTimeDateStamp=$($timeDateStamp -join '')"
}
Invoke-NativeStep "CMake build" { & cmake.exe --build $buildDirectory }
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
    $fixtureFolder = Join-Path $buildDirectory "fixtureInstall"
    if (Test-Path -LiteralPath $fixtureFolder) {
      Remove-Item -LiteralPath $fixtureFolder -Recurse -Force
    }
    New-Item -ItemType Directory -Path $fixtureFolder | Out-Null
    Copy-Item -LiteralPath $builtGame -Destination $fixtureFolder
    Write-BuildLog ("Fixture install: " + (Copy-PluginLayout $game $fixtureFolder -diagnostics))
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
