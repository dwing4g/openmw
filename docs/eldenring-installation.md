# Other installation options

For the complete Windows setup, follow the [README](../README.md#windows-installation).
After cloning, run commands from the `openmw-eldenring` repository root.

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

The Windows helper creates a runtime ZIP. Packages and ZIPs containing generated
game assets must stay local; do not commit them or upload them to GitHub Releases.
The same restriction applies to Linux packages.
