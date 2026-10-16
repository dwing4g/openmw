#!/usr/bin/env python3
"""Import real combat cues from a local Elden Ring installation.

Reads sd/enus/cs_main.bnk (loose or packed), resolves the actual Wwise Play
objects, and converts their complete WEMs losslessly. No game files change.
"""
import argparse
import json
from pathlib import Path
import struct
import sys
import tempfile

from eldenring_music import wem, wwise
from import_eldenring_music import digest, read_assets, validate_ogg, validate_wem

BANK = "enus/cs_main.bnk"
# From c0000.anibnd.dcx/a00.tae type-129 events: rolls 27100..27123 and
# crouch rolls 327100..327123 use character sound 8000. Backstep 27000
# starts armour sound 100; material 55 is the game's soft/leather movement.
CUES = (
    ("roll.ogg", "Play_c000008000", 665535489),
    *[(f"backstep-{i}.ogg", "Play_c000000155", media) for i, media in enumerate((
        445820189, 379670782, 20245932, 795778763, 330587730,
        459989575, 121988117, 367439636, 870971385))],
    # Weapon TAE 30700/32700 starts the counter chime at frame zero.
    ("guard-counter.ogg", "Play_c999999984", 594059761),
    # GuardHit a00:4200 starts armor sound 200; material 55 matches backstep.
    *[(f"block-{i}.ogg", "Play_c000000255", media) for i, media in enumerate((
        579348873, 887475329, 499114460, 110740972))],
)


def validate_bank(data):
    """Bounds-check the bank before the shared event-tree reader consumes it."""
    at, version = 0, None
    while at < len(data):
        if at + 8 > len(data):
            raise ValueError("Truncated sound bank chunk")
        tag, size = data[at:at + 4], struct.unpack_from("<I", data, at + 4)[0]
        start, end = at + 8, at + 8 + size
        if end > len(data):
            raise ValueError("Sound bank chunk exceeds its file")
        if tag == b"BKHD":
            if size < 4:
                raise ValueError("Truncated sound bank header")
            version = struct.unpack_from("<I", data, start)[0]
        elif tag == b"HIRC":
            if size < 4:
                raise ValueError("Truncated sound object table")
            count = struct.unpack_from("<I", data, start)[0]
            pos = start + 4
            for _ in range(count):
                if pos + 9 > end:
                    raise ValueError("Truncated sound object")
                length = struct.unpack_from("<I", data, pos + 1)[0]
                if length < 4 or pos + 5 + length > end:
                    raise ValueError("Invalid sound object extent")
                pos += 5 + length
            if pos != end:
                raise ValueError("Unexpected trailing sound object data")
        elif tag == b"DIDX" and size % 12:
            raise ValueError("Invalid embedded media index")
        at = end
    if version != 135:
        raise ValueError(f"Unsupported sound bank version {version}; expected 135")


def event_recordings(bank, name):
    """Require real Play routing, not a media ID appearing in unrelated data."""
    tree = bank.event_tree(name)
    if tree is None:
        raise ValueError(f"Missing combat sound event {name}")
    if tree[0] == "media":
        return {tree[1]: tree[2]}
    if tree[0] != "random" or any(child[0] != "media" for child in tree[1]):
        raise ValueError(f"Unsupported combat sound routing for {name}")
    return {child[1]: child[2] for child in tree[1]}


def import_sounds(game, output, keys):
    if output == game or game in output.parents:
        raise ValueError("Output must be outside the game installation")
    assets, headers = read_assets(game, keys, [BANK])
    data = assets[BANK]
    validate_bank(data)
    bank = wwise.Bank(data)
    events = {name: event_recordings(bank, name) for _, name, _ in CUES}
    for name, recordings in events.items():
        expected = {mid for _, event, mid in CUES if event == name}
        if set(recordings) != expected:
            raise ValueError(f"Combat event {name} changed; refusing unrelated recordings")
    missing = [f"wem/{str(mid)[:2]}/{mid}.wem" for _, _, mid in CUES
               if mid not in bank.media or bank.streamed(mid)]
    streamed, more_headers = read_assets(game, keys, missing) if missing else ({}, {})
    headers.update(more_headers)
    manifest = {"format_version": 2, "source_game": "ELDEN RING", "source_bank": BANK,
                "source_bank_sha256": digest(data), "source_headers": headers,
                "source_animation_events": {"roll": "a00.tae:27100-27123,327100-327123 type129/1/8000",
                                            "backstep": "a00.tae:27000 type129/15/100 + material55",
                                            "guard-counter": "weapon TAE:30700,32700 frame0 type129/1/999999984",
                                            "block": "a00.tae:4200 frame0 type129/15/200 + material55"},
                "sounds": []}
    output.mkdir(parents=True, exist_ok=True)
    destination = output / "sound/eldenring/dodge"
    with tempfile.TemporaryDirectory(prefix="eldenring-sounds-", dir=output) as temporary:
        stage = Path(temporary)
        for name, event, mid in CUES:
            source = streamed.get(f"wem/{str(mid)[:2]}/{mid}.wem", bank.media.get(mid))
            validate_wem(source)
            channels, rate, samples = wem.info(source)
            if channels not in (1, 2) or rate <= 0 or not 0.02 < samples / rate < 5:
                raise ValueError(f"Unexpected combat recording length: {mid}")
            audio = wem.to_ogg(source)
            validate_ogg(audio)
            (stage / name).write_bytes(audio)
            mix = events[event][mid]
            manifest["sounds"].append({"file": name, "wem_id": mid, "event": event,
                "sha256": digest(audio), "source_sha256": digest(source), "bytes": len(audio),
                "channels": channels, "sample_rate": rate, "samples": samples,
                "volume_db": mix.volume, "pitch_cents": mix.pitch,
                "volume_range_db": mix.volume_range, "pitch_range_cents": mix.pitch_range})
        destination.mkdir(parents=True, exist_ok=True)
        for sound in manifest["sounds"]:
            (stage / sound["file"]).replace(destination / sound["file"])
        (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        (stage / "manifest.json").replace(destination / "manifest.json")
    return manifest


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", required=True, type=Path)
    parser.add_argument("--keys-dir", type=Path,
                        default=root / "build-tools-local/elden-assets/BinderKeys/EldenRing_PC/Key")
    parser.add_argument("--output", type=Path, default=root / "build-eldenring-assets")
    args = parser.parse_args()
    manifest = import_sounds(args.game_dir.resolve(), args.output.resolve(), args.keys_dir.resolve())
    print(f"Imported {len(manifest['sounds'])} combat recordings into {args.output / 'sound/eldenring/dodge'}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, ImportError, struct.error, IndexError, KeyError, TypeError) as error:
        sys.exit(f"Elden Ring sound import failed: {error}")
