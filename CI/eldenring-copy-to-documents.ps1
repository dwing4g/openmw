#Requires -Version 5.1
<#
.SYNOPSIS
Copy the current OpenMW source and local assets into Documents for a Windows build.
.DESCRIPTION
Run this script in Windows PowerShell through the WSL UNC path. Copies vendored
extern sources and the generated timing header, excluding Git metadata, builds,
dependencies, caches, and runtime profiles. Existing destination-only files,
builds, profiles and saves are preserved. This script does not start a build.
.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File "\\wsl.localhost\Ubuntu\home\<linux-user>\openMW-eldenring\openmw\CI\eldenring-copy-to-documents.ps1"
.EXAMPLE
.\CI\eldenring-copy-to-documents.ps1 -DestinationDirectory 'D:\Sources\openmw' -ListOnly
#>
[CmdletBinding()]
param(
    [string]$SourceDirectory = (Split-Path -Parent $PSScriptRoot),
    [string]$DestinationDirectory = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'openmw'),
    [string]$AnimationsSource = '',
    [string]$MusicSource = '',
    [string]$SoundsSource = '',
    [switch]$ListOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'Run this script with Windows PowerShell through the WSL UNC path.'
}
$SourceDirectory = [IO.Path]::GetFullPath($SourceDirectory).TrimEnd('\')
$DestinationDirectory = [IO.Path]::GetFullPath($DestinationDirectory).TrimEnd('\')
$comparison = [StringComparison]::OrdinalIgnoreCase
if ($SourceDirectory.Equals($DestinationDirectory, $comparison) `
    -or $DestinationDirectory.StartsWith($SourceDirectory + '\', $comparison) `
    -or $SourceDirectory.StartsWith($DestinationDirectory + '\', $comparison)) {
    throw 'Source and destination must be separate directories, with neither inside the other.'
}
$robocopy = Get-Command 'robocopy.exe' -ErrorAction SilentlyContinue
if (-not $robocopy) { throw 'robocopy.exe was not found in this Windows installation.' }

# A source archive is buildable without .git; vendored libraries must remain present.
$required = @(
    'CMakeLists.txt', 'apps\openmw\CMakeLists.txt', 'components\CMakeLists.txt',
    'extern\oics\CMakeLists.txt', 'extern\sol3\sol\sol.hpp',
    'components\eldencombat\imported_timings.hpp', 'CI\eldenring-build-windows.ps1',
    'scripts\install_eldenring_animations.py', 'CI\eldenring-audit-animations.py', 'scripts\verify_eldenring_copy.py'
)
foreach ($relative in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $SourceDirectory $relative) -PathType Leaf)) {
        throw "The source tree is incomplete: $relative"
    }
}
$timingHeader = Join-Path $SourceDirectory 'components\eldencombat\imported_timings.hpp'
$timingHash = (Get-FileHash -LiteralPath $timingHeader -Algorithm SHA256).Hash

if (-not $AnimationsSource) {
    foreach ($candidate in @(
        (Join-Path $SourceDirectory 'local-assets\player_anims.bin'),
        (Join-Path (Split-Path -Parent $SourceDirectory) 'ELDEN-RING-Combat-Rewrite\assets\player_anims.bin')
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $AnimationsSource = $candidate; break }
    }
}
$animationHash = ''
if ($AnimationsSource) {
    if (-not (Test-Path -LiteralPath $AnimationsSource -PathType Leaf)) {
        throw "The animation source does not exist: $AnimationsSource"
    }
    $reader = New-Object IO.BinaryReader -ArgumentList ([IO.File]::OpenRead($AnimationsSource))
    try {
        if ([Text.Encoding]::ASCII.GetString($reader.ReadBytes(4)) -cne 'ERAN') {
            throw "The animation source is not a baked ERAN bank: $AnimationsSource"
        }
    } finally { $reader.Dispose() }
    $animationHash = (Get-FileHash -LiteralPath $AnimationsSource -Algorithm SHA256).Hash
} else {
    Write-Warning 'No local player_anims.bin was found. Source will be copied, but Elden animations require your baked bank in local-assets\player_anims.bin or the build helper -AnimationsSource option.'
}

if (-not $MusicSource) {
    foreach ($candidate in @(
        (Join-Path $SourceDirectory 'local-assets\music\eldenring'),
        (Join-Path $SourceDirectory 'build-eldenring-assets\music\eldenring')
    )) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'manifest.json') -PathType Leaf) {
            $MusicSource = $candidate
            break
        }
    }
}
$musicPython = Get-Command 'py.exe' -ErrorAction SilentlyContinue
$musicPythonArguments = @()
if ($musicPython) { $musicPythonArguments = @('-3') }
else { $musicPython = Get-Command 'python.exe' -ErrorAction SilentlyContinue }
if (-not $musicPython) { throw 'Python 3.10+ is required to validate copied source and local assets.' }
if ($AnimationsSource) {
    # Check the complete bank, including all six seal phases. SourceDirectory
    # can differ from this script's location, so use its matching timing header.
    & $musicPython.Source @musicPythonArguments (Join-Path $SourceDirectory 'scripts\install_eldenring_animations.py') `
        --source $AnimationsSource --timings-header $timingHeader --check-only
    if ($LASTEXITCODE -ne 0) { throw 'Animation validation failed; no source files were copied.' }
}
$musicInstaller = Join-Path $SourceDirectory 'scripts\install_eldenring_music.py'
if ($MusicSource) {
    & $musicPython.Source @musicPythonArguments $musicInstaller --source $MusicSource --check-only
    if ($LASTEXITCODE -ne 0) { throw 'Music validation failed; no source files were copied.' }
}

$soundInstaller = Join-Path $SourceDirectory 'scripts\install_eldenring_sounds.py'
if (-not $SoundsSource) {
    # An old staged dodge library must not hide newly imported block/counter cues.
    $soundCandidates = @(
        (Join-Path $SourceDirectory 'local-assets\sound\eldenring\dodge'),
        (Join-Path $SourceDirectory 'build-eldenring-assets\sound\eldenring\dodge')
    )
    # ASCII-escaped JSON preserves Unicode paths across Python/PowerShell code pages.
    $selectedSounds = & $musicPython.Source @musicPythonArguments $soundInstaller --json --select-source @soundCandidates
    if ($LASTEXITCODE -ne 0) { throw 'Sound selection failed; no source files were copied.' }
    $soundSelection = ($selectedSounds -join "`n") | ConvertFrom-Json
    $SoundsSource = $soundSelection.source
}
if ($SoundsSource) {
    & $musicPython.Source @musicPythonArguments $soundInstaller --source $SoundsSource --check-only
    if ($LASTEXITCODE -ne 0) { throw 'Sound validation failed; no source files were copied.' }
} else {
    Write-Warning 'No imported combat sounds found; import them with scripts/import_eldenring_sounds.py to include them.'
}

# Enumerate excluded directory paths, avoiding traversal into large builds and
# dependency trees. Do not rely on wildcard directory handling by Robocopy.
$excludedNames = @(
    '.git', '.github', '.gitlab', '.hg', '.svn', '.bzr', '.cache', '.codex', '.agents', '.aws',
    '.idea', '.vscode', '.vs', '.ipynb_checkpoints', '__pycache__', '.pytest_cache', '.mypy_cache',
    '.ruff_cache', '.venv', 'venv', 'node_modules', 'deps', 'prebuilt', 'CMakeFiles',
    'local-assets', 'elden-profile', 'profiles', 'saves', 'screenshots'
)
$excludedDirectories = New-Object 'System.Collections.Generic.List[string]'
$pending = New-Object 'System.Collections.Generic.Stack[string]'
$pending.Push($SourceDirectory)
while ($pending.Count -gt 0) {
    $directory = $pending.Pop()
    foreach ($child in (Get-ChildItem -LiteralPath $directory -Directory -Force)) {
        if ($child.Name -in $excludedNames -or $child.Name -like 'build*' -or $child.Name -like 'cmake-build-*' `
            -or $child.Name -like 'MSVC*' -or $child.Name -like 'OpenMW-EldenCombat-Windows-*' `
            -or ($directory -eq $SourceDirectory -and $child.Name -eq 'resources') `
            -or ($child.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            $excludedDirectories.Add($child.FullName)
        } else {
            $pending.Push($child.FullName)
        }
    }
}
$excludedFiles = @(
    '.git*', '.hg*', '.svn*', '.bzr*', '.mailmap', 'CMakeCache.txt', 'cmake_install.cmake',
    'compile_commands.json', '.ninja_deps', '.ninja_log', 'build.ninja', 'rules.ninja',
    '*.pyc', '*.pyo', '*.o', '*.obj', '*.a', '*.so', '*.pdb', '*.dll', '*.exe', '*.lib',
    '*.dmp', '*.omwsave', 'OpenMW-EldenCombat-Windows-*.zip', 'OpenMW-EldenCombat-Windows-*.zip.sha256',
    (Join-Path $SourceDirectory 'Makefile'), (Join-Path $SourceDirectory 'openmw.cfg'),
    (Join-Path $SourceDirectory 'settings.cfg'), (Join-Path $SourceDirectory 'input_v3.xml')
)
$copyArguments = @($SourceDirectory, $DestinationDirectory, '/E', '/XJ', '/COPY:DAT', '/DCOPY:DAT',
    '/R:2', '/W:1', '/IS', '/IT', '/NP', '/NFL', '/NDL')
if ($excludedDirectories.Count -gt 0) { $copyArguments += @('/XD') + $excludedDirectories.ToArray() }
$copyArguments += @('/XF') + $excludedFiles
if ($ListOnly) { $copyArguments += '/L' }

Write-Host "Source: $SourceDirectory"
Write-Host "Destination: $DestinationDirectory"
& $robocopy.Source @copyArguments
$copyExit = $LASTEXITCODE
# Robocopy uses 0-7 for successful copies or nonfatal differences, 8+ for errors.
if ($copyExit -ge 8) { throw "Robocopy failed with exit code $copyExit." }
if ($ListOnly) {
    if ($AnimationsSource) { Write-Host "Would copy animation bank: $AnimationsSource -> local-assets\player_anims.bin" }
    if ($MusicSource) { Write-Host "Would copy regional music: $MusicSource -> local-assets\music\eldenring" }
    if ($SoundsSource) { Write-Host "Would copy combat sounds: $SoundsSource -> local-assets\sound\eldenring\dodge" }
    Write-Host 'List-only run finished; no files were copied.'
    return
}
foreach ($relative in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $DestinationDirectory $relative) -PathType Leaf)) {
        throw "Copied source is incomplete: $relative"
    }
}
if ((Get-FileHash -LiteralPath (Join-Path $DestinationDirectory 'components\eldencombat\imported_timings.hpp') -Algorithm SHA256).Hash -ine $timingHash) {
    throw 'Copied timing header does not match the source; rerun after source edits finish.'
}

if ($AnimationsSource) {
    $assetDirectory = Join-Path $DestinationDirectory 'local-assets'
    $destinationAsset = Join-Path $assetDirectory 'player_anims.bin'
    if ((Test-Path -LiteralPath $destinationAsset) -and -not (Test-Path -LiteralPath $destinationAsset -PathType Leaf)) {
        throw "The animation destination is not a file: $destinationAsset"
    }
    New-Item -ItemType Directory -Path $assetDirectory -Force | Out-Null
    $currentHash = if (Test-Path -LiteralPath $destinationAsset -PathType Leaf) {
        (Get-FileHash -LiteralPath $destinationAsset -Algorithm SHA256).Hash
    } else { '' }
    if ($currentHash -ine $animationHash) {
        $temporary = Join-Path $assetDirectory ('player_anims.' + [Guid]::NewGuid().ToString('N') + '.tmp')
        try {
            Copy-Item -LiteralPath $AnimationsSource -Destination $temporary
            if ((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ine $animationHash) {
                throw 'Animation copy hash mismatch; the source may have changed while copying.'
            }
            if ($currentHash) {
                $backupDirectory = Join-Path $assetDirectory 'backups'
                New-Item -ItemType Directory -Path $backupDirectory -Force | Out-Null
                $backup = Join-Path $backupDirectory ("player_anims-$currentHash.bin")
                if (-not (Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $destinationAsset -Destination $backup }
                if ((Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash -ine $currentHash) {
                    throw 'Animation backup verification failed; replacement cancelled.'
                }
            }
            Move-Item -LiteralPath $temporary -Destination $destinationAsset -Force
        } finally {
            if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
        }
    }
    if ((Get-FileHash -LiteralPath $destinationAsset -Algorithm SHA256).Hash -ine $animationHash) {
        throw 'Copied animation bank failed SHA256 verification.'
    }
    Write-Host "Local animation bank: $destinationAsset"
}
if ($MusicSource) {
    & $musicPython.Source @musicPythonArguments $musicInstaller --source $MusicSource `
        --library (Join-Path $DestinationDirectory 'local-assets')
    if ($LASTEXITCODE -ne 0) { throw 'Regional music copy failed.' }
}
if ($SoundsSource) {
    & $musicPython.Source @musicPythonArguments $soundInstaller --source $SoundsSource `
        --library (Join-Path $DestinationDirectory 'local-assets')
    if ($LASTEXITCODE -ne 0) { throw 'Combat sound copy failed.' }
}
$verifyArguments = @('--source', $SourceDirectory, '--destination', $DestinationDirectory)
if ($AnimationsSource) { $verifyArguments += @('--animations-source', $AnimationsSource) }
if ($MusicSource) { $verifyArguments += @('--music-source', $MusicSource) }
if ($SoundsSource) { $verifyArguments += @('--sounds-source', $SoundsSource) }
& $musicPython.Source @musicPythonArguments (Join-Path $SourceDirectory 'scripts\verify_eldenring_copy.py') @verifyArguments
if ($LASTEXITCODE -ne 0) { throw 'Source/resource copy verification failed.' }
Write-Host 'Source and local asset copy complete. Existing destination builds, profiles and saves were preserved.'
Write-Host 'Build in Windows PowerShell with:'
Write-Host ('powershell -NoProfile -ExecutionPolicy Bypass -File "' + (Join-Path $DestinationDirectory 'CI\eldenring-build-windows.ps1') + '"')
