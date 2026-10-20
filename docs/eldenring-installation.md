# Installation details

For the short Windows release setup, follow the [README](../README.md#windows-x64-download-and-installation).
After cloning, run commands from the `openmw-eldenring` repository root.


## Windows release ZIP

Before extracting, compare this PowerShell result with the supplied `.sha256` file:

```powershell
Get-FileHash .\OpenMW-EldenCombat-Windows-x64.zip -Algorithm SHA256
```

Extract the complete archive into a writable folder outside `Program Files`.
Open PowerShell in that extracted folder. These commands perform the same setup
as the double-click entry points; replace both example paths with your own:

```powershell
.\Setup-EldenCombat.cmd -MorrowindDirectory 'D:\Games\Morrowind'
.\Import-EldenRing.cmd -EldenRingDirectory 'D:\Steam\steamapps\common\ELDEN RING\Game'
.\Start-EldenCombat-Launcher.cmd
.\Start-EldenCombat.cmd
```

Run each command only after the preceding one succeeds. If setup or extraction
fails, correct the reported path/prerequisite and repeat that step. No compiler
is needed. Import uses Python 3.14 and a private Python environment under `.elden-assets` and
reads the Oodle DLL from your own Elden Ring installation; that DLL is never
bundled. The included public archive keys are licensed format tooling.

Settings, controls and content order live in `elden-profile`. Saves use your
Windows Documents folder's `My Games\OpenMW\saves`, including Documents
redirection. For a new installation with separate saves, add `-PortableSaves`
to setup. For a Russian Morrowind installation use `-GameEncoding win1251`.
Rerunning setup preserves an existing configured profile.

To update, extract the new ZIP into a new folder, copy your `elden-profile`
from the old installation, and run `Import-EldenRing.cmd` again. Keep the old
folder until the new installation works. On another computer, create a new
profile so it selects that computer's Morrowind path.

## Windows source build

Source builds require Git, 64-bit Python 3.10+, 7-Zip, Visual Studio 2026 with
**Desktop development with C++** and a Windows SDK, and CMake 4.2+.

```powershell
git clone --branch elden-combat https://github.com/Smthri/openmw-eldenring.git
cd openmw-eldenring
powershell -NoProfile -ExecutionPolicy Bypass -File .\CI\eldenring-build-windows.ps1 `
  -MorrowindDirectory 'D:\Games\Morrowind'
```

The build helper produces a configured local package. For the complete locally
compiled timing path, prepare and generate the Elden Ring data before building:

```powershell
$env:ER_GAME_DIR = 'D:\Steam\steamapps\common\ELDEN RING\Game'
py -3 -m venv .venv
$python = Join-Path $PWD '.venv\Scripts\python.exe'
& $python -m pip install cryptography zstandard
& $python scripts/prepare_eldenring_reference.py
git clone https://github.com/JKAnderson/BinderKeys.git build-tools-local/elden-assets/BinderKeys
& $python scripts/unpack_eldenring_assets.py --game-dir $env:ER_GAME_DIR `
  --keys-dir build-tools-local/elden-assets/BinderKeys/EldenRing_PC/Key --generate
& $python scripts/import_eldenring_music.py --game-dir $env:ER_GAME_DIR
& $python scripts/import_eldenring_sounds.py --game-dir $env:ER_GAME_DIR
```

Re-run the build command after generation. This local build embeds extracted
timings and includes generated assets, so **do not distribute its executable
or ZIP**. For a shareable engine use a clean source snapshot, an empty install
directory, `-ShareableRelease`, and `scripts/package_eldenring_release.py`.
That mode explicitly disables compiled timing data and excludes personal
configuration. Keep the matching source archive and dependency license notices
with the release.

## Get the source for Linux / WSL

```sh
git clone --branch elden-combat https://github.com/Smthri/openmw-eldenring.git
cd openmw-eldenring
```

Use a separate build directory and runtime package. These instructions build
OpenMW itself; Rust and Bevy are not required. The commands below build a Linux
executable, including when run under WSL. For a Windows executable, use the
README's native PowerShell instructions.

Python 3.10 or newer and Git are required. The extraction scripts need the
Python `cryptography` and `zstandard` packages. The preparation helper downloads
the tested combat reference version and applies the included
[source patch](../scripts/eldenring_reference.patch) for native
Linux decompression, newer regulation containers, spell animations, and
conditional attack events. It leaves generated game data in the neighboring
checkout, outside this repository.

## Generate Elden Ring data

Use an x86-64 Linux host with Python's venv support, a C++ compiler, CMake,
and Ninja installed. Replace the two game paths below with your installations.

```sh
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install cryptography zstandard
python3 scripts/prepare_eldenring_reference.py --destination ../ELDEN-RING-Combat-Rewrite

export ER_GAME_DIR="/path/to/ELDEN RING/Game"
export MORROWIND_DIR="/path/to/Morrowind"
bash CI/eldenring-build-asset-decoder.sh
export ER_KRAKEN_LIBRARY="$PWD/build-tools-local/elden-assets/build/liblinoodle.so"

python3 scripts/unpack_eldenring_assets.py \
  --game-dir "$ER_GAME_DIR" \
  --keys-dir build-tools-local/elden-assets/BinderKeys/EldenRing_PC/Key \
  --reference-dir ../ELDEN-RING-Combat-Rewrite --generate
python3 scripts/import_eldenring_timings.py \
  ../ELDEN-RING-Combat-Rewrite/src/sim/extracted.rs
```

The decoder helper fetches pinned open-source dependencies. It reads
`oo2core_6_win64.dll` from your installed game without copying it into the
repository. This Linux path does not need Wine. The unpacker reads the game
installation and writes selected archives and generated data to the neighboring
checkout; it does not patch the installed game.

For optional music and combat sounds, run:

```sh
python3 scripts/import_eldenring_music.py --game-dir "$ER_GAME_DIR"
python3 scripts/import_eldenring_sounds.py --game-dir "$ER_GAME_DIR"
```

## Build and install on Linux

Install the development dependencies described in
[OpenMW's build guide](https://wiki.openmw.org/index.php?title=Development_Environment_Setup).
This source tree uses C++20 and needs CMake, Ninja, OpenSceneGraph, MyGUI,
SDL2, OpenAL, FFmpeg, Bullet, Boost, Lua/LuaJIT, SQLite, ICU, yaml-cpp,
and the other dependencies checked by CMake. The launcher and wizard also
require Qt 6. Use CMake 3.22 or newer for the local install below.

Generate/import the optional timing data **before** configuring the build.
If you skipped extraction, set `MORROWIND_DIR` to your Morrowind installation
for the profile command below. Then build a package with all runtime files
in one directory:

```sh
cmake -S . -B build-eldenring-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DOPENMW_LOCAL_INSTALL=ON \
  -DBUILD_OPENCS=OFF \
  -DBUILD_ELDENCOMBAT_TESTS=ON
cmake --build build-eldenring-linux --parallel
ctest --test-dir build-eldenring-linux --output-on-failure -R '^elden'
cmake --install build-eldenring-linux --prefix "$PWD/build-eldenring-package-linux"
```

If you generated the optional assets, install them into that package:

```sh
python3 scripts/install_eldenring_animations.py \
  --source ../ELDEN-RING-Combat-Rewrite/assets/player_anims.bin \
  --package build-eldenring-package-linux
python3 scripts/install_eldenring_music.py \
  --source build-eldenring-assets/music/eldenring \
  --package build-eldenring-package-linux
python3 scripts/install_eldenring_sounds.py \
  --source build-eldenring-assets/sound/eldenring/dodge \
  --package build-eldenring-package-linux
```

Skip any installer whose corresponding assets you did not generate. Configure
the Morrowind installation and start the game:

```sh
python3 scripts/configure_eldenring_profile.py \
  --package build-eldenring-package-linux \
  --morrowind "$MORROWIND_DIR" --target linux
./build-eldenring-package-linux/Start-EldenCombat.sh
```

The profile helper accepts either the Morrowind directory or its `Data Files`
directory. It creates a separate combat configuration and preserves an
existing profile. Saves normally use OpenMW's standard user-data location;
pass `--portable-saves` when creating a profile to keep saves inside the package.

## Windows build options

With Visual Studio 2022, install CMake 3.22+ and use
`-VisualStudioVersion 2022` in the README's build command.

The build helper accepts `-Jobs 8`, `-WithoutLauncher`, `-PortableSaves`,
`-TimingsSource`, `-AnimationsSource`, `-MusicSource`, and `-SoundsSource`.
It automatically imports timings and installs animations from the neighboring
`ELDEN-RING-Combat-Rewrite` checkout, and installs music/sounds from
`build-eldenring-assets`. Existing profile settings are preserved; saves use
OpenMW's standard user-data location unless `-PortableSaves` is selected.

To build with native animations and fallback timings, skip game-data generation
on a fresh clone and run the build helper with your Morrowind installation path.
Elden Ring is only required for generating its optional data.

## Optional interior music classification

Before building, you can generate the interior lookup from your own
Morrowind, Tribunal, and Bloodmoon files. This command requires all three
master files to be present in `Data Files`:

```sh
python3 scripts/eldenring_music/generate_cells.py \
  --data-dir "$MORROWIND_DIR/Data Files" \
  --output files/data-mw/scripts/omw/music/eldenring_cells.lua
```

On Windows, run this in the same PowerShell session as the README, before building:

```powershell
& $python scripts/eldenring_music/generate_cells.py --data-dir "$morrowind\Data Files" --output files/data-mw/scripts/omw/music/eldenring_cells.lua
```

This generated lookup stays ignored by Git. Without it, the music selector uses
its built-in name and region rules.
Re-run CMake after generating it so the build includes it.

## Local packages

The ordinary Windows build helper creates a local runtime ZIP. It can contain
compiled timing tables, imported game assets and a personal profile; never
upload it. Only the separately audited `-ShareableRelease` package excludes
those files. Generated game assets remain local on Windows and Linux.
