#Requires -Version 5.1
<#
.SYNOPSIS
Build and package this OpenMW source tree for Windows x64.
.DESCRIPTION
Run in Windows PowerShell with Visual Studio 2026's C++ desktop workload and
Windows SDK installed, CMake 4.2 or newer, Python 3.10 or newer, and 7-Zip available. Uses the same
pinned, SHA512-verified OpenMW dependency bundle as the upstream Windows CI.
The default builds the game, ini importer, settings launcher, install wizard and
the launcher's navmesh tool, downloading the pinned Qt SDK if needed. Pass -WithoutLauncher for a game-only
build without Qt. -WithLauncher remains accepted for existing commands.
If the dependency path contains non-ASCII characters, only Qt uses an ASCII
cache under Public Documents\OpenMW-BuildDependencies\Qt to avoid an aqt bug.
The packaged engine is only reported after its PE architecture and --version pass;
launcher builds also verify the launcher, wizard and required Qt runtime files.
Pass -VisualStudioVersion 2022 to use the older toolchain and CMake >= 3.22.
.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File .\CI\eldenring-build-windows.ps1
.EXAMPLE
.\CI\eldenring-build-windows.ps1 -Jobs 8
#>
[CmdletBinding()]
param(
    [ValidateSet('2026', '2022')]
    [string]$VisualStudioVersion = '2026',
    [ValidateSet('Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [string]$BuildDirectory = '',
    [string]$DependencyDirectory = '',
    [string]$InstallDirectory = '',
    [ValidateRange(1, 256)]
    [int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount - 1),
    [switch]$WithLauncher = $true,
    [switch]$WithoutLauncher,
    [switch]$PortableSaves,
    [string]$QtRoot = '',
    [string]$TimingsSource = '',
    [string]$AnimationsSource = '',
    [string]$MusicSource = '',
    [string]$SoundsSource = '',
    [string]$MorrowindDirectory = 'C:\Program Files (x86)\Steam\steamapps\common\Morrowind'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($WithoutLauncher -and $WithLauncher -and $PSBoundParameters.ContainsKey('WithLauncher')) {
    throw 'Choose either -WithLauncher or -WithoutLauncher, not both.'
}
$buildLauncher = $WithLauncher -and -not $WithoutLauncher

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'This script requires native Windows; it cannot use Linux libraries to produce openmw.exe.'
}
if (-not [Environment]::Is64BitOperatingSystem) {
    throw 'A 64-bit Windows host is required.'
}

$sourceRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $sourceRoot "build-eldenring-windows-$VisualStudioVersion-x64" }
if (-not $DependencyDirectory) { $DependencyDirectory = Join-Path $sourceRoot 'deps' }
if (-not $InstallDirectory) { $InstallDirectory = Join-Path $sourceRoot 'build-eldenring-package-x64' }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$DependencyDirectory = [IO.Path]::GetFullPath($DependencyDirectory)
$InstallDirectory = [IO.Path]::GetFullPath($InstallDirectory)

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE."
    }
}

function Get-Download {
    param([string]$Url, [string]$Destination)
    $temporary = "$Destination.partial"
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $client = New-Object Net.WebClient
    try {
        Write-Host "Downloading $Url"
        $client.DownloadFile($Url, $temporary)
        Move-Item -Force $temporary $Destination
    } finally {
        $client.Dispose()
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
    }
}

function Find-Program {
    param([string]$Name, [string[]]$Candidates = @())
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) { return $candidate }
    }
    return $null
}

function Test-WindowsExecutable {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Build completed without $Path."
    }
    $reader = New-Object IO.BinaryReader -ArgumentList ([IO.File]::OpenRead($Path))
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "$Path lacks a DOS executable header." }
        $reader.BaseStream.Position = 0x3C
        $peOffset = $reader.ReadUInt32()
        $reader.BaseStream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -ne 0x8664) {
            throw "$Path is not an AMD64 PE executable."
        }
    } finally { $reader.Dispose() }
}

$vswhere = Find-Program 'vswhere.exe' @(
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe')
)
if (-not $vswhere) {
    throw "Visual Studio $VisualStudioVersion Build Tools are missing. Install the Desktop development with C++ workload and a Windows SDK."
}
if ($VisualStudioVersion -eq '2026') {
    $vsRange = '[18.0,19.0)'
    $generator = 'Visual Studio 18 2026'
    $minimumCmakeVersion = [version]'4.2.0'
} else {
    $vsRange = '[17.0,18.0)'
    $generator = 'Visual Studio 17 2022'
    $minimumCmakeVersion = [version]'3.22.0'
}
$visualStudio = & $vswhere -latest -products '*' -version $vsRange `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $visualStudio) {
    throw "Visual Studio $VisualStudioVersion with the x64 C++ toolchain was not found."
}
$visualStudio = ($visualStudio | Select-Object -First 1).Trim()

# A stale PATH installation must not hide a compatible bundled/standalone CMake.
$cmakeCandidates = @(
    (Find-Program 'cmake.exe'),
    (Join-Path $visualStudio 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'),
    (Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe')
)
$cmake = $null
foreach ($candidate in ($cmakeCandidates | Select-Object -Unique)) {
    if (-not $candidate -or -not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
    $cmakeVersion = @(& $candidate --version)
    if ($LASTEXITCODE -eq 0 -and $cmakeVersion.Length -gt 0 -and
        $cmakeVersion[0] -match 'cmake version ([0-9]+\.[0-9]+\.[0-9]+)' -and
        [version]$Matches[1] -ge $minimumCmakeVersion) {
        $cmake = $candidate
        break
    }
}
$sevenZip = Find-Program '7z.exe' @(
    (Join-Path $env:ProgramFiles '7-Zip\7z.exe'),
    (Join-Path ${env:ProgramFiles(x86)} '7-Zip\7z.exe'),
    (Join-Path $env:ProgramFiles 'NVIDIA Corporation\NVIDIA App\7z.exe')
)
if (-not $cmake) {
    throw "CMake $minimumCmakeVersion or newer is required for Visual Studio $VisualStudioVersion. Install or update Kitware.CMake."
}
if (-not $sevenZip) { throw '7-Zip is missing. Install 7-Zip to extract the upstream dependency bundle.' }
$python = Find-Program 'py.exe'
$pythonArguments = @()
if ($python) { $pythonArguments = @('-3') } else { $python = Find-Program 'python.exe' }
if (-not $python) { throw 'Python 3.10 or newer is required to import combat data and configure the Morrowind profile.' }
Invoke-Checked $python ($pythonArguments + @('-c', 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)'))
foreach ($testScript in @('eldenring-test-profile.py', 'eldenring-test-animation-install.py',
                         'eldenring-test-music-import.py', 'eldenring-test-music-install.py',
                         'eldenring-test-sounds.py', 'eldenring-test-copy.py')) {
    Invoke-Checked $python ($pythonArguments + @((Join-Path $PSScriptRoot $testScript)))
}
Invoke-Checked $python ($pythonArguments + @((Join-Path $PSScriptRoot 'eldenring-test-qt-translations.py'), '--cmake', $cmake))
if (-not (Test-Path -LiteralPath (Join-Path $MorrowindDirectory 'Data Files\Morrowind.esm'))) {
    throw "Morrowind data was not found in $MorrowindDirectory. Supply -MorrowindDirectory with its installation directory."
}
Write-Host "Using Visual Studio ${VisualStudioVersion}: $visualStudio"
Write-Host "Using CMake: $cmake"

# Import the x64 toolchain environment for CMake's runtime dependency scanner.
$vcvars = Join-Path $visualStudio 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars -PathType Leaf)) {
    throw "The Visual Studio x64 toolchain setup script was not found: $vcvars"
}
# Supply CMD's command line directly; PowerShell's native argument quoting can
# add another layer of quotes around a batch file path containing spaces.
# CMD also needs a local working directory when the source tree is on a WSL UNC share.
$vcvarsStartInfo = New-Object Diagnostics.ProcessStartInfo
$vcvarsStartInfo.FileName = $env:COMSPEC
$vcvarsStartInfo.Arguments = '/d /u /s /c ""' + $vcvars + '" >nul && set"'
$vcvarsStartInfo.WorkingDirectory = $visualStudio
$vcvarsStartInfo.UseShellExecute = $false
$vcvarsStartInfo.RedirectStandardOutput = $true
$vcvarsStartInfo.RedirectStandardError = $true
# CMD's internal SET uses UTF-16 with /u, preserving paths in USERPROFILE and TEMP.
$vcvarsStartInfo.StandardOutputEncoding = [Text.Encoding]::Unicode
$vcvarsStartInfo.CreateNoWindow = $true
$vcvarsProcess = New-Object Diagnostics.Process
$vcvarsProcess.StartInfo = $vcvarsStartInfo
try {
    $null = $vcvarsProcess.Start()
    # Drain both streams concurrently so a full pipe cannot block CMD.
    $vcvarsOutputTask = $vcvarsProcess.StandardOutput.ReadToEndAsync()
    $vcvarsErrorTask = $vcvarsProcess.StandardError.ReadToEndAsync()
    $vcvarsProcess.WaitForExit()
    $environmentLines = $vcvarsOutputTask.GetAwaiter().GetResult() -split '\r?\n'
    $vcvarsError = $vcvarsErrorTask.GetAwaiter().GetResult()
    if ($vcvarsProcess.ExitCode -ne 0) {
        throw "Could not activate the Visual Studio x64 toolchain (exit code $($vcvarsProcess.ExitCode)). $vcvarsError"
    }
} finally {
    $vcvarsProcess.Dispose()
}
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}

$versions = @{}
Get-Content (Join-Path $PSScriptRoot 'deps_versions.msvc.sh') | ForEach-Object {
    if ($_ -match '^([A-Z_]+)=([0-9][0-9.-]*)$') { $versions[$Matches[1]] = $Matches[2] }
}
if (-not $versions.ContainsKey('VCPKG_TAG') -or -not $versions.ContainsKey('QT_VER')) {
    throw 'The pinned dependency versions could not be read.'
}

New-Item -ItemType Directory -Force -Path $DependencyDirectory, $BuildDirectory, $InstallDirectory | Out-Null
# Upstream uses this v143 bundle with VS 2026/v145 as well. Keep the newer
# linker/runtime and disable LTO; do not force the v143 compiler toolset.
$dependencyName = "vcpkg-x64-windows-2022-$($versions['VCPKG_TAG'])"
$dependencyRoot = Join-Path $DependencyDirectory $dependencyName
$toolchain = Join-Path $dependencyRoot 'scripts\buildsystems\vcpkg.cmake'
if (-not (Test-Path -LiteralPath $toolchain)) {
    $manifestFile = Join-Path $DependencyDirectory "$dependencyName-manifest.txt"
    Get-Download "https://gitlab.com/OpenMW/openmw-deps/-/raw/main/windows/$dependencyName-manifest.txt" $manifestFile
    $manifest = @(Get-Content $manifestFile | Where-Object { $_.Trim().Length -gt 0 })
    if ($manifest.Length -ne 2 -or $manifest[0] -notmatch '^https://' -or
        $manifest[1] -notmatch '^([a-fA-F0-9]{128})\s+\*?([^\\/]+)$') {
        throw 'The upstream dependency manifest has an unexpected format.'
    }
    $expectedHash = $Matches[1]
    $archiveFile = Join-Path $DependencyDirectory $Matches[2]
    if (-not (Test-Path -LiteralPath $archiveFile)) { Get-Download $manifest[0] $archiveFile }
    $actualHash = (Get-FileHash -LiteralPath $archiveFile -Algorithm SHA512).Hash
    if ($actualHash -ne $expectedHash) {
        throw "SHA512 mismatch for $archiveFile. Delete this archive and rerun."
    }
    Invoke-Checked $sevenZip @('x', '-y', "-o$dependencyRoot", $archiveFile)
    if (-not (Test-Path -LiteralPath $toolchain)) { throw 'The extracted dependency bundle does not contain the vcpkg toolchain.' }
}
$installedDependencies = Join-Path $dependencyRoot 'installed\x64-windows'

if ($TimingsSource -and -not (Test-Path -LiteralPath $TimingsSource -PathType Leaf)) {
    throw "The supplied timing source does not exist: $TimingsSource"
}
if (-not $TimingsSource) {
    $defaultTimings = Join-Path (Split-Path -Parent $sourceRoot) 'ELDEN-RING-Combat-Rewrite\src\sim\extracted.rs'
    if (Test-Path -LiteralPath $defaultTimings -PathType Leaf) { $TimingsSource = $defaultTimings }
}
if ($TimingsSource) {
    Invoke-Checked $python ($pythonArguments + @((Join-Path $sourceRoot 'scripts\import_eldenring_timings.py'), $TimingsSource))
}

$animationInstaller = Join-Path $sourceRoot 'scripts\install_eldenring_animations.py'
if ($AnimationsSource -and -not (Test-Path -LiteralPath $AnimationsSource -PathType Leaf)) {
    throw "The supplied animation bank does not exist: $AnimationsSource"
}
if (-not $AnimationsSource) {
    foreach ($candidate in @(
        (Join-Path $sourceRoot 'local-assets\player_anims.bin'),
        (Join-Path (Split-Path -Parent $sourceRoot) 'ELDEN-RING-Combat-Rewrite\assets\player_anims.bin'),
        (Join-Path $InstallDirectory 'resources\vfs\animations\eldencombat\player_anims.bin')
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $AnimationsSource = $candidate; break }
    }
}
$animationHash = ''
if ($AnimationsSource) {
    $animationJson = Invoke-Checked $python ($pythonArguments + @($animationInstaller, '--source', $AnimationsSource, '--check-only', '--json'))
    $animationValidation = ($animationJson -join "`n") | ConvertFrom-Json
    $animationHash = $animationValidation.sha256
} else {
    Write-Warning 'No player_anims.bin was found. The package will use native Morrowind animations. Supply -AnimationsSource with your locally baked bank, or place it in local-assets\player_anims.bin or the sibling ELDEN-RING-Combat-Rewrite\assets directory.'
}

if ($MusicSource -and -not (Test-Path -LiteralPath $MusicSource -PathType Container)) {
    throw "The supplied music source does not exist: $MusicSource"
}
if (-not $MusicSource) {
    foreach ($candidate in @(
        (Join-Path $sourceRoot 'local-assets\music\eldenring'),
        (Join-Path $sourceRoot 'build-eldenring-assets\music\eldenring'),
        (Join-Path $InstallDirectory 'resources\vfs\music\eldenring')
    )) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'manifest.json') -PathType Leaf) {
            $MusicSource = $candidate
            break
        }
    }
}
$musicInstaller = Join-Path $sourceRoot 'scripts\install_eldenring_music.py'
$musicHash = ''
if ($MusicSource) {
    $musicJson = Invoke-Checked $python ($pythonArguments + @($musicInstaller, '--source', $MusicSource, '--check-only', '--json'))
    $musicValidation = ($musicJson -join "`n") | ConvertFrom-Json
    $musicHash = $musicValidation.manifest_sha256
} else {
    Write-Host 'No imported regional music found. Import it with scripts\import_eldenring_music.py or supply -MusicSource. The music toggle will fall back to Morrowind music until tracks are installed.'
}

if ($SoundsSource -and -not (Test-Path -LiteralPath $SoundsSource -PathType Container)) {
    throw "The supplied sound source does not exist: $SoundsSource"
}
$soundInstaller = Join-Path $sourceRoot 'scripts\install_eldenring_sounds.py'
if (-not $SoundsSource) {
    $soundCandidates = @(
        (Join-Path $sourceRoot 'local-assets\sound\eldenring\dodge'),
        (Join-Path $sourceRoot 'build-eldenring-assets\sound\eldenring\dodge'),
        (Join-Path $InstallDirectory 'resources\vfs\sound\eldenring\dodge')
    )
    # ASCII-escaped JSON preserves Unicode paths across Python/PowerShell code pages.
    $selectedSounds = Invoke-Checked $python ($pythonArguments + @($soundInstaller, '--json', '--select-source') + $soundCandidates)
    $soundSelection = ($selectedSounds -join "`n") | ConvertFrom-Json
    $SoundsSource = $soundSelection.source
}
$soundHash = ''
if ($SoundsSource) {
    $soundJson = Invoke-Checked $python ($pythonArguments + @($soundInstaller, '--source', $SoundsSource, '--check-only', '--json'))
    $soundValidation = ($soundJson -join "`n") | ConvertFrom-Json
    $soundHash = $soundValidation.manifest_sha256
    if ('guard-counter.ogg' -notin @($soundValidation.sounds.file)) {
        Write-Warning 'The sound library contains only dodge cues. Re-import with scripts\import_eldenring_sounds.py and recopy assets to add block and guard-counter sounds.'
    }
} else {
    Write-Warning 'No imported combat sounds found. Recopy the WSL source/assets with the updated copy-to-documents.sh or CI\eldenring-copy-to-documents.ps1, or supply -SoundsSource. Rolls, backsteps and guard counters will have no imported sound until the recordings are installed; blocks use the native impact fallback.'
}

if ($buildLauncher) {
    $qtRuntimeDlls = @('Qt6Core', 'Qt6Gui', 'Qt6Network', 'Qt6OpenGL', 'Qt6OpenGLWidgets', 'Qt6Widgets', 'Qt6Svg')
    $qtPlugins = @('platforms\qwindows.dll', 'imageformats\qsvg.dll', 'iconengines\qsvgicon.dll')
    $qtRequired = @('lib\cmake\Qt6\Qt6Config.cmake', 'lib\cmake\Qt6LinguistTools\Qt6LinguistToolsConfig.cmake') +
        @('qmake', 'moc', 'uic', 'rcc', 'lrelease' | ForEach-Object { "bin\$_.exe" }) +
        @($qtRuntimeDlls | ForEach-Object { "bin\$_.dll"; "lib\$_.lib" }) +
        @($qtPlugins | ForEach-Object { "plugins\$_" })
    $qtInstallMarker = ''
    if (-not $QtRoot) {
        $qtDirectory = Join-Path $DependencyDirectory 'Qt'
        # aqt 3.1.15 decodes qmake -query as UTF-8, but Qt 6.6 on Windows emits
        # the ANSI code page. Keep only the managed Qt SDK at an ASCII path;
        # the source, other dependencies, build and package can stay in the profile.
        if ($qtDirectory -match '[^\x00-\x7F]') {
            $publicDocuments = [Environment]::GetFolderPath([Environment+SpecialFolder]::CommonDocuments)
            if (-not $publicDocuments -or $publicDocuments -match '[^\x00-\x7F]') {
                throw 'The Qt installer requires an ASCII path. Supply -DependencyDirectory with an ASCII path, or -QtRoot with a complete existing Qt SDK.'
            }
            $qtDirectory = Join-Path $publicDocuments 'OpenMW-BuildDependencies\Qt'
            Write-Host "Using the ASCII Qt SDK cache to avoid the installer's Unicode path error: $qtDirectory"
        }
        $QtRoot = Join-Path $qtDirectory "$($versions['QT_VER'])\msvc2019_64"
        $qtInstallMarker = Join-Path $QtRoot '.openmw-aqt-3.1.15-complete'
        $managedQtRequired = $qtRequired + @('bin\qt.conf', 'plugins\styles\qwindowsvistastyle.dll', 'translations\qtbase_bg.qm')
        $missingQtFiles = @($managedQtRequired | Where-Object {
            -not (Test-Path -LiteralPath (Join-Path $QtRoot $_) -PathType Leaf)
        })
        # Qt6Config.cmake is extracted before aqt's post-install patching. Its
        # presence alone must not make a failed installation look complete.
        if (-not (Test-Path -LiteralPath $qtInstallMarker -PathType Leaf) -or $missingQtFiles.Length -gt 0) {
            try {
                New-Item -ItemType Directory -Force -Path $qtDirectory | Out-Null
            } catch {
                throw "Could not create the Qt SDK cache at $qtDirectory. Supply -DependencyDirectory with a writable ASCII path, or -QtRoot with a complete existing Qt SDK. $($_.Exception.Message)"
            }
            if (Test-Path -LiteralPath $qtInstallMarker) { Remove-Item -LiteralPath $qtInstallMarker -Force }
            $aqt = Join-Path $qtDirectory 'aqt_x64.exe'
            if (-not (Test-Path -LiteralPath $aqt)) {
                Get-Download 'https://github.com/miurahr/aqtinstall/releases/download/v3.1.15/aqt_x64.exe' $aqt
            }
            Invoke-Checked $aqt @('install-qt', 'windows', 'desktop', $versions['QT_VER'], 'win64_msvc2019_64', '--outputdir', $qtDirectory)
        }
        $qtRequired = $managedQtRequired
    }
    $QtRoot = [IO.Path]::GetFullPath($QtRoot)
    # Upstream renamed the Windows style plugin in Qt 6.7; also support a newer -QtRoot.
    $qtStyle = @('styles\qmodernwindowsstyle.dll', 'styles\qwindowsvistastyle.dll') |
        Where-Object { Test-Path -LiteralPath (Join-Path $QtRoot "plugins\$_") -PathType Leaf } |
        Select-Object -First 1
    if (-not $qtStyle) { throw "The Qt SDK is missing its Windows style plugin: $QtRoot" }
    $qtPlugins += $qtStyle
    $qtRequired += "plugins\$qtStyle"
    foreach ($relative in $qtRequired) {
        if (-not (Test-Path -LiteralPath (Join-Path $QtRoot $relative) -PathType Leaf)) {
            throw "The Qt SDK is incomplete: $(Join-Path $QtRoot $relative)"
        }
    }
    $qtTranslations = Join-Path $QtRoot 'translations'
    if (-not (Test-Path -LiteralPath $qtTranslations -PathType Container) -or
        @(Get-ChildItem -LiteralPath $qtTranslations -Filter '*.qm' -File).Length -eq 0) {
        throw "The Qt SDK is missing its compiled translations: $qtTranslations"
    }
    if ($qtInstallMarker) {
        Set-Content -LiteralPath $qtInstallMarker -Encoding ASCII -Value "aqt 3.1.15; Qt $($versions['QT_VER']); win64_msvc2019_64"
    }
}

$qtEnabled = if ($buildLauncher) { 'ON' } else { 'OFF' }
$configureArguments = @(
    '-S', $sourceRoot, '-B', $BuildDirectory, '-G', $generator, '-A', 'x64',
    '-D', "CMAKE_GENERATOR_INSTANCE=$visualStudio",
    '-D', "CMAKE_TOOLCHAIN_FILE=$toolchain",
    '-D', 'VCPKG_TARGET_TRIPLET=x64-windows',
    '-D', "CMAKE_INSTALL_PREFIX=$InstallDirectory",
    '-D', "LuaJit_INCLUDE_DIR=$installedDependencies/include/luajit",
    '-D', "LuaJit_LIBRARY=$installedDependencies/lib/lua51.lib",
    '-D', 'BUILD_OPENMW=ON',
    '-D', "BUILD_LAUNCHER=$qtEnabled", '-D', "BUILD_WIZARD=$qtEnabled",
    '-D', 'BUILD_MWINIIMPORTER=ON', '-D', 'BUILD_OPENCS=OFF',
    '-D', 'BUILD_ESSIMPORTER=OFF', '-D', 'BUILD_BSATOOL=OFF', '-D', 'BUILD_ESMTOOL=OFF',
    '-D', 'BUILD_NIFTEST=OFF', '-D', "BUILD_NAVMESHTOOL=$qtEnabled", '-D', 'BUILD_BULLETOBJECTTOOL=OFF',
    '-D', 'BUILD_COMPONENTS_TESTS=OFF', '-D', 'BUILD_OPENMW_TESTS=OFF', '-D', 'BUILD_OPENCS_TESTS=OFF',
    '-D', 'BUILD_BENCHMARKS=OFF', '-D', 'BUILD_ELDENCOMBAT_TESTS=ON', '-D', 'OPENMW_USE_SYSTEM_SQLITE3=OFF',
    '-D', 'OPENMW_USE_SYSTEM_YAML_CPP=OFF', '-D', 'OPENMW_DEPLOY_RUNTIME_DEPENDENCIES=ON',
    '-D', 'OPENMW_LTO_BUILD=OFF', '-D', "DEPLOY_QT_TRANSLATIONS=$qtEnabled"
)
if ($buildLauncher) {
    # A previous configure may still cache the old SDK even after the prefix
    # changes (for example when recovering a failed install in a Unicode path).
    $configureArguments += @('-U', 'QT_DIR', '-U', 'Qt6*_DIR',
        '-D', "CMAKE_PREFIX_PATH=$QtRoot", '-D', "QT_TRANSLATIONS_DIR:PATH=$qtTranslations")
}
Invoke-Checked $cmake $configureArguments
$buildLog = Join-Path $BuildDirectory "eldenring-build-$Configuration.log"
$buildErrorLog = Join-Path $BuildDirectory "eldenring-build-$Configuration-errors.log"
# Let MSBuild write UTF-8 logs directly, preserving diagnostics in localized
# Windows installations without piping native stderr through PowerShell 5.1.
# Keep each logger switch as one argument, including paths containing spaces.
foreach ($log in @($buildLog, $buildErrorLog)) {
    if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }
}
Write-Host "Build log: $buildLog"
Write-Host "Build errors: $buildErrorLog"
$buildArguments = @('--build', $BuildDirectory, '--config', $Configuration, '--parallel', "$Jobs", '--',
    '/fl1', "/flp1:LogFile=$buildLog;Verbosity=normal;Encoding=UTF-8",
    '/fl2', "/flp2:LogFile=$buildErrorLog;ErrorsOnly;Encoding=UTF-8")
& $cmake @buildArguments
$buildExitCode = $LASTEXITCODE
if ($buildExitCode -ne 0) {
    $buildErrors = ''
    if (Test-Path -LiteralPath $buildErrorLog -PathType Leaf) {
        $buildErrors = @(Get-Content -LiteralPath $buildErrorLog -Encoding UTF8 -TotalCount 20 -ErrorAction SilentlyContinue) -join "`n"
    }
    if ($buildErrors.Trim()) {
        Write-Host "First build errors:`n$buildErrors"
    } else {
        Write-Host 'MSBuild did not record an error. Check the build log and preceding CMake output.'
    }
    throw "Windows build failed with exit code $buildExitCode. Error log: $buildErrorLog`nFull build log: $buildLog"
}

# These runtime DLLs are not all exposed as imported CMake targets upstream.
# Stage them before tests so a test failure leaves the engine's DLLs beside it.
$binaryDirectory = Join-Path $BuildDirectory $Configuration
foreach ($pluginDirectory in @('platforms', 'styles', 'imageformats', 'iconengines')) {
    New-Item -ItemType Directory -Force -Path (Join-Path $binaryDirectory $pluginDirectory) | Out-Null
}
$dependencyBin = Join-Path $installedDependencies 'bin'
Get-ChildItem -LiteralPath $dependencyBin -Filter '*.dll' -File | Copy-Item -Destination $binaryDirectory -Force
$myGui = Join-Path $dependencyBin 'Release\MyGUIEngine.dll'
if (-not (Test-Path -LiteralPath $myGui -PathType Leaf)) {
    throw "The dependency bundle is missing the required MyGUI runtime: $myGui"
}
Copy-Item -LiteralPath $myGui -Destination $binaryDirectory -Force
Get-ChildItem -LiteralPath $dependencyBin -Directory -Filter 'osgPlugins-*' | ForEach-Object {
    Get-ChildItem -LiteralPath $_.FullName -Filter '*.dll' -File | Copy-Item -Destination $binaryDirectory -Force
}
if ($buildLauncher) {
    foreach ($dll in $qtRuntimeDlls) {
        Copy-Item -LiteralPath (Join-Path $QtRoot "bin\$dll.dll") -Destination $binaryDirectory -Force
    }
    foreach ($plugin in $qtPlugins) {
        $pluginSource = Join-Path $QtRoot "plugins\$plugin"
        $pluginDestination = Join-Path $binaryDirectory (Split-Path -Parent $plugin)
        New-Item -ItemType Directory -Force -Path $pluginDestination | Out-Null
        Copy-Item -LiteralPath $pluginSource -Destination $pluginDestination -Force
    }
}
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctest)) { throw 'ctest.exe is missing from the CMake installation.' }
Invoke-Checked $ctest @('--test-dir', $BuildDirectory, '-C', $Configuration, '-R', '^elden((combat|ai|damage)(-imported)?|input|animation|locomotion|navigation|spear|retarget|grip|jump|spellcasting)$', '--output-on-failure', '--no-tests=error')
if ($AnimationsSource) {
    # Verify the incantation clips and their release frames in the packaged bank.
    Invoke-Checked (Join-Path $binaryDirectory 'eldenspellcasting-tests.exe') @($AnimationsSource)
    # Check arm/knee/foot reconstruction in actual combat clips and movement loops.
    Invoke-Checked (Join-Path $binaryDirectory 'eldenretarget-tests.exe') @($AnimationsSource)
    # Exercise the production retargeter against imported finger stances and
    # every frame of the one-handed and two-handed spear heavy attacks as well.
    Invoke-Checked (Join-Path $binaryDirectory 'eldenspear-tests.exe') @($AnimationsSource)
}
Invoke-Checked $cmake @('--install', $BuildDirectory, '--config', $Configuration, '--prefix', $InstallDirectory)
if ($AnimationsSource) {
    Invoke-Checked $python ($pythonArguments + @($animationInstaller, '--source', $AnimationsSource,
        '--source-sha256', $animationHash, '--package', $InstallDirectory))
}
if ($MusicSource) {
    Invoke-Checked $python ($pythonArguments + @($musicInstaller, '--source', $MusicSource,
        '--source-sha256', $musicHash, '--package', $InstallDirectory))
}
if ($SoundsSource) {
    Invoke-Checked $python ($pythonArguments + @($soundInstaller, '--source', $SoundsSource,
        '--source-sha256', $soundHash, '--package', $InstallDirectory))
}
Copy-Item -LiteralPath $myGui -Destination $InstallDirectory -Force
Copy-Item -LiteralPath (Join-Path $sourceRoot 'components\eldencombat\LICENSE-MIT.txt') `
    -Destination (Join-Path $InstallDirectory 'EldenCombat-LICENSE-MIT.txt') -Force
Copy-Item -LiteralPath (Join-Path $sourceRoot 'components\eldencombat\README.txt') `
    -Destination (Join-Path $InstallDirectory 'EldenCombat-README.txt') -Force
Copy-Item -LiteralPath (Join-Path $sourceRoot 'ELDEN-COMBAT.txt') `
    -Destination $InstallDirectory -Force

# Bundle the redistributable runtime DLLs supplied with this compiler.
$runtimeRoot = [Environment]::GetEnvironmentVariable('VCToolsRedistDir')
if (-not $runtimeRoot) { throw 'The Visual C++ runtime redistributable directory was not found.' }
$runtimeDlls = @(Get-ChildItem (Join-Path $runtimeRoot 'x64\Microsoft.VC*.CRT\*.dll') -File)
if ($runtimeDlls.Length -eq 0) { throw 'No redistributable x64 Visual C++ runtime DLLs were found.' }
$runtimeDlls | Copy-Item -Destination $InstallDirectory -Force
$executable = Join-Path $InstallDirectory 'openmw.exe'
Test-WindowsExecutable $executable
Test-WindowsExecutable (Join-Path $InstallDirectory 'openmw-iniimporter.exe')
$packageRequired = @('defaults.bin', 'resources\vfs\builtin.omwscripts')
if ($buildLauncher) {
    Test-WindowsExecutable (Join-Path $InstallDirectory 'openmw-launcher.exe')
    Test-WindowsExecutable (Join-Path $InstallDirectory 'openmw-wizard.exe')
    Test-WindowsExecutable (Join-Path $InstallDirectory 'openmw-navmeshtool.exe')
    $packageRequired += @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Svg.dll') + $qtPlugins
}
foreach ($relative in $packageRequired) {
    if (-not (Test-Path -LiteralPath (Join-Path $InstallDirectory $relative) -PathType Leaf)) {
        throw "The runtime package is incomplete: $(Join-Path $InstallDirectory $relative)"
    }
}
Push-Location $InstallDirectory
# Parse a real package path as well as --version to catch Unicode argument failures.
try { Invoke-Checked $executable @('--version', '--config', (Join-Path $InstallDirectory 'elden-profile')) } finally { Pop-Location }

# Use a separate profile so existing OpenMW key bindings do not override the
# Elden defaults, and configure the supplied installation without a wizard.
$profileConfig = Join-Path $InstallDirectory 'elden-profile\openmw.cfg'
$profileAlreadyConfigured = Test-Path -LiteralPath $profileConfig -PathType Leaf
$profileArguments = $pythonArguments + @((Join-Path $sourceRoot 'scripts\configure_eldenring_profile.py'),
    '--package', $InstallDirectory, '--morrowind', $MorrowindDirectory, '--target', 'windows')
if ($PortableSaves) { $profileArguments += '--portable-saves' }
Invoke-Checked $python $profileArguments
$morrowindIni = Join-Path $MorrowindDirectory 'Morrowind.ini'
if (-not $profileAlreadyConfigured -and (Test-Path -LiteralPath $morrowindIni)) {
    Invoke-Checked (Join-Path $InstallDirectory 'openmw-iniimporter.exe') @(
        '--ini', $morrowindIni, '--cfg', $profileConfig, '--output', $profileConfig, '--no-archives')
}
Invoke-Checked (Join-Path $InstallDirectory 'Start-EldenCombat.cmd') @('--version')

$archive = Join-Path (Split-Path -Parent $InstallDirectory) 'OpenMW-EldenCombat-Windows-x64.zip'
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
Invoke-Checked $sevenZip @('a', '-tzip', '-mx=5', $archive, "$InstallDirectory\*")
$archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath "$archive.sha256" -Encoding ASCII -Value "$archiveHash  $([IO.Path]::GetFileName($archive))"
Write-Host "Windows x64 executable: $executable"
Write-Host "Runtime package: $archive"
Write-Host 'Run openmw.exe or Start-EldenCombat.cmd in the package to play with the configured Morrowind installation and separate controls.'
if ($buildLauncher) {
    Write-Host 'Run openmw-launcher.exe or Start-EldenCombat-Launcher.cmd in the package to change graphics, data files and game settings.'
}
if ($profileAlreadyConfigured) {
    Write-Host 'Existing profile preferences were preserved; see the profile output above for its save location.'
} elseif ($PortableSaves) {
    Write-Host 'Portable saves: elden-profile\user-data\saves inside the package.'
} else {
    Write-Host 'See the profile output above for the selected saved-game folder (current or previous Windows Documents).'
}
