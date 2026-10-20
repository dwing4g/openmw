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

## Windows x64 download and installation

You need **64-bit Windows 10 (1903 or newer) or Windows 11**, your own installed
**Morrowind and Elden Ring**, and [64-bit Python 3.14](https://www.python.org/downloads/windows/)
with the `py` launcher. Internet access is needed once to install two Python
packages. Visual Studio, CMake, Git and Rust are not needed to install the release.

1. Download the **[Windows x64 ZIP](https://github.com/Smthri/openmw-eldenring/releases/download/windows-x64-2026.10.20/OpenMW-EldenCombat-Windows-x64.zip)**
   and its **[SHA-256 checksum](https://github.com/Smthri/openmw-eldenring/releases/download/windows-x64-2026.10.20/OpenMW-EldenCombat-Windows-x64.zip.sha256)**
   from the [2026-10-20 release](https://github.com/Smthri/openmw-eldenring/releases/tag/windows-x64-2026.10.20).
   GitHub's **Source code** downloads are not compiled applications.
2. Right-click the ZIP, choose **Extract All**, and extract into a new writable
   folder, such as `Games\OpenMW-EldenCombat`. Keep all files together.
3. Double-click **`Setup-EldenCombat.cmd`**. Select your Morrowind folder containing
   `Data Files` and `Morrowind.ini`.
4. Double-click **`Import-EldenRing.cmd`**. Select your installed `ELDEN RING\Game`
   folder. Wait for **Elden Ring import completed**. This generates animations,
   combat timings, music and sounds locally; it does not modify either game.
5. Open **`Start-EldenCombat-Launcher.cmd`** to choose your display settings, then
   **`Start-EldenCombat.cmd`** to play. If the launcher offers its installation
   wizard, choose **Skip**; setup has already configured Morrowind.

Only the original release ZIP is suitable for sharing. **Never upload the
configured installation, `.elden-assets` cache, or generated game data.**
The release executable loads locally generated timings at runtime; no extracted
timing table is compiled into the shared binary.

For checksum verification, command-line setup, updates and building from source,
see the [installation details](docs/eldenring-installation.md).

The release was verified with a fresh native Windows installation, local asset
import, launcher and game startup, relocation, and live player/NPC combat.
The [matching source archive](https://github.com/Smthri/openmw-eldenring/releases/download/windows-x64-2026.10.20/OpenMW-EldenCombat-Source.zip)
is included with the release.

## Compile from source

For 64-bit Windows. Linux/WSL instructions are [here](docs/eldenring-installation.md).

### 1. Install the requirements

- Morrowind and Elden Ring.
- Git, **64-bit Python 3.10+** (including the `py` launcher), CMake 4.2+, and 7-Zip.
- Visual Studio 2026 with **Desktop development with C++** and a Windows SDK.

### 2. Download the fork

Open a new PowerShell window in a folder where you want to keep the project:

```powershell
git clone --branch elden-combat https://github.com/Smthri/openmw-eldenring.git
cd openmw-eldenring
```

Keep using this PowerShell window for the remaining steps. Run commands in order;
if one fails, fix the error before continuing.

### 3. Generate the game data

Replace the two paths below with your installations. The Elden Ring path must
end in `Game`; the Morrowind path must contain the `Data Files` folder.

```powershell
$env:ER_GAME_DIR = 'D:\Steam\steamapps\common\ELDEN RING\Game'
$morrowind = 'D:\Steam\steamapps\common\Morrowind'

py -3 -m venv .venv
$python = Join-Path $PWD '.venv\Scripts\python.exe'
& $python -m pip install cryptography zstandard
& $python scripts/prepare_eldenring_reference.py

git clone https://github.com/JKAnderson/BinderKeys.git build-tools-local/elden-assets/BinderKeys

& $python scripts/unpack_eldenring_assets.py `
  --game-dir $env:ER_GAME_DIR `
  --keys-dir build-tools-local/elden-assets/BinderKeys/EldenRing_PC/Key `
  --generate
```

This creates the animation bank automatically. No manual WitchyBND extraction,
`.nif` conversion, or Rust installation is needed.

For music and combat sounds, also run these optional commands before building:

```powershell
& $python scripts/import_eldenring_music.py --game-dir $env:ER_GAME_DIR
& $python scripts/import_eldenring_sounds.py --game-dir $env:ER_GAME_DIR
```

### 4. Build and play

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\CI\eldenring-build-windows.ps1 `
  -VisualStudioVersion 2026 `
  -MorrowindDirectory $morrowind
```

Wait for it to finish successfully. It downloads dependencies, builds the fork,
installs the generated assets, and sets up Morrowind automatically.

Open `build-eldenring-package-x64`, then double-click:

- **`Start-EldenCombat-Launcher.cmd`** to choose your resolution and settings.
- **`Start-EldenCombat.cmd`** to play.

If the launcher offers an installation wizard, click **Skip**; Morrowind is already configured.

Keep the generated package and ZIP local: they contain your game-derived assets.
For Linux/WSL, Visual Studio 2022, or other options, see the
[installation details](docs/eldenring-installation.md).

## Enable music and use the controls

New profiles inherit the fork's display, graphics and gameplay defaults:
1280×720 windowed, 1× interface scaling, difficulty −90, smooth animation
transitions and movement, and turning toward movement. Distant terrain,
shadows, enhanced lighting and water are enabled. Rebuilding preserves
existing settings and controls; customize them in the launcher or the
package's `elden-profile/settings.cfg`.

Combat and Elden Ring music are enabled by default. Music remains a separate
option under **Settings > Gameplay > Elden Ring regional and boss music**;
missing imported tracks fall back to Morrowind's music.

| Action | Default keyboard / mouse |
| --- | --- |
| Roll / backstep; sprint | Tap/release Space; hold Space |
| Jump; crouch | F; X |
| Right-hand light attack | Left mouse |
| Heavy / charged attack; guard counter | Shift + left mouse |
| Guard / off-hand or paired attack | Right mouse |
| Two-hand right / left armament | E + left / right mouse |
| Lock on / release | Q or middle mouse |
| Draw weapon; ready magic | R; C |
| Interact; inventory | E on release; Tab |

Inventory equipping fills the right hand first; equipping another melee weapon
fills the left for dual wielding. Use E + mouse to switch either armament into
or out of a two-handed grip. With matching weapon classes in both hands, right
click uses the dedicated paired combo and its running, roll and jump attacks.
Left click attacks with the right-hand weapon. Sheathing hides both weapons;
shields retain their normal visibility. With a spear and shield, hold right
mouse and press left mouse to thrust while blocking.

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

After committing, run `python3 CI/eldenring-audit-source.py --base f5e0fbe9fc --revision HEAD`
to check every added or changed file version since the fork's upstream base,
including files removed in later commits.

The audit is an additional check, not a substitute for reviewing new files.
After committing reviewed source on `elden-combat`, maintainers can run
`python3 scripts/publish_eldenring_fork.py` with an authenticated GitHub CLI and
Git SSH access. It audits every new commit, creates/verifies a public fork of
OpenMW under the authenticated account, pushes only this branch without force,
and makes it the fork's default branch.

Never upload your game installations, extraction directories, configured runtime
packages, or their backups. Only the clean shareable release and its corresponding
source archive may be distributed. The included ww2ogg codebook is licensed reusable
format data, not an Elden Ring recording; its license is kept beside it.

## License

OpenMW and the combined application are covered by [GPLv3](LICENSE).
The combat adaptation preserves the reference project's
[MIT notice](components/eldencombat/LICENSE-MIT.txt). The audio conversion
tools preserve their [MIT notice](scripts/eldenring_music/LICENSE-MIT.txt)
and [ww2ogg BSD license](scripts/eldenring_music/ww2ogg/COPYING).
These code licenses grant no rights to proprietary game assets or files
generated from them.
