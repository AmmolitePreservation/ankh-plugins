<#
.SYNOPSIS
Lays out a game's built files as they go into that game's install folder, so an install and a release hold the same
files.

.DESCRIPTION
-rootFolder is the folder whose games\<game> folders hold the games and whose build folder takes their builds: this
repository, or a root of its own (build.ps1 -root). A build is for an architecture (build\<architecture>, which
game.cfg's architecture picks) and holds every game of it; a game's files are in its games\<game>\bin there.
games\<game>\game.cfg names the game's executable relative to its install folder: the loader DLL (loader=) goes beside
it, and the ankh-plugins folder beside it gets every plugin the game's plugins.txt names and, in its game folder, the
game's game.cfg and plugins.txt, which tell the loader which game it is in; a newer build replaces all of them. No
settings files: each plugin carries its own and writes or updates them in the ankh-plugins folder when the game starts,
so a player's settings are never replaced. The diagnostic plugins never ship; Copy-PluginLayout -diagnostics adds them
to an install, and the loader keeps a listed plugin whose DLL the folder holds. The licenses folder of ankh-plugins
gets this repository's license and the notices of the code built into the DLLs.

The kit is the download for a game that has none of its own: every loader and every central plugin of an architecture
(the build's target central), with a game.cfg and a plugins.txt to fill in.
#>
param([Parameter(Mandatory = $true)][string]$rootFolder)

$repositoryFolder = $PSScriptRoot
if (-not (Test-Path -LiteralPath (Join-Path $rootFolder "games") -PathType Container)) {
  throw "$rootFolder has no games folder"
}

function Get-GameFolder {
  param([string]$game)
  $folder = Join-Path (Join-Path $rootFolder "games") $game
  if (-not (Test-Path -LiteralPath (Join-Path $folder "game.cfg"))) {
    throw "$folder\game.cfg does not exist"
  }
  return $folder
}

function Get-BuildFolder {
  # The build tree of the game's architecture, which every game of that architecture in the root shares.
  param([string]$game)
  return Join-Path (Join-Path $rootFolder "build") (Get-GameArchitecture $game)
}

function Get-GameBuildFolder {
  param([string]$game)
  return Join-Path (Join-Path (Get-BuildFolder $game) "games") $game
}

function Get-GameBinaryFolder {
  param([string]$game)
  return Join-Path (Get-GameBuildFolder $game) "bin"
}

function Get-GameConfigPath {
  param([string]$game)
  return Join-Path (Get-GameFolder $game) "game.cfg"
}

function Get-GameSettingValues {
  # Every value game.cfg gives the key, in the file's order; callers wrap the call in @().
  param([string]$game, [string]$key)
  $values = @()
  foreach ($line in Get-Content -LiteralPath (Get-GameConfigPath $game)) {
    if ($line -match ('^\s*' + $key + '\s*=\s*(.+?)\s*$')) {
      $values += $Matches[1]
    }
  }
  return $values
}

function Get-GameSetting {
  param([string]$game, [string]$key)
  $values = @(Get-GameSettingValues $game $key)
  if ($values.Count -ne 1) {
    throw "$(Get-GameConfigPath $game) must set $key exactly once"
  }
  return $values[0]
}

function Get-GameExecutable {
  param([string]$game)
  return Get-GameSetting $game "executable"
}

function Get-GameCompanions {
  # Programs beside the game's executable that also get the plugins, such as a launcher step the game cannot start
  # without: file names, any number of companion= lines.
  param([string]$game)
  $companions = @(Get-GameSettingValues $game "companion")
  foreach ($companion in $companions) {
    if ($companion -notmatch '^[^\\/:|]+\.exe$') {
      throw "$(Get-GameConfigPath $game): companion must be an .exe file name beside the executable, not $companion"
    }
  }
  return $companions
}

function Get-GameLauncher {
  # A launcher the game's Steam entry starts, which launcher-skip (msimg32.dll) replaces with the game; none when unset.
  param([string]$game)
  $launchers = @(Get-GameSettingValues $game "launcher")
  foreach ($launcher in $launchers) {
    if ($launcher -notmatch '^[^\\/:|]+\.exe$') {
      throw "$(Get-GameConfigPath $game): launcher must be an .exe file name beside the executable, not $launcher"
    }
  }
  if ($launchers.Count -gt 1) {
    throw "$(Get-GameConfigPath $game) sets launcher more than once"
  }
  return $launchers
}

function Get-GameTimeDateStamp {
  # The build of the executable the game's handlers were written for, which the loader checks; none when unset.
  param([string]$game)
  $stamps = @(Get-GameSettingValues $game "timeDateStamp")
  if ($stamps.Count -gt 1) {
    throw "$(Get-GameConfigPath $game) sets timeDateStamp more than once"
  }
  foreach ($stamp in $stamps) {
    if ($stamp -cnotmatch '^0x[0-9A-F]{8}$') {
      throw "$(Get-GameConfigPath $game): timeDateStamp must be 0x and eight hexadecimal digits in capitals, as the loader's log prints it, not $stamp"
    }
  }
  return $stamps
}

function Get-LoaderProxyFolder {
  # <name>.txt lists the exports of the system DLL <name>.dll, which the loader can then stand in for.
  return Join-Path (Join-Path (Join-Path $repositoryFolder "core") "loader") "proxies"
}

function Get-GameLoader {
  # The system DLL the game imports, which the loader stands in for.
  param([string]$game)
  $loader = Get-GameSetting $game "loader"
  $known = @(Get-ChildItem -LiteralPath (Get-LoaderProxyFolder) -Filter "*.txt" | ForEach-Object { $_.BaseName })
  if ($known -cnotcontains $loader) {
    throw "$(Get-GameConfigPath $game): loader must be one of $(($known | Sort-Object) -join ', '), not $loader"
  }
  return $loader
}

function Get-GameProxies {
  # The DLLs that go beside the game's executable: the loader, and msimg32.dll for a game with a launcher.
  param([string]$game)
  $proxies = @("$(Get-GameLoader $game).dll")
  if (@(Get-GameLauncher $game).Count -eq 1) {
    $proxies += "msimg32.dll"
  }
  return $proxies
}

function Get-GameArchitecture {
  param([string]$game)
  $architecture = Get-GameSetting $game "architecture"
  if (@("x64", "x86") -notcontains $architecture) {
    throw "$(Get-GameConfigPath $game): architecture must be x64 or x86, not $architecture"
  }
  return $architecture
}

function Get-DiagnosticPlugins {
  return @("stability-guard.dll", "display-report.dll")
}

function Get-CentralBinaryFolder {
  # Where the build's target central puts every loader and central plugin of an architecture.
  param([string]$architecture)
  return Join-Path (Join-Path (Join-Path (Join-Path $rootFolder "build") $architecture) "central") "bin"
}

function Get-LoaderNames {
  return @(Get-ChildItem -LiteralPath (Get-LoaderProxyFolder) -Filter "*.txt" | ForEach-Object { $_.BaseName } | Sort-Object)
}

function Get-CentralPlugins {
  # Every plugin any game can take: the folders of the repository's plugins and the root's. launcher-skip is none of
  # them: the loader does not load it (Get-KitProxies).
  $folders = @((Join-Path $repositoryFolder "plugins"), (Join-Path $rootFolder "plugins")) | Select-Object -Unique
  $names = @()
  foreach ($folder in $folders) {
    if (Test-Path -LiteralPath $folder -PathType Container) {
      $names += @(Get-ChildItem -LiteralPath $folder -Directory | Where-Object { $_.Name -ne "launcher-skip" } | ForEach-Object { $_.Name })
    }
  }
  return @($names | Sort-Object | ForEach-Object { "$_.dll" })
}

function Get-KitProxies {
  # The DLLs of a kit that go beside an executable: every loader, and launcher-skip's msimg32.dll, which is x86 only.
  param([string]$architecture)
  $proxies = @(Get-LoaderNames | ForEach-Object { "$_.dll" })
  if ($architecture -eq "x86") {
    $proxies += "msimg32.dll"
  }
  return $proxies
}

function Get-LicenseFiles {
  # Source path and the name it has in an install's licenses folder: this repository's license, and the notice the
  # vendored hooking library asks to travel with every binary built from it.
  return @(
    @{ source = (Join-Path $repositoryFolder "LICENSE"); name = "ankh-plugins.txt" },
    @{ source = (Join-Path (Join-Path (Join-Path (Join-Path (Join-Path $repositoryFolder "core") "shared") "vendor") "minhook") "LICENSE.txt"); name = "MinHook.txt" }
  )
}

function Copy-LicenseFiles {
  param([string]$pluginsFolder)
  $licensesFolder = Join-Path $pluginsFolder "licenses"
  if (-not (Test-Path -LiteralPath $licensesFolder)) {
    New-Item -ItemType Directory -Path $licensesFolder -Force | Out-Null
  }
  foreach ($license in Get-LicenseFiles) {
    if (-not (Test-Path -LiteralPath $license.source)) {
      throw "$($license.source) is missing"
    }
    Copy-Item -LiteralPath $license.source -Destination (Join-Path $licensesFolder $license.name) -Force
  }
}

function Get-GameDefinitionFiles {
  # What the loader reads to know which game it is in, from the game folder of ankh-plugins.
  return @("game.cfg", "plugins.txt")
}

function Get-ShippedPlugins {
  # Every plugin the game's plugins.txt names, on or switched off with #: a player can turn any of them on.
  param([string]$game)
  $listPath = Join-Path (Get-GameFolder $game) "plugins.txt"
  $plugins = @()
  foreach ($line in Get-Content -LiteralPath $listPath) {
    $name = $line.Trim().TrimStart("#").Trim()
    if ($name -match '^[^\\/: ]+\.dll$') {
      $plugins += $name
    }
  }
  if ($plugins.Count -eq 0) {
    throw "$listPath names no plugins"
  }
  return $plugins
}

function Copy-LayoutFile {
  param([string]$sourcePath, [string]$destinationFolder)
  if (-not (Test-Path -LiteralPath $sourcePath)) {
    throw "$sourcePath is missing; run build.ps1 first"
  }
  if (-not (Test-Path -LiteralPath $destinationFolder)) {
    New-Item -ItemType Directory -Path $destinationFolder -Force | Out-Null
  }
  Copy-Item -LiteralPath $sourcePath -Destination $destinationFolder -Force
}

function Copy-ReleaseFiles {
  param([string]$game, [string]$clientFolder)
  $binaryFolder = Get-GameBinaryFolder $game
  $plugins = Get-ShippedPlugins $game
  $proxies = Get-GameProxies $game
  foreach ($proxy in $proxies) {
    Copy-LayoutFile (Join-Path $binaryFolder $proxy) $clientFolder
  }
  foreach ($plugin in $plugins) {
    Copy-LayoutFile (Join-Path $binaryFolder $plugin) (Join-Path $clientFolder "ankh-plugins")
  }
  foreach ($definitionFile in Get-GameDefinitionFiles) {
    Copy-LayoutFile (Join-Path (Join-Path $binaryFolder "game") $definitionFile) (Join-Path (Join-Path $clientFolder "ankh-plugins") "game")
  }
  Copy-LicenseFiles (Join-Path $clientFolder "ankh-plugins")
  return "$($proxies -join ', '), plugins $($plugins -join ', ')"
}

function Copy-KitFiles {
  # Lays the kit of an architecture out in an empty folder: README.txt, loaders\ with every DLL that goes beside an
  # executable, and the ankh-plugins folder with every central plugin, the licenses and, in game\, a game.cfg to fill
  # in and a plugins.txt that lists every plugin switched off.
  param([string]$architecture, [string]$kitFolder)
  $binaryFolder = Get-CentralBinaryFolder $architecture
  $kitSource = Join-Path $repositoryFolder "kit"
  $pluginsFolder = Join-Path $kitFolder "ankh-plugins"
  $gameFolder = Join-Path $pluginsFolder "game"
  $proxies = @(Get-KitProxies $architecture)
  $plugins = @(Get-CentralPlugins)
  foreach ($proxy in $proxies) {
    Copy-LayoutFile (Join-Path $binaryFolder $proxy) (Join-Path $kitFolder "loaders")
  }
  foreach ($plugin in $plugins) {
    Copy-LayoutFile (Join-Path $binaryFolder $plugin) $pluginsFolder
  }
  Copy-LicenseFiles $pluginsFolder
  New-Item -ItemType Directory -Path $gameFolder -Force | Out-Null
  $ascii = [System.Text.Encoding]::ASCII
  $readme = @(Get-Content -LiteralPath (Join-Path $kitSource "README.txt"))
  [System.IO.File]::WriteAllText((Join-Path $kitFolder "README.txt"), (($readme -join "`r`n") + "`r`n"), $ascii)
  $gameSettings = @(Get-Content -LiteralPath (Join-Path $kitSource "game.cfg")) | ForEach-Object { $_.Replace("{architecture}", $architecture) }
  [System.IO.File]::WriteAllText((Join-Path $gameFolder "game.cfg"), (($gameSettings -join "`r`n") + "`r`n"), $ascii)
  $pluginList = @(
    "# The plugins the loader loads into the game, one DLL per line in load order, from the ankh-plugins folder.",
    "# A line starting with # switches that plugin off: remove the # in front of the ones the game should load.",
    "# After the first start the list to edit is plugins.txt in the ankh-plugins folder, which starts as a copy of this."
  ) + @($plugins | ForEach-Object { "#$_" })
  [System.IO.File]::WriteAllText((Join-Path $gameFolder "plugins.txt"), (($pluginList -join "`r`n") + "`r`n"), $ascii)
  return "$($proxies -join ', '), plugins $($plugins -join ', ')"
}

function Get-FolderFingerprint {
  # The SHA-256 of a folder's files by relative path: the same files give the same fingerprint, whatever a zip's timestamps.
  param([string]$folder)
  $lines = @()
  foreach ($file in Get-ChildItem -LiteralPath $folder -Recurse -File | Sort-Object FullName) {
    $lines += $file.FullName.Substring($folder.Length + 1).Replace("\", "/") + " " + (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash.ToLowerInvariant()
  }
  $bytes = [System.Text.Encoding]::ASCII.GetBytes(($lines -join "`n"))
  $hasher = New-Object System.Security.Cryptography.SHA256Managed
  return ([System.BitConverter]::ToString($hasher.ComputeHash($bytes)) -replace "-", "").ToLowerInvariant()
}

function Get-ReleaseFingerprint {
  # The SHA-256 of the release's files by name: the same files give the same fingerprint, whatever the zip's timestamps.
  param([string]$game)
  $binaryFolder = Get-GameBinaryFolder $game
  $lines = @()
  $definitionFiles = @(Get-GameDefinitionFiles | ForEach-Object { Join-Path "game" $_ })
  foreach ($fileName in @(Get-GameProxies $game) + @(Get-ShippedPlugins $game) + $definitionFiles) {
    $path = Join-Path $binaryFolder $fileName
    if (-not (Test-Path -LiteralPath $path)) {
      throw "$path is missing; run build.ps1 first"
    }
    $lines += $fileName + " " + (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()
  }
  foreach ($license in Get-LicenseFiles) {
    $lines += "licenses/" + $license.name + " " + (Get-FileHash -Algorithm SHA256 -LiteralPath $license.source).Hash.ToLowerInvariant()
  }
  $bytes = [System.Text.Encoding]::ASCII.GetBytes(($lines -join "`n"))
  $hasher = New-Object System.Security.Cryptography.SHA256Managed
  return ([System.BitConverter]::ToString($hasher.ComputeHash($bytes)) -replace "-", "").ToLowerInvariant()
}

function Add-DiagnosticPlugins {
  param([string]$game, [string]$clientFolder)
  $binaryFolder = Get-GameBinaryFolder $game
  $pluginsFolder = Join-Path $clientFolder "ankh-plugins"
  $diagnosticPlugins = @(Get-DiagnosticPlugins)
  foreach ($plugin in $diagnosticPlugins) {
    Copy-LayoutFile (Join-Path $binaryFolder $plugin) $pluginsFolder
  }
  $listPath = Join-Path $pluginsFolder "plugins.txt"
  $lines = @()
  if (Test-Path -LiteralPath $listPath) {
    $lines = @(Get-Content -LiteralPath $listPath)
  }
  $listed = @($lines | ForEach-Object { $_.Trim().TrimStart("#").Trim() })
  $missing = @($diagnosticPlugins | Where-Object { $listed -notcontains $_ })
  if ($missing.Count -gt 0) {
    [System.IO.File]::WriteAllText($listPath, ((@($missing) + $lines) -join "`r`n"), [System.Text.Encoding]::ASCII)
  }
  return "diagnostic plugins $($diagnosticPlugins -join ', ')"
}

function Copy-PluginLayout {
  param([string]$game, [string]$destination, [switch]$diagnostics)
  $executablePath = Join-Path $destination (Get-GameExecutable $game)
  if (-not (Test-Path -LiteralPath $executablePath)) {
    throw "$destination has no $(Get-GameExecutable $game)"
  }
  $clientFolder = Split-Path $executablePath
  $installed = Copy-ReleaseFiles $game $clientFolder
  if ($diagnostics) {
    $installed += ", " + (Add-DiagnosticPlugins $game $clientFolder)
  }
  return $installed
}
