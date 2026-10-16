#Requires -Version 5.1
<#
.SYNOPSIS
Apply reviewed audit fixes to the existing Windows checkout and rebuild its package.
.DESCRIPTION
Run this script from the WSL checkout through its Windows UNC path. The adjacent
eldenring-audit-files.json lists relative paths and beforeSha256/afterSha256 hashes
(beforeSha256 is null for new files). All source and destination files are checked
before copying. Uses the existing CMake build, keeps backups, and preserves profiles.
Updates the engine and packed defaults together, plus an already packaged launcher
when the existing build enables it. Use eldenring-build-windows.ps1 to add a launcher
and its Qt runtime to a package that does not have them yet.
#>
[CmdletBinding()]
param(
    [string]$SourceDirectory = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'openmw'),
    [string]$BuildDirectory = '',
    [string]$PackageDirectory = '',
    [string]$AnimationsSource = '',
    [ValidateRange(1, 256)] [int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount - 1)
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Get-Sha256([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }

$auditRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$SourceDirectory = [IO.Path]::GetFullPath($SourceDirectory)
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $SourceDirectory 'build-eldenring-windows-2026-x64' }
if (-not $PackageDirectory) { $PackageDirectory = Join-Path $SourceDirectory 'build-eldenring-package-x64' }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$PackageDirectory = [IO.Path]::GetFullPath($PackageDirectory)
$packageExe = Join-Path $PackageDirectory 'openmw.exe'
$cache = @{}
foreach ($line in [IO.File]::ReadAllLines((Join-Path $BuildDirectory 'CMakeCache.txt'), [Text.Encoding]::UTF8)) {
    if ($line -match '^(CMAKE_COMMAND|CMAKE_HOME_DIRECTORY|BUILD_LAUNCHER):[^=]+=(.*)$') { $cache[$Matches[1]] = $Matches[2] }
}
if (-not $cache.ContainsKey('CMAKE_COMMAND') -or -not $cache.ContainsKey('CMAKE_HOME_DIRECTORY')) {
    throw 'The existing build cache must identify CMake and its source directory.'
}
if ([IO.Path]::GetFullPath($cache['CMAKE_HOME_DIRECTORY']).TrimEnd('\') -ine $SourceDirectory.TrimEnd('\')) {
    throw "The selected build does not belong to $SourceDirectory."
}
$cmake = $cache['CMAKE_COMMAND']
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
foreach ($required in @($cmake, $ctest, $packageExe)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing required file: $required" }
}
$buildLauncher = $cache.ContainsKey('BUILD_LAUNCHER') -and $cache['BUILD_LAUNCHER'] -match '^(1|ON|YES|TRUE|Y)$'
$artifactNames = @('defaults.bin', 'openmw.exe')
if ($buildLauncher) {
    # This helper refreshes existing executables; the full installer deploys Qt DLLs and plugins.
    $launcherRuntime = @('openmw-launcher.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Svg.dll',
        'platforms\qwindows.dll', 'imageformats\qsvg.dll', 'iconengines\qsvgicon.dll')
    foreach ($relative in $launcherRuntime) {
        if (-not (Test-Path -LiteralPath (Join-Path $PackageDirectory $relative) -PathType Leaf)) {
            throw "The existing package is missing launcher runtime file '$relative'. Run CI\eldenring-build-windows.ps1 to install the launcher and its runtime first. No files were copied."
        }
    }
    $artifactNames += 'openmw-launcher.exe'
} else {
    Write-Host 'This build disables the launcher. Game settings will be updated; use CI\eldenring-build-windows.ps1 to add the launcher settings UI.'
}
$artifacts = @()
foreach ($relative in $artifactNames) {
    $destination = Join-Path $PackageDirectory $relative
    if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
        throw "The existing package is missing '$relative'. Repair it with CI\eldenring-build-windows.ps1 first. No files were copied."
    }
    $artifacts += [pscustomobject]@{ Relative = $relative; Destination = $destination
        Source = (Join-Path $BuildDirectory ('Release\' + $relative)); Current = (Get-Sha256 $destination)
        Expected = $null }
}

$manifestPath = Join-Path $PSScriptRoot 'eldenring-audit-files.json'
$manifest = [IO.File]::ReadAllText($manifestPath, [Text.Encoding]::UTF8) | ConvertFrom-Json
if ($manifest.formatVersion -ne 1 -or @($manifest.files).Count -eq 0) { throw 'Invalid or empty audit manifest.' }
$entries = @()
$seen = @{}
foreach ($item in $manifest.files) {
    $relative = $item.path.Replace('/', '\')
    if (-not $relative -or [IO.Path]::IsPathRooted($relative) -or $relative.Contains(':') `
        -or @($relative.Split('\') | Where-Object { $_ -in @('', '.', '..') }).Count -gt 0 -or $seen.ContainsKey($relative)) {
        throw "Unsafe or duplicate manifest path: $relative"
    }
    $seen[$relative] = $true
    if ($item.afterSha256 -notmatch '^[0-9a-fA-F]{64}$' `
        -or ($null -ne $item.beforeSha256 -and $item.beforeSha256 -notmatch '^[0-9a-fA-F]{64}$')) {
        throw "Invalid manifest hashes: $relative"
    }
    $source = Join-Path $auditRoot $relative
    $destination = Join-Path $SourceDirectory $relative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf) -or (Get-Sha256 $source) -ine $item.afterSha256) {
        throw "Audit source has changed or is missing: $source"
    }
    $current = $null
    if (Test-Path -LiteralPath $destination) {
        if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) { throw "Not a file: $destination" }
        $current = Get-Sha256 $destination
    }
    if ($current -ine $item.afterSha256 -and $current -ine $item.beforeSha256) {
        throw "Unrelated Windows edits or missing original file: $destination. No files were copied."
    }
    $entries += [pscustomobject]@{ Relative = $relative; Source = $source; Destination = $destination
        Current = $current; Expected = $item.afterSha256; Apply = ($current -ine $item.afterSha256) }
}
$packageDoc = Join-Path $PackageDirectory 'ELDEN-COMBAT.txt'
if ($seen.ContainsKey('ELDEN-COMBAT.txt') -and (Test-Path -LiteralPath $packageDoc)) {
    $doc = $manifest.files | Where-Object { $_.path -ieq 'ELDEN-COMBAT.txt' }
    if (-not (Test-Path -LiteralPath $packageDoc -PathType Leaf) `
        -or ((Get-Sha256 $packageDoc) -ine $doc.beforeSha256 -and (Get-Sha256 $packageDoc) -ine $doc.afterSha256)) {
        throw 'Packaged documentation has unrelated edits; no files were copied.'
    }
}

# Validate the complete bank and its runtime inventory before changing any Windows files.
$python = Get-Command 'py.exe' -ErrorAction SilentlyContinue
$pythonArguments = @()
if ($python) { $pythonArguments = @('-3') } else { $python = Get-Command 'python.exe' -ErrorAction SilentlyContinue }
if (-not $python) { throw 'Python 3.10 or newer is required to validate and install the animation bank; no files were copied.' }
$python = $python.Source
& $python @pythonArguments -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)'
if ($LASTEXITCODE -ne 0) { throw 'Python 3.10 or newer is required; no files were copied.' }
$animationInstaller = Join-Path $auditRoot 'scripts\install_eldenring_animations.py'
if (-not $seen.ContainsKey('scripts\install_eldenring_animations.py') `
    -or -not $seen.ContainsKey('CI\eldenring-audit-animations.py') `
    -or -not $seen.ContainsKey('components\eldencombat\imported_timings.hpp')) {
    throw 'The audit manifest must include the animation installer, scanner and generated timing header; no files were copied.'
}
if (-not $AnimationsSource) {
    foreach ($candidate in @(
        (Join-Path (Split-Path -Parent $auditRoot) 'ELDEN-RING-Combat-Rewrite\assets\player_anims.bin'),
        (Join-Path (Split-Path -Parent $SourceDirectory) 'ELDEN-RING-Combat-Rewrite\assets\player_anims.bin'),
        (Join-Path $PackageDirectory 'resources\vfs\animations\eldencombat\player_anims.bin')
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $AnimationsSource = $candidate; break }
    }
}
if (-not $AnimationsSource -or -not (Test-Path -LiteralPath $AnimationsSource -PathType Leaf)) {
    throw 'The local player_anims.bin was not found. Supply -AnimationsSource with your baked bank; no files were copied.'
}
$animationJson = & $python @pythonArguments $animationInstaller --source $AnimationsSource --check-only --json
if ($LASTEXITCODE -ne 0) { throw 'Animation validation failed; no files were copied.' }
$animationValidation = ($animationJson -join "`n") | ConvertFrom-Json
$animationHash = $animationValidation.sha256
$packageAnimations = Join-Path $PackageDirectory 'resources\vfs\animations\eldencombat\player_anims.bin'
if ((Test-Path -LiteralPath $packageAnimations) -and -not (Test-Path -LiteralPath $packageAnimations -PathType Leaf)) {
    throw "Animation destination is not a file: $packageAnimations. No files were copied."
}
$packageAnimationHash = if (Test-Path -LiteralPath $packageAnimations) { Get-Sha256 $packageAnimations } else { $null }

$backupRoot = Join-Path $BuildDirectory ('audit-backups\' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $backupRoot 'manifest.json')
foreach ($entry in $entries) {
    if (-not $entry.Apply) { Write-Host "Already applied: $($entry.Relative)"; continue }
    if ($null -ne $entry.Current) {
        $backup = Join-Path $backupRoot ('source\' + $entry.Relative)
        New-Item -ItemType Directory -Path (Split-Path -Parent $backup) -Force | Out-Null
        Copy-Item -LiteralPath $entry.Destination -Destination $backup
        if ((Get-Sha256 $backup) -ine $entry.Current) { throw "Backup verification failed: $backup" }
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $entry.Destination) -Force | Out-Null
    Copy-Item -LiteralPath $entry.Source -Destination $entry.Destination -Force
    if ((Get-Sha256 $entry.Destination) -ine $entry.Expected) { throw "Copy verification failed: $($entry.Destination)" }
}
Write-Host "Source backups: $backupRoot"
Push-Location $SourceDirectory
try {
    & $cmake -S $SourceDirectory -B $BuildDirectory -D BUILD_ELDENCOMBAT_TESTS=ON
    if ($LASTEXITCODE -ne 0) { throw "Configure failed ($LASTEXITCODE); packaged executables and defaults were preserved." }
    $buildTargets = @('openmw', 'eldencombat-tests', 'eldencombat-imported-tests', 'eldeninput-tests',
        'eldenanimation-tests', 'eldenai-tests', 'eldenai-imported-tests')
    if ($buildLauncher) { $buildTargets += 'openmw-launcher' }
    & $cmake --build $BuildDirectory --config Release --target @buildTargets --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE); packaged executables and defaults were preserved." }
    foreach ($test in @('eldencombat', 'eldencombat-imported', 'eldeninput', 'eldenanimation', 'eldenai', 'eldenai-imported')) {
        & $ctest --test-dir $BuildDirectory -C Release -R "^$test`$" --output-on-failure --no-tests=error
        if ($LASTEXITCODE -ne 0) { throw "Test $test failed ($LASTEXITCODE); packaged executables and defaults were preserved." }
    }
} finally { Pop-Location }
foreach ($artifact in $artifacts) {
    if (-not (Test-Path -LiteralPath $artifact.Source -PathType Leaf)) {
        throw "Build produced no required artifact: $($artifact.Source). Packaged executables and defaults were preserved."
    }
    $artifact.Expected = Get-Sha256 $artifact.Source
    if (-not (Test-Path -LiteralPath $artifact.Destination -PathType Leaf) `
        -or (Get-Sha256 $artifact.Destination) -ine $artifact.Current) {
        throw "The packaged file changed during the build: $($artifact.Destination). Packaged executables and defaults were preserved."
    }
}
$currentAnimationHash = if (Test-Path -LiteralPath $packageAnimations -PathType Leaf) { Get-Sha256 $packageAnimations } else { $null }
if ($currentAnimationHash -ine $packageAnimationHash) {
    throw 'The packaged animation bank changed during the build; executables, defaults and animations were preserved.'
}
foreach ($artifact in $artifacts) {
    $backup = Join-Path $backupRoot $artifact.Relative
    Copy-Item -LiteralPath $artifact.Destination -Destination $backup
    if ((Get-Sha256 $backup) -ine $artifact.Current) { throw "Package backup verification failed: $backup" }
}
& $python @pythonArguments $animationInstaller --source $AnimationsSource --source-sha256 $animationHash `
    --package $PackageDirectory --backup-directory (Join-Path $backupRoot 'animations')
if ($LASTEXITCODE -ne 0) { throw 'Animation installation failed; packaged executables and defaults were preserved.' }
foreach ($artifact in $artifacts) {
    Copy-Item -LiteralPath $artifact.Source -Destination $artifact.Destination -Force
    if ((Get-Sha256 $artifact.Destination) -ine $artifact.Expected) {
        throw "Packaged file verification failed: $($artifact.Destination). Backups: $backupRoot"
    }
}
if ($seen.ContainsKey('ELDEN-COMBAT.txt')) {
    if (Test-Path -LiteralPath $packageDoc) { Copy-Item -LiteralPath $packageDoc -Destination (Join-Path $backupRoot 'ELDEN-COMBAT.txt') }
    Copy-Item -LiteralPath (Join-Path $SourceDirectory 'ELDEN-COMBAT.txt') -Destination $packageDoc -Force
    if ((Get-Sha256 $packageDoc) -ine (Get-Sha256 (Join-Path $SourceDirectory 'ELDEN-COMBAT.txt'))) { throw 'Packaged documentation verification failed.' }
}
foreach ($artifact in $artifacts) { Write-Host "Updated package file: $($artifact.Destination)" }
Write-Host "Installed animation bank: $packageAnimations"
Write-Host "Start the game with: $(Join-Path $PackageDirectory 'Start-EldenCombat.cmd')"
