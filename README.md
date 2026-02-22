Aggressive Performance
======

Two experimental optimizations for OpenMW that reduce CPU-side bottlenecks in the scene graph. Works best in dense cities like Narsis and Old Ebonheart.

[MoC Object Culling MR](https://gitlab.com/OpenMW/openmw/-/merge_requests/5150) — Read more about the occlusion culling approach.

---

## Software Occlusion Culling (Intel Masked Occlusion Culling)

Terrain and building meshes are rasterized into a low-resolution CPU depth buffer during cull traversal. Objects whose screen-space AABB is fully behind the depth buffer are rejected before reaching the draw thread, saving draw calls for hidden geometry.

## No culling, No in cell paging:
<img width="2175" height="1209" alt="image" src="https://github.com/user-attachments/assets/c1c6ec57-eedb-4ea0-9b8c-74b8897adb65" />

## Culling:
<img width="2175" height="1209" alt="image" src="https://github.com/user-attachments/assets/31c96d5e-94b6-4628-9296-64107f6b1a24" />

## In Cell Paging (alread in OpenMW, you just have to turn it on):
<img width="2175" height="1209" alt="image" src="https://github.com/user-attachments/assets/b2891825-00af-4abe-9786-411a0be34449" />

## Culling and Paging (best of both worlds):
<img width="2175" height="1209" alt="image" src="https://github.com/user-attachments/assets/4ebfd644-df5a-43a1-9db0-075887760bcf" />

##Example of walking through a tunnel in Vivec. Notice the occlusion doesn't make the banner disappear.



https://github.com/user-attachments/assets/85aeebc1-a40c-4581-b69f-7c362f1a263b





Recommended settings:
```ini
[Camera]
occlusion culling = true
occlusion culling terrain = true
occlusion culling statics = true
occlusion buffer width = 512
occlusion buffer height = 256
occlusion terrain lod = 3
occlusion terrain radius = 2
occlusion occluder min radius = 300
occlusion occluder max radius = 5000
occlusion occluder shrink factor = 1
occlusion occluder mesh resolution = 7
occlusion occluder max mesh resolution = 24
occlusion occluder inside threshold = 1.0
occlusion occluder max distance = 6144
occlusion debug overlay = true
```
### Known Issues

- **Shadow casters culled too early** — If a building is occluded but would normally cast a visible shadow, that shadow disappears. Rarely noticeable in practice. Workarounds: disable shadows, use soft shadows, or use higher resolution shadows.
- **Mournhold terrain** — The terrain LOD in Mournhold renders as an invisible ceiling that breaks occlusion. Workaround: `occlusion culling terrain = false`

---

## Shadow Temporal Reuse

Skips shadow cascade cull traversals on non-update frames while reusing the previous shadow map FBO textures. Only the shadow-space matrices are recomputed each frame so shadows track the moving camera.

**Setting:** `[Shadows] shadow update interval` (default `1` = off, max `4`)

```
interval=1 (off):
  Frame 1: [main cull][shadow cull x3][CLSB] → draw
  Frame 2: [main cull][shadow cull x3][CLSB] → draw

interval=2:
  Frame 1: [main cull][shadow cull x3][CLSB] → draw   ← full update
  Frame 2: [main cull]                       → draw   ← reuse FBOs

interval=3:
  Frame 1: [main cull][shadow cull x3][CLSB] → draw   ← full update
  Frame 2: [main cull]                       → draw   ← reuse
  Frame 3: [main cull]                       → draw   ← reuse

interval=4:
  Frame 1: [main cull][shadow cull x3][CLSB] → draw   ← full update
  Frame 2: [main cull]                       → draw   ← reuse
  Frame 3: [main cull]                       → draw   ← reuse
  Frame 4: [main cull]                       → draw   ← reuse
```

### Recommended Settings

```ini
[Shadows]
shadow update interval = 2
```

### Known Issues

- **FPS jitter** — Average FPS increases but alternates between update and reuse frames (e.g. 60-70 instead of a steady 40-45). Use a framerate cap if this is distracting.
- **Lagging NPC shadows** — At interval 2 this is negligible. At interval 4, NPC shadows visibly trail their owners. Use 4 only with NPC shadows disabled — it works well for terrain and statics, whose shadows only shift with time-of-day.
- **Shadow glitches** — Occasional brief visual artifacts on shadows during reuse frames.

---

The two systems are independent: occlusion culling saves draw calls by rejecting hidden objects, shadow reuse saves CPU cull traversals by skipping shadow cascade walks.

### Additional Performance Settings

```ini
[Terrain]
distant terrain = true
object paging = true
object paging active grid = true
```

---


OpenMW
======

OpenMW is an open-source open-world RPG game engine that supports playing Morrowind by Bethesda Softworks. You need to own the game for OpenMW to play Morrowind.

OpenMW also comes with OpenMW-CS, a replacement for Bethesda's Construction Set.

* Version: 0.51.0
* License: GPLv3 (see [LICENSE](https://gitlab.com/OpenMW/openmw/-/raw/master/LICENSE) for more information)
* Website: https://www.openmw.org
* IRC: #openmw on irc.libera.chat
* Discord: https://discord.gg/bWuqq2e


Font Licenses:
* DejaVuLGCSansMono.ttf: custom (see [files/data/fonts/DejaVuFontLicense.txt](https://gitlab.com/OpenMW/openmw/-/raw/master/files/data/fonts/DejaVuFontLicense.txt) for more information)
* DemonicLetters.ttf: SIL Open Font License (see [files/data/fonts/DemonicLettersFontLicense.txt](https://gitlab.com/OpenMW/openmw/-/raw/master/files/data/fonts/DemonicLettersFontLicense.txt) for more information)
* MysticCards.ttf: SIL Open Font License (see [files/data/fonts/MysticCardsFontLicense.txt](https://gitlab.com/OpenMW/openmw/-/raw/master/files/data/fonts/MysticCardsFontLicense.txt) for more information)

Current Status
--------------

The main quests in Morrowind, Tribunal and Bloodmoon are all completable. Some issues with side quests are to be expected (but rare). Check the [bug tracker](https://gitlab.com/OpenMW/openmw/-/issues/?milestone_title=openmw-1.0) for a list of issues we need to resolve before the "1.0" release. Even before the "1.0" release, however, OpenMW boasts some new [features](https://wiki.openmw.org/index.php?title=Features), such as improved graphics and user interfaces.

Pre-existing modifications created for the original Morrowind engine can be hit-and-miss. The OpenMW script compiler performs more thorough error-checking than Morrowind does, meaning that a mod created for Morrowind may not necessarily run in OpenMW. Some mods also rely on quirky behaviour or engine bugs in order to work. We are considering such compatibility issues on a case-by-case basis - in some cases adding a workaround to OpenMW may be feasible, in other cases fixing the mod will be the only option. If you know of any mods that work or don't work, feel free to add them to the [Mod status](https://wiki.openmw.org/index.php?title=Mod_status) wiki page.

Getting Started
---------------

* [Official forums](https://forum.openmw.org/)
* [Installation instructions](https://openmw.readthedocs.io/en/latest/manuals/installation/index.html)
* [Build from source](https://wiki.openmw.org/index.php?title=Development_Environment_Setup)
* [Testing the game](https://wiki.openmw.org/index.php?title=Testing)
* [How to contribute](https://wiki.openmw.org/index.php?title=Contribution_Wanted)
* [Report a bug](https://gitlab.com/OpenMW/openmw/issues) - read the [guidelines](https://wiki.openmw.org/index.php?title=Bug_Reporting_Guidelines) before submitting your first bug!
* [Known issues](https://gitlab.com/OpenMW/openmw/issues?label_name%5B%5D=Bug)

The data path
-------------

The data path tells OpenMW where to find your Morrowind files. If you run the launcher, OpenMW should be able to pick up the location of these files on its own, if both Morrowind and OpenMW are installed properly (installing Morrowind under WINE is considered a proper install).

Command line options
--------------------

    Syntax: openmw <options>
    Allowed options:
      --config arg                          additional config directories
      --replace arg                         settings where the values from the
                                            current source should replace those
                                            from lower-priority sources instead of
                                            being appended
      --user-data arg                       set user data directory (used for
                                            saves, screenshots, etc)
      --resources arg (=resources)          set resources directory
      --help                                print help message
      --version                             print version information and quit
      --data arg (=data)                    set data directories (later directories
                                            have higher priority)
      --data-local arg                      set local data directory (highest
                                            priority)
      --fallback-archive arg (=fallback-archive)
                                            set fallback BSA archives (later
                                            archives have higher priority)
      --start arg                           set initial cell
      --content arg                         content file(s): esm/esp, or
                                            omwgame/omwaddon/omwscripts
      --groundcover arg                     groundcover content file(s): esm/esp,
                                            or omwgame/omwaddon
      --no-sound [=arg(=1)] (=0)            disable all sounds
      --script-all [=arg(=1)] (=0)          compile all scripts (excluding dialogue
                                            scripts) at startup
      --script-all-dialogue [=arg(=1)] (=0) compile all dialogue scripts at startup
      --script-console [=arg(=1)] (=0)      enable console-only script
                                            functionality
      --script-run arg                      select a file containing a list of
                                            console commands that is executed on
                                            startup
      --script-warn [=arg(=1)] (=1)         handling of warnings when compiling
                                            scripts
                                            0 - ignore warnings
                                            1 - show warnings but consider script as
                                            correctly compiled anyway
                                            2 - treat warnings as errors
      --load-savegame arg                   load a save game file on game startup
                                            (specify an absolute filename or a
                                            filename relative to the current
                                            working directory)
      --skip-menu [=arg(=1)] (=0)           skip main menu on game startup
      --new-game [=arg(=1)] (=0)            run new game sequence (ignored if
                                            skip-menu=0)
      --encoding arg (=win1252)             Character encoding used in OpenMW game
                                            messages:

                                            win1250 - Central and Eastern European
                                            such as Polish, Czech, Slovak,
                                            Hungarian, Slovene, Bosnian, Croatian,
                                            Serbian (Latin script), Romanian and
                                            Albanian languages

                                            win1251 - Cyrillic alphabet such as
                                            Russian, Bulgarian, Serbian Cyrillic
                                            and other languages

                                            win1252 - Western European (Latin)
                                            alphabet, used by default
      --fallback arg                        fallback values
      --no-grab [=arg(=1)] (=0)             Don't grab mouse cursor
      --export-fonts [=arg(=1)] (=0)        Export Morrowind .fnt fonts to PNG
                                            image and XML file in current directory
      --activate-dist arg (=-1)             activation distance override
      --random-seed arg (=<impl defined>)   seed value for random number generator
