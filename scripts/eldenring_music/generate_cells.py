#!/usr/bin/env python3
"""Build interior music classifications from local Morrowind/expansion records.

The generated record catalog is private game-derived data; do not commit or
redistribute it. Architectural reference IDs distinguish unnamed caves and
ruins from houses. Re-run against a localized installation when its interior
record IDs differ. Runtime name heuristics cover cells added by other plugins.
"""

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import struct


def static_ids(path):
    """Exclude similarly named leveled creature lists from architecture checks."""
    data = path.read_bytes()
    at = 0
    result = set()
    while at + 16 <= len(data):
        tag, size, _, _ = struct.unpack_from('<4sIII', data, at)
        body = data[at + 16:at + 16 + size]
        at += 16 + size
        if tag == b'STAT' and body[:4] == b'NAME':
            length = struct.unpack_from('<I', body, 4)[0]
            result.add(body[8:8 + length].rstrip(b'\0').decode('cp1252').lower())
    return result


def interiors(path):
    data = path.read_bytes()
    if data[:4] != b"TES3":
        raise ValueError(f"Not a TES3 plugin: {path}")
    at = 0
    while at < len(data):
        if at + 16 > len(data):
            raise ValueError(f"Truncated record in {path}")
        tag, size, _, _ = struct.unpack_from("<4sIII", data, at)
        end = at + 16 + size
        if end > len(data):
            raise ValueError(f"Invalid record extent in {path}")
        pos, at = at + 16, end
        if tag != b"CELL":
            continue
        names, flags = [], None
        while pos < end:
            if pos + 8 > end:
                raise ValueError(f"Truncated cell subrecord in {path}")
            kind, length = struct.unpack_from("<4sI", data, pos)
            pos += 8
            if pos + length > end:
                raise ValueError(f"Invalid cell subrecord extent in {path}")
            body = data[pos:pos + length]
            pos += length
            if kind == b"NAME":
                names.append(body.rstrip(b"\0").decode("cp1252").lower())
            elif kind == b"DATA" and flags is None:
                flags = struct.unpack_from("<I", body)[0]
        if flags is not None and flags & 1 and names:
            yield names[0], names[1:], flags


def classify(name, refs, flags):
    def has(*words):
        return any(word in name for word in words)

    def architecture(*prefixes):
        return sum(count for ref, count in refs.items() if ref.startswith(prefixes)) >= 3

    # Specific dangerous sites precede the generic architecture of their rooms.
    if has("dagoth ur", "akulakhan", "kogoruhn") or architecture("furn_6th", "furn_ash_statue"):
        return "caelid"
    if has("ancestral tomb", "barrow", "crypt", "catacomb", "tomb", "sepulcher"):
        return "catacombs"
    if has("sewer", "canalworks", "underworks", "prison", "jail", "dungeon"):
        return "shunning_grounds"
    if has("mine"):
        return "tunnels"
    if has("shipwreck", "grotto"):
        return "underground_rivers"
    if has("sotha sil", "bamz-amschend") or architecture("in_dwrv_", "in_dwe_", "in_sotha_"):
        return "eternal_city"
    if has("mortrag glacier", "norenen-dur") or architecture("in_dae_", "ex_dae_"):
        return "farum_azula"
    if has("castle karstaag") or architecture("bm_ic_", "bm_ka_"):
        return "snowfield"
    if has("old mournhold"):
        return "eternal_city"
    if has("temple", "shrine", "high fane", "chapel", "imperial cult", "palace of vivec"):
        return "divine_tower"
    if flags & 0x80:  # Quasi-exteriors, including Mournhold's open city squares.
        return "leyndell" if name.startswith("mournhold") else "limgrave"
    if has("royal palace", "grand council"):
        return "leyndell"
    if has("guild of mages", "mages guild", "telvanni council", "tel fyr") or architecture("in_t_", "in_telvanni"):
        return "academy"
    if has("fort ", "castle ") or architecture("in_stronghold_"):
        return "stormveil"
    if architecture("in_moldcave", "in_mudcave", "in_lavacave", "in_pycave", "in_bm_cave"):
        return "caves"
    return "roundtable_hold"


def generate(data_dir, output):
    cells = {}
    filenames = ("Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm")
    statics = set().union(*(static_ids(data_dir / filename) for filename in filenames))
    for filename in filenames:
        for name, refs, flags in interiors(data_dir / filename):
            # Expansion overrides often contain just their additional references.
            old_refs, _ = cells.setdefault(name, (Counter(), flags))
            old_refs.update(ref for ref in refs if ref in statics)
            cells[name] = old_refs, flags
    profiles = defaultdict(list)
    for name, (refs, flags) in sorted(cells.items()):
        profiles[classify(name, refs, flags)].append(name)
    lines = [
        '-- Interior record IDs from Morrowind.esm, Tribunal.esm and Bloodmoon.esm.',
        '-- Generated by scripts/eldenring_music/generate_cells.py from cell names',
        '-- and architectural reference IDs. Local game data: do not redistribute.',
        '-- Keep the ID lookup: many caves and ruins have no type in their name.',
        'local groups = {',
    ]
    for profile, names in sorted(profiles.items()):
        lines.append('    ' + profile + ' = {')
        lines.extend('        ' + json.dumps(name, ensure_ascii=False) + ',' for name in names)
        lines.append('    },')
    lines += ['}', '', 'local cells = {}', 'for profile, names in pairs(groups) do',
              '    for _, name in ipairs(names) do cells[name] = profile end', 'end', 'return cells', '']
    output.write_text('\n'.join(lines), encoding='utf-8')
    return {profile: len(names) for profile, names in profiles.items()}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(generate(args.data_dir, args.output), indent=2))
