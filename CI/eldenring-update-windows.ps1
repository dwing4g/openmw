#Requires -Version 5.1
<#
.SYNOPSIS
Copy the updated WSL sources/assets, rebuild, and refresh the existing game package.
.DESCRIPTION
Run this script in native Windows PowerShell through its WSL UNC path. Uses the
existing Documents\openmw build and unpacked package when present. Preserves
settings and saves, and creates an updated ZIP. The profile generator repairs
an empty default save path when exactly one previous Documents folder has saves.
#>
[CmdletBinding()]
param(
    [string]$DestinationDirectory = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'openmw'),
    [string]$PackageDirectory = '',
    [string]$MorrowindDirectory = 'C:\Program Files (x86)\Steam\steamapps\common\Morrowind',
    [ValidateRange(1, 256)]
    [int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount - 1)
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'Run this update in native Windows PowerShell.'
}
$DestinationDirectory = [IO.Path]::GetFullPath($DestinationDirectory)
if (-not $PackageDirectory) {
    $existingPackage = Join-Path $DestinationDirectory 'OpenMW-EldenCombat-Windows-x64'
    if (Test-Path -LiteralPath (Join-Path $existingPackage 'openmw.exe') -PathType Leaf) {
        $PackageDirectory = $existingPackage
    } else {
        $PackageDirectory = Join-Path $DestinationDirectory 'build-eldenring-package-x64'
    }
}
$PackageDirectory = [IO.Path]::GetFullPath($PackageDirectory)
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ($sourceRoot.TrimEnd('\') -ine $DestinationDirectory.TrimEnd('\')) {
    & (Join-Path $PSScriptRoot 'eldenring-copy-to-documents.ps1') -DestinationDirectory $DestinationDirectory
}
& (Join-Path $DestinationDirectory 'CI\eldenring-build-windows.ps1') `
    -InstallDirectory $PackageDirectory -MorrowindDirectory $MorrowindDirectory -Jobs $Jobs
Write-Host "Updated package: $PackageDirectory"
Write-Host 'Open the launcher, then Settings > Gameplay > Elden Ring regional and boss music to enable the imported tracks.'
