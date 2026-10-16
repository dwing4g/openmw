#!/usr/bin/env python3
"""Import ambience and boss music from a local Elden Ring installation.

Reads the game without changing it. Packed installations require Python's
cryptography package and the public BinderKeys EldenRing_PC/Key/sd.pem key.
The bundled Python decoder converts Wwise Vorbis to Ogg without re-encoding;
no Wine, Oodle, FFmpeg or Wwise installation is needed. Alternatively supply
--decoder pointing to a compatible Python wem.py (info and to_ogg functions).
Generated music is local game-derived data and must not be committed.
"""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile

from unpack_eldenring_assets import decrypt_header, decrypt_ranges, path_hash, selected_entries
from eldenring_music.catalog import TRACKS, BOSS_TRACKS, BOSS_PROFILES, BOSS_SOURCE_URL


# WEM IDs documented by the author of this ambient-music replacement tool:
# https://www.nexusmods.com/eldenring/mods/6194?tab=description
# The importer also checks each ID against MusicTrack objects in the player's
# cs_smain.bnk. Music routes through Play_m000000000 in that bank; see:
# https://ndahn.github.io/yonder/guides/locating_sounds/
MUSIC_EVENT = 2303085137  # Wwise FNV-1("play_m000000000")
MUSIC_BANK = "cs_smain.bnk"
SOURCE_URL = "https://www.nexusmods.com/eldenring/mods/6194?tab=description"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_assets(game, keys, names):
    """Read loose assets or only the requested extents from sd/sd.bdt."""
    assets = {}
    missing = set(names)
    sources = {}
    for name in names:
        path = game / "sd" / name
        if path.is_file():
            assets[name] = path.read_bytes()
            missing.remove(name)
    if not missing:
        return assets, sources
    bhd = game / "sd" / "sd.bhd"
    bdt = game / "sd" / "sd.bdt"
    if not bhd.is_file() or not bdt.is_file():
        raise ValueError("Missing sd/sd.bhd and sd/sd.bdt, and these unpacked files: "
                         + ", ".join(sorted(missing)))
    encrypted = bhd.read_bytes()
    key = keys / "sd.pem"
    if encrypted[:4] != b"BHD5" and not key.is_file():
        raise ValueError(f"Missing public sound-archive key: {key}. "
                         "Use --keys-dir with BinderKeys/EldenRing_PC/Key.")
    header = decrypt_header(encrypted, key)
    sources["sd/sd.bhd"] = digest(encrypted)
    # sd is an independent archive: its path hashes start /wem/, not /sd/wem/.
    wanted = {path_hash(name): name for name in missing}
    archive_size = bdt.stat().st_size
    with bdt.open("rb") as source:
        for endian, entry in selected_entries(header, wanted):
            hashed, padded, size, offset, _sha_offset, aes_offset = entry
            if padded <= 0 or offset < 0 or offset + padded > archive_size or size > padded:
                raise ValueError("Invalid sound archive file extent")
            source.seek(offset)
            data = source.read(padded)
            if len(data) != padded:
                raise ValueError("Truncated sound archive file data")
            data = decrypt_ranges(data, header, aes_offset, endian)
            if size > 0:
                data = data[:size]
            name = wanted[hashed]
            assets[name] = data
            missing.remove(name)
    if missing:
        raise ValueError("Sound archive is missing required music files: " + ", ".join(sorted(missing)))
    return assets, sources


def music_sources(bank):
    """Return streamed WEM -> MusicTrack IDs in a Wwise 2019 bank.

    Reads the source-data prefix of MusicTrack records, not heuristic byte
    matches or prefetched DIDX fragments. Unknown bank layouts fail closed.
    """
    at = 0
    version = None
    objects = []
    while at < len(bank):
        if at + 8 > len(bank):
            raise ValueError("Truncated sound bank chunk")
        tag, size = bank[at:at + 4], struct.unpack_from("<I", bank, at + 4)[0]
        start, end = at + 8, at + 8 + size
        if end > len(bank):
            raise ValueError("Sound bank chunk exceeds its file")
        if tag == b"BKHD":
            if size < 4:
                raise ValueError("Truncated sound bank header")
            version = struct.unpack_from("<I", bank, start)[0]
        elif tag == b"HIRC":
            if size < 4:
                raise ValueError("Truncated sound bank object table")
            count = struct.unpack_from("<I", bank, start)[0]
            pos = start + 4
            for _ in range(count):
                if pos + 9 > end:
                    raise ValueError("Truncated sound bank object")
                kind, length, oid = struct.unpack_from("<BII", bank, pos)
                if length < 4 or pos + 5 + length > end:
                    raise ValueError("Invalid sound bank object extent")
                objects.append((kind, oid, bank[pos + 9:pos + 5 + length]))
                pos += 5 + length
            if pos != end:
                raise ValueError("Unexpected trailing sound bank object data")
        at = end
    if version != 135:
        raise ValueError(f"Unsupported Wwise bank version {version}; expected 135")
    if not any(kind == 4 and oid == MUSIC_EVENT for kind, oid, _ in objects):
        raise ValueError("Sound bank has no Play_m000000000 music event")
    media = {}
    for kind, oid, body in objects:
        if kind != 11:  # CAkMusicTrack
            continue
        if len(body) < 5:
            raise ValueError("Truncated music track")
        count = struct.unpack_from("<I", body, 1)[0]
        if 5 + 14 * count > len(body):
            raise ValueError("Truncated music track source table")
        for index in range(count):
            plugin, stream, mid, _size, _bits = struct.unpack_from("<IBIIB", body, 5 + 14 * index)
            if plugin == 0x00040001 and stream in (1, 2):
                media.setdefault(mid, []).append(oid)
    return media


def load_decoder(path):
    if not path.is_file():
        raise ValueError(f"Missing Wwise Vorbis decoder: {path}. Restore scripts/eldenring_music "
                         "or pass --decoder /path/to/wem.py.")
    spec = importlib.util.spec_from_file_location("eldenring_wem_decoder", path)
    if spec is None or spec.loader is None:
        raise ValueError("--decoder must name a Python wem.py module")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if not callable(getattr(module, "info", None)) or not callable(getattr(module, "to_ogg", None)):
        raise ValueError("Decoder must expose info(data) and to_ogg(data)")
    return module


def validate_wem(data):
    """Reject short prefetched fragments before the decoder can accept them."""
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("Music recording is not a Wwise RIFF/WAVE file")
    size = struct.unpack_from("<I", data, 4)[0] + 8
    if size != len(data):
        raise ValueError("Music recording is truncated or has unexpected trailing data")


def validate_ogg(data):
    """Verify complete output framing, Vorbis identification and EOS."""
    at, sequence, last = 0, 0, False
    while at < len(data):
        if at + 27 > len(data) or data[at:at + 4] != b"OggS" or data[at + 4] != 0:
            raise ValueError("Decoder produced an invalid Ogg page")
        flags = data[at + 5]
        granule, _serial, seq = struct.unpack_from("<qII", data, at + 6)
        segments = data[at + 26]
        payload = at + 27 + segments
        if payload > len(data):
            raise ValueError("Decoder produced truncated Ogg lacing")
        end = payload + sum(data[at + 27:payload])
        if end > len(data) or seq != sequence or last:
            raise ValueError("Decoder produced a truncated or discontinuous Ogg stream")
        if sequence == 0 and (not flags & 2 or data[payload:payload + 7] != b"\x01vorbis"):
            raise ValueError("Decoder output is not Ogg Vorbis")
        last = bool(flags & 4)
        sequence += 1
        at = end
    if sequence < 4 or not last or granule <= 0:
        raise ValueError("Decoder output has no complete audio stream")


def import_music(game, output, keys, decoder_path, include_night=False):
    music_root = output / "music" / "eldenring"
    if output == game or game in output.parents:
        raise ValueError("Output must be outside the installed game directory")
    selected = []
    for region, title, day, night in TRACKS:
        selected.append((region, title, "day", day))
        if include_night and night:
            selected.append((region, title + " (Night)", "night", night))
    selected.extend((profile, title, "battle", mid) for profile, title, mid in BOSS_TRACKS)
    names = [MUSIC_BANK] + [f"wem/{str(mid)[:2]}/{mid}.wem" for _, _, _, mid in selected]
    decoder = load_decoder(decoder_path)
    print("Reading the local Elden Ring sound archive...", flush=True)
    assets, headers = read_assets(game, keys, names)
    sources = music_sources(assets[MUSIC_BANK])
    manifest = {"format_version": 3, "source_game": "ELDEN RING", "source_id_reference": SOURCE_URL,
                "boss_source_id_reference": BOSS_SOURCE_URL,
                "source_headers": headers, "source_bank_sha256": digest(assets[MUSIC_BANK]), "tracks": []}
    # Finish every conversion before replacing any previous imported output.
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="eldenring-music-", dir=output) as temporary:
        staging = Path(temporary)
        for region, title, variant, mid in selected:
            if mid not in sources:
                raise ValueError(f"{title}: WEM {mid} is not a streamed Vorbis music track in cs_smain.bnk")
            data = assets[f"wem/{str(mid)[:2]}/{mid}.wem"]
            validate_wem(data)
            channels, rate, samples = decoder.info(data)
            if channels != 2 or rate <= 0 or samples < rate * 30:
                raise ValueError(f"{title}: expected a full stereo music recording")
            ogg = decoder.to_ogg(data)
            validate_ogg(ogg)
            relative = f"{region}/{variant}.ogg"
            track = staging / relative
            track.parent.mkdir(parents=True, exist_ok=True)
            track.write_bytes(ogg)
            manifest["tracks"].append({"region": region, "file": relative, "sha256": digest(ogg),
                                       "bytes": len(ogg), "wem_id": mid, "title": title,
                                       "profile_type": "boss" if region in BOSS_PROFILES else "ambient",
                                       "source_sha256": digest(data), "music_track_ids": sources[mid],
                                       "channels": channels, "sample_rate": rate, "samples": samples,
                                       "duration_seconds": round(samples / rate, 3)})
            print(f"Converted {title}: {samples / rate:.1f}s -> {relative}", flush=True)
        for track in manifest["tracks"]:
            target = music_root / track["file"]
            target.parent.mkdir(parents=True, exist_ok=True)
            (staging / track["file"]).replace(target)
        manifest_temp = staging / "manifest.json"
        manifest_temp.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        manifest_temp.replace(music_root / "manifest.json")
    print(f"Imported {len(selected)} full tracks into {music_root}; game installation unchanged.", flush=True)
    return manifest


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", type=Path, required=True, help="Installed Elden Ring Game folder")
    parser.add_argument("--keys-dir", type=Path,
                        default=root / "build-tools-local/elden-assets/BinderKeys/EldenRing_PC/Key",
                        help="Public BinderKeys EldenRing_PC/Key folder (sd.pem)")
    parser.add_argument("--output", type=Path, default=root / "build-eldenring-assets",
                        help="Asset output root; creates music/eldenring underneath")
    parser.add_argument("--decoder", type=Path,
                        default=Path(__file__).resolve().parent / "eldenring_music/wem.py",
                        help="Optional compatible Python Wwise Vorbis decoder module")
    parser.add_argument("--include-night", action="store_true", help="Also import available night variants")
    args = parser.parse_args()
    import_music(args.game_dir.resolve(), args.output.resolve(), args.keys_dir.resolve(),
                 args.decoder.resolve(), args.include_night)


if __name__ == "__main__":
    try:
        main()
    except ModuleNotFoundError as error:
        sys.exit(f"Missing Python dependency {error.name!r}; install it with: python -m pip install {error.name}")
    except (ValueError, OSError, ImportError, struct.error, IndexError, KeyError) as error:
        sys.exit(f"Elden Ring music import failed: {error}")
