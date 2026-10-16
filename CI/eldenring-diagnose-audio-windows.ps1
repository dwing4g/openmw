#Requires -Version 5.1
<#
.SYNOPSIS
Run the existing Elden combat package and retain evidence of silent audio.
.DESCRIPTION
Keeps reports outside the package in Documents\OpenMW-Audio-Diagnostics.
Uses the existing profile without changing volume, device or backend settings.
Can run from the source tree (including a WSL UNC path) or beside openmw.exe.
#>
[CmdletBinding()]
param([string]$PackageDirectory = '')

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'Run this script in Windows PowerShell.'
}
$documents = [Environment]::GetFolderPath('MyDocuments')
if (-not $documents) { throw 'Windows did not return a Documents folder.' }
if (-not $PackageDirectory) {
    $sourceRoot = Split-Path -Parent $PSScriptRoot
    # Prefer the package beside this script or produced by this source checkout.
    # Windows may redirect Known Documents to OneDrive while older builds remain
    # in USERPROFILE\Documents.
    $candidates = @(
        $PSScriptRoot,
        (Join-Path $sourceRoot 'OpenMW-EldenCombat-Windows-x64'),
        (Join-Path $sourceRoot 'build-eldenring-package-x64')
    )
    $documentRoots = @($documents)
    if ($env:USERPROFILE) { $documentRoots += Join-Path $env:USERPROFILE 'Documents' }
    foreach ($root in ($documentRoots | Select-Object -Unique)) {
        $candidates += Join-Path $root 'openmw\OpenMW-EldenCombat-Windows-x64'
        $candidates += Join-Path $root 'openmw\build-eldenring-package-x64'
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'openmw.exe') -PathType Leaf) {
            $PackageDirectory = $candidate
            break
        }
    }
    if (-not $PackageDirectory) {
        throw 'No existing Windows package found. Pass -PackageDirectory with the folder containing openmw.exe.'
    }
}
$PackageDirectory = [IO.Path]::GetFullPath($PackageDirectory)
$executable = Join-Path $PackageDirectory 'openmw.exe'
$profile = Join-Path $PackageDirectory 'elden-profile'
foreach ($required in @($executable, (Join-Path $profile 'openmw.cfg'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing required file: $required" }
}

$runName = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$report = Join-Path (Join-Path $documents 'OpenMW-Audio-Diagnostics') $runName
New-Item -ItemType Directory -Path $report | Out-Null
Write-Host "Audio evidence will be saved to: $report"
$snapshots = [ordered]@{
    'package-openmw.cfg' = (Join-Path $PackageDirectory 'openmw.cfg')
    'package-defaults.bin' = (Join-Path $PackageDirectory 'defaults.bin')
    'package-settings-default.cfg' = (Join-Path $PackageDirectory 'settings-default.cfg')
    'profile-openmw.cfg' = (Join-Path $profile 'openmw.cfg')
    'profile-settings.cfg' = (Join-Path $profile 'settings.cfg')
    'previous-openmw.log' = (Join-Path $profile 'openmw.log')
    'standard-openmw.cfg' = (Join-Path $documents 'My Games\OpenMW\openmw.cfg')
    'standard-settings.cfg' = (Join-Path $documents 'My Games\OpenMW\settings.cfg')
    'package-alsoft.ini' = (Join-Path $PackageDirectory 'alsoft.ini')
}
if ($env:APPDATA) { $snapshots['user-alsoft.ini'] = Join-Path $env:APPDATA 'alsoft.ini' }
foreach ($entry in $snapshots.GetEnumerator()) {
    if (Test-Path -LiteralPath $entry.Value -PathType Leaf) {
        Copy-Item -LiteralPath $entry.Value -Destination (Join-Path $report $entry.Key)
    }
}
Get-ChildItem -LiteralPath $PackageDirectory -File |
    Where-Object { $_.Extension -in @('.exe', '.dll') } |
    ForEach-Object {
        [pscustomobject]@{
            Name = $_.Name; Size = $_.Length; FileVersion = $_.VersionInfo.FileVersion
            ProductVersion = $_.VersionInfo.ProductVersion
            SHA256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        }
    } | Export-Csv -LiteralPath (Join-Path $report 'binaries.csv') -NoTypeInformation -Encoding UTF8

# OpenMW accepts VERBOSE in components/debug/debugging.cpp. OpenAL logging:
# https://github.com/kcat/openal-soft/blob/master/docs/env-vars.txt
$logging = [ordered]@{
    OPENMW_DEBUG_LEVEL = 'VERBOSE'
    ALSOFT_LOGLEVEL = '3'
    ALSOFT_LOGFILE = (Join-Path $report 'openal.log')
}
$originalEnvironment = @{}
foreach ($name in $logging.Keys) {
    $originalEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
[ordered]@{
    Package = $PackageDirectory; Profile = $profile; Started = (Get-Date).ToString('o')
    Arguments = '--replace config --config elden-profile'
    PreviousLoggingEnvironment = $originalEnvironment
    OpenALDrivers = [Environment]::GetEnvironmentVariable('ALSOFT_DRIVERS', 'Process')
    OpenALConfig = [Environment]::GetEnvironmentVariable('ALSOFT_CONF', 'Process')
} | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $report 'run.json') -Encoding UTF8

Write-Host 'Leave the main menu open briefly to check the music, then quit the game to finish the report.'
try {
    foreach ($name in $logging.Keys) { [Environment]::SetEnvironmentVariable($name, $logging[$name], 'Process') }
    $game = Start-Process -FilePath $executable -WorkingDirectory $PackageDirectory `
        -ArgumentList @('--replace', 'config', '--config', 'elden-profile') `
        -RedirectStandardOutput (Join-Path $report 'stdout.log') `
        -RedirectStandardError (Join-Path $report 'stderr.log') -Wait -PassThru
    "Exit code: $($game.ExitCode)" | Set-Content -LiteralPath (Join-Path $report 'exit-code.txt') -Encoding UTF8
} catch {
    $_ | Out-String | Set-Content -LiteralPath (Join-Path $report 'launch-error.txt') -Encoding UTF8
    throw
} finally {
    foreach ($name in $logging.Keys) { [Environment]::SetEnvironmentVariable($name, $originalEnvironment[$name], 'Process') }
    $engineLog = Join-Path $profile 'openmw.log'
    if (Test-Path -LiteralPath $engineLog -PathType Leaf) {
        Copy-Item -LiteralPath $engineLog -Destination (Join-Path $report 'openmw.log')
    }
    Write-Host "Saved audio diagnostics: $report"
}
$pattern = 'Initializing OpenAL|Opened "|audio device|sound disabled|Title music|morrowind title|Failed.*(audio|stream)|Error updating stream|ALC? error|No codec|AL lib:.*(EE|WW)'
$logs = @('openmw.log', 'stdout.log', 'stderr.log', 'openal.log') |
    ForEach-Object { Join-Path $report $_ } | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf }
$summary = @(Select-String -LiteralPath $logs -Pattern $pattern -Encoding UTF8 |
    ForEach-Object { $_.Line } | Select-Object -Unique -First 60)
$summary | Set-Content -LiteralPath (Join-Path $report 'audio-summary.txt') -Encoding UTF8
if ($summary.Count) { $summary | ForEach-Object { Write-Host $_ } }
else { Write-Host 'No matching audio messages were captured; inspect the complete logs in the report folder.' }
Write-Host 'This run collected diagnostics; it did not apply an audio fix.'
