<#
.SYNOPSIS
  Writes a timeDateStamp into an executable's PE header, so a fixture game is the build its game.cfg names.
.EXAMPLE
  .\setTimeDateStamp.ps1 -path build\fixture-x64\bin\fixtureGame.exe -timeDateStamp 0x9B5E3C7A
#>
param(
  [Parameter(Mandatory = $true)][string]$path,
  [Parameter(Mandatory = $true)][string]$timeDateStamp
)
$ErrorActionPreference = "Stop"
if ($timeDateStamp -cnotmatch '^0x[0-9A-F]{8}$') {
  throw "timeDateStamp must be 0x and eight hexadecimal digits in capitals, not $timeDateStamp"
}
$bytes = [System.IO.File]::ReadAllBytes($path)
$headerOffset = [System.BitConverter]::ToInt32($bytes, 0x3C)
if ([System.BitConverter]::ToUInt32($bytes, $headerOffset) -ne 0x00004550) {
  throw "$path is not a PE executable"
}
$stampBytes = [System.BitConverter]::GetBytes([System.Convert]::ToUInt32($timeDateStamp, 16))
[System.Array]::Copy($stampBytes, 0, $bytes, $headerOffset + 8, 4)
[System.IO.File]::WriteAllBytes($path, $bytes)
