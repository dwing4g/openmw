Lossless Wwise Vorbis converter

wem.py and ww2ogg/packed_codebooks_aoTuV_603.bin are copied from the
ELDEN-RING-Combat-Rewrite tools directory, revision
177a3b5ec14a9bde39b9ab458a3400b4038224e3.
The Python port is MIT licensed (LICENSE-MIT.txt); the ww2ogg algorithm,
Tremor codebook logic and codebook data retain the BSD license in
ww2ogg/COPYING. Both notices must accompany copies of this directory.

Upstream projects:
https://github.com/Funny-Bones/ELDEN-RING-Combat-Rewrite
https://github.com/hcs64/ww2ogg

The converter restores Ogg/Vorbis headers and packet framing without
re-encoding audio. It is tooling only and is not used by OpenMW at runtime.
No Elden Ring audio is included here; the packed codebook library comes
from ww2ogg and is reusable format data, not a game soundtrack.

Music catalog and cell classification

catalog.py lists the 20 base-game ambient recordings used by the regional and
interior playlists. The importer confirms their streamed MusicTrack membership
in the user's cs_smain.bnk before converting the local recordings. Version 2
manifests require all 20 profiles; the installer also accepts the original
five-profile version 1 libraries and keeps backups when upgrading them.

generate_cells.py reads Morrowind.esm, Tribunal.esm and Bloodmoon.esm to produce
files/data-mw/scripts/omw/music/eldenring_cells.lua. The generated table contains
interior record identifiers and music profile choices derived from local game
files. It is ignored by Git and must not be committed or redistributed.
It classifies unnamed caves and ruins by their static architectural references,
combines expansion overrides, and distinguishes leveled creatures from statics.
Classification happens while
generating the table, so the runtime music script never scans world objects.
Added cells use ordered name heuristics and a sheltered/outdoor default profile.
Without the optional generated table, all interiors use those same heuristics.

To reproduce the table from the English game files, run from the OpenMW root:
  python scripts/eldenring_music/generate_cells.py --data-dir "<Morrowind>/Data Files" --output files/data-mw/scripts/omw/music/eldenring_cells.lua

The ID/title reference is the mod author's Non-Combat BGM IDs list:
https://www.nexusmods.com/eldenring/mods/6194?tab=description

Dodge sound import

wwise.py is also copied from the same ELDEN-RING-Combat-Rewrite revision and
covered by LICENSE-MIT.txt. It reads Play action routing and source mix levels
from Wwise version-135 banks. scripts/import_eldenring_sounds.py uses it to
verify the dodge cues in the locally installed game's enus/cs_main.bnk before
extracting and losslessly converting ten recordings. No recordings are shipped
in the source tree. Reproduce from the OpenMW root with:
  python scripts/import_eldenring_sounds.py --game-dir "<Elden Ring>/Game"

The source was identified from c0000.anibnd.dcx/a00.tae event type 129, not from
an unlabelled audio sample: normal rolls 27100-27103, 27110-27113, 27120-27123
and crouch rolls 327100-327103, 327110-327113, 327120-327123 use character sound
8000 (Play_c000008000, WEM 665535489). Backstep 27000 starts armour sound 100;
soft/leather material 55 resolves to Play_c000000155 and nine random movement
recordings. These source volume and pitch values are respected at playback.
The roll cue is material-independent; backsteps use the source soft movement
set rather than adding surface-specific footsteps or armour clatter.

Output: build-eldenring-assets/sound/eldenring/dodge, including a SHA256 manifest
with event IDs, bank/input hashes and audio metadata. The copy/build helpers
validate and stage this folder under local-assets and then package it under
resources/vfs/sound/eldenring/dodge. Existing libraries are backed up before
replacement. Playback follows an accepted roll, crouch roll or backstep and
respects OpenMW's SFX volume; absent optional sounds are skipped quietly.
