#Requires -Version 5.1
<#
.SYNOPSIS
Apply the loading-screen crash fix to an existing Windows checkout and rebuild it.
.DESCRIPTION
Uses the existing Visual Studio/CMake build and updates openmw.exe in the unpacked
package. Keeps source/executable backups and preserves the profile and crash dump.
#>
[CmdletBinding()]
param(
    [string]$SourceDirectory = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'openmw'),
    [string]$BuildDirectory = '',
    [string]$PackageDirectory = '',
    [ValidateRange(1, 256)]
    [int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount - 1)
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$SourceDirectory = [IO.Path]::GetFullPath($SourceDirectory)
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $SourceDirectory 'build-eldenring-windows-2026-x64' }
if (-not $PackageDirectory) { $PackageDirectory = Join-Path $SourceDirectory 'OpenMW-EldenCombat-Windows-x64' }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$PackageDirectory = [IO.Path]::GetFullPath($PackageDirectory)
$sourceFile = Join-Path $SourceDirectory 'apps\openmw\mwinput\actionmanager.cpp'
$packageExe = Join-Path $PackageDirectory 'openmw.exe'

# Read UTF-8 explicitly: the checkout/build paths may contain Cyrillic characters.
$cache = @{}
foreach ($line in [IO.File]::ReadAllLines((Join-Path $BuildDirectory 'CMakeCache.txt'))) {
    if ($line -match '^(CMAKE_COMMAND|CMAKE_HOME_DIRECTORY):[^=]+=(.*)$') {
        $cache[$Matches[1]] = $Matches[2]
    }
}
if (-not $cache.ContainsKey('CMAKE_COMMAND') -or -not $cache.ContainsKey('CMAKE_HOME_DIRECTORY')) {
    throw 'The build cache does not identify its CMake executable and source directory.'
}
$cachedSource = [IO.Path]::GetFullPath($cache['CMAKE_HOME_DIRECTORY'])
if ($cachedSource.TrimEnd('\') -ine $SourceDirectory.TrimEnd('\')) {
    throw "This build belongs to $cachedSource, not $SourceDirectory."
}
$cmake = $cache['CMAKE_COMMAND']
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
foreach ($required in @($cmake, $ctest, $sourceFile, $packageExe)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing required file: $required" }
}

$before = @'
    void ActionManager::clearCombatInput()
    {
        mBindingsManager->clearActionButtons();
        const auto world = MWBase::Environment::get().getWorld();
'@
$after = @'
    void ActionManager::clearCombatInput()
    {
        mBindingsManager->clearActionButtons();
        const auto& environment = MWBase::Environment::get();
        // Loading screens pump input before the world has a player or camera.
        // Always clear physical buttons, but only touch actors once a game exists.
        if (environment.getStateManager()->getState() == MWBase::StateManager::State_NoGame)
            return;
        const auto world = environment.getWorld();
'@
$before = $before.Replace("`r`n", "`n")
$after = $after.Replace("`r`n", "`n")
$original = [IO.File]::ReadAllText($sourceFile)
$normalized = $original.Replace("`r`n", "`n")
if ($normalized.Contains($after)) {
    Write-Host 'The startup fix is already applied.'
} elseif ($normalized.Contains($before)) {
    if (-not (Test-Path -LiteralPath "$sourceFile.pre-startup-fix")) {
        Copy-Item -LiteralPath $sourceFile -Destination "$sourceFile.pre-startup-fix"
    }
    $patched = $normalized.Replace($before, $after)
    if ($original.Contains("`r`n")) { $patched = $patched.Replace("`n", "`r`n") }
    [IO.File]::WriteAllText($sourceFile, $patched, (New-Object Text.UTF8Encoding($false)))
    Write-Host "Applied startup fix: $sourceFile"
} else {
    throw 'The source differs from the diagnosed version; no files were overwritten.'
}

Push-Location $SourceDirectory
try {
    & $cmake --build $BuildDirectory --config Release --target openmw --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "OpenMW build failed ($LASTEXITCODE)." }
    & $ctest --test-dir $BuildDirectory -C Release -R '^elden(combat(-imported)?|input)$' --output-on-failure --no-tests=error
    if ($LASTEXITCODE -ne 0) { throw "Combat tests failed ($LASTEXITCODE)." }
} finally {
    Pop-Location
}

$builtExe = Join-Path $BuildDirectory 'Release\openmw.exe'
if (-not (Test-Path -LiteralPath "$packageExe.pre-startup-fix")) {
    Copy-Item -LiteralPath $packageExe -Destination "$packageExe.pre-startup-fix"
}
Copy-Item -LiteralPath $builtExe -Destination $packageExe -Force
if ((Get-FileHash -LiteralPath $builtExe).Hash -ne (Get-FileHash -LiteralPath $packageExe).Hash) {
    throw 'The packaged executable does not match the rebuilt executable.'
}
Write-Host "Updated executable: $packageExe"
Write-Host "Start the game with: $(Join-Path $PackageDirectory 'Start-EldenCombat.cmd')"
