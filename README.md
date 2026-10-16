# OpenMW with Elden Ring combat and music

This fork adapts the combat state machine from
[ELDEN-RING-Combat-Rewrite](https://github.com/Funny-Bones/ELDEN-RING-Combat-Rewrite)
to [OpenMW](https://github.com/OpenMW/openmw). It adds buffered melee combos,
charged attacks, dodging, guarding, lock-on, two-handed and off-hand combat,
animation retargeting, and optional regional and boss music. Morrowind still
provides the world, equipment, damage, spells, and game rules.

**No proprietary game assets are distributed in this repository.** To play,
you need your own Morrowind installation. To use the imported animations,
timings, music, and sound effects, you also need your own Elden Ring PC
installation and must generate those files locally. Without that optional
data, the combat integration uses fallback timings and native animations;
missing music falls back to Morrowind's music.

This is an independent fan project, not an official OpenMW release or a
project affiliated with Bethesda, FromSoftware, or Bandai Namco.

## Repositories and credits

| Project | Link |
| --- | --- |
| This fork | [Smthri/openmw-eldenring](https://github.com/Smthri/openmw-eldenring) |
| Official OpenMW GitHub repository | [OpenMW/openmw](https://github.com/OpenMW/openmw) |
| OpenMW development repository | [OpenMW on GitLab](https://gitlab.com/OpenMW/openmw) |
| Combat reference and extraction tools | [Funny-Bones/ELDEN-RING-Combat-Rewrite](https://github.com/Funny-Bones/ELDEN-RING-Combat-Rewrite) |
| Linux reader for the locally installed Oodle DLL | [McSimp/linoodle](https://github.com/McSimp/linoodle) |
| Public archive keys | [JKAnderson/BinderKeys](https://github.com/JKAnderson/BinderKeys) |
| Wwise Vorbis conversion | [hcs64/ww2ogg](https://github.com/hcs64/ww2ogg) |

The original OpenMW README is preserved in [README.openmw.md](README.openmw.md).
See [ELDEN-COMBAT.txt](ELDEN-COMBAT.txt) for integration details and
[components/eldencombat/README.txt](components/eldencombat/README.txt) for the
combat component's behavior and limitations.

## Get the source

```sh
git clone --branch elden-combat https://github.com/Smthri/openmw-eldenring.git
cd openmw-eldenring
```

Run the commands below from this `openmw` directory. Use a separate build
directory and runtime package; do not copy game files into tracked source
directories. These instructions build OpenMW itself, so Rust and Bevy are
not required.

## Generate optional game data locally

Python 3.10 or newer and Git are required. The extraction scripts need the
Python `cryptography` and `zstandard` packages. The combat reference is pinned
to `177a3b5ec14a9bde39b9ab458a3400b4038224e3`; the preparation helper applies
the included [source patch](scripts/eldenring_reference.patch) for native
Linux decompression, newer regulation containers, spell animations, and
conditional attack events. It leaves generated game data in the neighboring
checkout, outside this repository.

### Linux / WSL

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

### Windows x64

Use Python 3.10 or newer and Git for Windows. In PowerShell, replace the game
paths below:

```powershell
py -3 -m venv .venv
$python = Join-Path $PWD '.venv\Scripts\python.exe'
& $python -m pip install cryptography zstandard
& $python scripts/prepare_eldenring_reference.py --destination ../ELDEN-RING-Combat-Rewrite

$env:ER_GAME_DIR = 'D:\Steam\steamapps\common\ELDEN RING\Game'
$morrowind = 'D:\Steam\steamapps\common\Morrowind'
git clone https://github.com/JKAnderson/BinderKeys.git build-tools-local/elden-assets/BinderKeys
git -C build-tools-local/elden-assets/BinderKeys checkout b95a32c509e35e5b18306c1d37a7778ea4034f38

& $python scripts/unpack_eldenring_assets.py `
  --game-dir $env:ER_GAME_DIR `
  --keys-dir build-tools-local/elden-assets/BinderKeys/EldenRing_PC/Key `
  --reference-dir ../ELDEN-RING-Combat-Rewrite --generate
& $python scripts/import_eldenring_timings.py ../ELDEN-RING-Combat-Rewrite/src/sim/extracted.rs
```

Windows uses the Oodle DLL from your installed game directly. The Linux
decoder build step is not needed. To generate optional music and sounds:

```powershell
& $python scripts/import_eldenring_music.py --game-dir $env:ER_GAME_DIR
& $python scripts/import_eldenring_sounds.py --game-dir $env:ER_GAME_DIR
```

### Optional interior music classification

Before building, you can generate the interior lookup from your own
Morrowind, Tribunal, and Bloodmoon files. This command requires all three
master files to be present in `Data Files`:

```sh
python3 scripts/eldenring_music/generate_cells.py \
  --data-dir "$MORROWIND_DIR/Data Files" \
  --output files/data-mw/scripts/omw/music/eldenring_cells.lua
```

On Windows, use `& $python` instead of `python3` and
`--data-dir "$morrowind\Data Files"`. This generated lookup stays ignored by
Git. Without it, the music selector uses its built-in name and region rules.
Re-run CMake after generating it so the build includes it.

## Build and install

### Linux

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

### Windows x64

Install Visual Studio's **Desktop development with C++** workload and a
Windows SDK, CMake, Python 3.10+, Git, and 7-Zip. The supplied helper supports
Visual Studio 2022 with CMake 3.22+, or Visual Studio 2026 with CMake 4.2+.
The explicit 2022 invocation is:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\CI\eldenring-build-windows.ps1 `
  -VisualStudioVersion 2022 `
  -MorrowindDirectory 'D:\Steam\steamapps\common\Morrowind'
```

Use `-VisualStudioVersion 2026` for that toolchain. The helper downloads and
verifies the pinned OpenMW dependency bundle, builds the engine and launcher,
runs the combat tests, configures the profile, and installs into
`build-eldenring-package-x64`. It automatically finds locally generated
timings and animations in the neighboring combat repository and music/sounds
under `build-eldenring-assets`.

Optional parameters include `-Jobs 8`, `-WithoutLauncher`, `-PortableSaves`,
`-TimingsSource`, `-AnimationsSource`, `-MusicSource`, and `-SoundsSource`.
Run `build-eldenring-package-x64\Start-EldenCombat.cmd` to play, or
`Start-EldenCombat-Launcher.cmd` there to change settings.

**The Windows helper also creates a local runtime ZIP. If generated game
assets were installed, that ZIP contains them. Do not commit it or upload it
to GitHub Releases. The same restriction applies to local Linux packages.**

## Enable music and use the controls

The generated profile enables combat. Music is a separate option and starts
disabled: enable **Settings > Gameplay > Elden Ring regional and boss music**
in the launcher, or add `elden ring music = true` under `[Game]` in the
package's `elden-profile/settings.cfg`.

| Action | Default keyboard / mouse |
| --- | --- |
| Roll / backstep; sprint | Tap/release Space; hold Space |
| Jump; crouch | F; X |
| Light attack | Left mouse |
| Heavy / charged attack; guard counter | Shift + left mouse |
| Guard / off-hand attack | Right mouse |
| Two-hand right / left armament | E + left / right mouse |
| Lock on / release | Q or middle mouse |
| Draw weapon; ready magic | R; C |
| Interact; inventory | E on release; Tab |

Existing key bindings are preserved; use **Reset to Defaults** in Controls to
adopt the new layout. See [ELDEN-COMBAT.txt](ELDEN-COMBAT.txt) for gamepad
controls and the remaining bindings.

## Keep proprietary data out of Git

Only source code, authored configuration, documentation, tests, and explicitly
licensed third-party tooling belong in this fork. Never add Elden Ring or
Morrowind archives, DLLs, music, sounds, meshes, textures, skeletons, animation
banks, extracted parameters, or generated timing/lookup tables. Data generated
from a game remains game-derived even when its format is C++, Rust, Lua, JSON,
or another text format.

The relevant local paths are ignored, including `build-*`, `local-assets`,
`components/eldencombat/imported_timings.hpp`, and the generated interior lookup.
Do not use `git add -f` for these files. Before publishing source changes,
review the staged diff and run the source audit:

```sh
git diff --cached --stat
git diff --cached
python3 CI/eldenring-audit-source.py
```

After committing, run `python3 CI/eldenring-audit-source.py --base origin/master --revision HEAD`
to check every added or changed file version since the fork's upstream base,
including files removed in later commits.

The audit is an additional check, not a substitute for reviewing new files.
After committing reviewed source on `elden-combat`, maintainers can run
`python3 scripts/publish_eldenring_fork.py` with an authenticated GitHub CLI and
Git SSH access. It audits every new commit, creates/verifies a public fork of
OpenMW under the authenticated account, pushes only this branch without force,
and makes it the fork's default branch.

Never upload your game installations, extraction directories, local runtime
packages, or their backups. The included ww2ogg codebook is licensed reusable
format data, not an Elden Ring recording; its license is kept beside it.

## License

OpenMW and the combined application are covered by [GPLv3](LICENSE).
The combat adaptation preserves the reference project's
[MIT notice](components/eldencombat/LICENSE-MIT.txt). The audio conversion
tools preserve their [MIT notice](scripts/eldenring_music/LICENSE-MIT.txt)
and [ww2ogg BSD license](scripts/eldenring_music/ww2ogg/COPYING).
These code licenses grant no rights to proprietary game assets or files
generated from them.
