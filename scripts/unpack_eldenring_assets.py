#!/usr/bin/env python3
"""Selectively read the player's assets from a local Elden Ring installation.

The installed game is read-only. Output is the sibling recreation's ignored
er-files directory unless --output specifies a different directory. Requires
cryptography, the public EldenRing_PC/Key keys from JKAnderson/BinderKeys, and
the recreation's DCX reader (Wine or ER_KRAKEN_LIBRARY for KRAK; zstandard for
newer regulation files). No game-derived data belongs in version control.

Format references, rather than copied implementation:
https://github.com/JKAnderson/SoulsFormats/blob/master/SoulsFormats/Formats/BHD5.cs
https://github.com/Nordgaren/UXM-Selective-Unpack/blob/master/UXM/ArchiveDictionary.cs
https://github.com/JKAnderson/SoulsFormats/blob/master/SoulsFormats/Util/SFUtil.cs
"""

import argparse
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import struct
import subprocess
import sys


PACKS = (
    "c0000", "c0000_a00_hi", "c0000_a00_lo", "c0000_a00_md", "c0000_a0x",
    "c0000_a1x", "c0000_a2x", "c0000_a3x", "c0000_a4x", "c0000_a5x", "c0000_a9x",
)
ARCHIVES = ("Data3", "Data0", "Data1", "Data2", "DLC")
# This public format constant decrypts regulation.bin, whose initial 16 bytes
# are the CBC IV. It is unrelated to user credentials or save files.
REGULATION_KEY = bytes.fromhex(
    "99 BF FC 36 6A 6B C8 C6 F5 82 7D 09 36 02 D6 76 "
    "C4 28 92 A0 1C 20 7F B0 24 D3 AF 4E 49 3F EF 99"
)


def path_hash(path):
    result = 0
    for character in "/" + path.replace("\\", "/").lower().lstrip("/"):
        result = (result * 0x85 + ord(character)) & 0xFFFFFFFFFFFFFFFF
    return result


def decrypt_header(data, key_file):
    if data[:4] == b"BHD5":
        return data
    from cryptography.hazmat.primitives import serialization
    key = serialization.load_pem_public_key(key_file.read_bytes()).public_numbers()
    block_size = (key.n.bit_length() + 7) // 8
    if len(data) % block_size:
        raise ValueError("Truncated RSA archive header")
    output = b"".join(
        pow(int.from_bytes(data[offset:offset + block_size], "big"), key.e, key.n)
        .to_bytes(block_size - 1, "big")
        for offset in range(0, len(data), block_size)
    )
    if output[:4] != b"BHD5":
        raise ValueError(f"Incorrect public key or unsupported archive: {key_file}")
    return output


def selected_entries(header, wanted):
    if len(header) < 28 or header[:4] != b"BHD5" or header[4] not in (0, 255):
        raise ValueError("Invalid BHD5 header")
    endian = "<" if header[4] == 255 else ">"
    count, offset = struct.unpack_from(endian + "ii", header, 16)
    if count < 0 or offset < 28 or offset + count * 8 > len(header):
        raise ValueError("Invalid BHD5 bucket table")
    for bucket in range(count):
        entries, start = struct.unpack_from(endian + "ii", header, offset + bucket * 8)
        if entries < 0 or start < 0 or start + entries * 40 > len(header):
            raise ValueError("Invalid BHD5 entry table")
        for index in range(entries):
            entry = struct.unpack_from(endian + "Qiiqqq", header, start + index * 40)
            if entry[0] in wanted:
                yield endian, entry


def decrypt_ranges(data, header, offset, endian):
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    if not offset:
        return data
    if offset < 0 or offset + 20 > len(header):
        raise ValueError("Invalid BHD5 AES key offset")
    key = header[offset:offset + 16]
    count, = struct.unpack_from(endian + "i", header, offset + 16)
    if count < 0 or offset + 20 + count * 16 > len(header):
        raise ValueError("Invalid BHD5 AES range table")
    output = bytearray(data)
    for index in range(count):
        start, end = struct.unpack_from(endian + "qq", header, offset + 20 + index * 16)
        if start == -1 or end == -1 or start == end:
            continue
        if start < 0 or end < start or end > len(data) or (end - start) % 16:
            raise ValueError("Invalid BHD5 AES range")
        decryptor = Cipher(algorithms.AES(key), modes.ECB()).decryptor()
        output[start:end] = decryptor.update(data[start:end]) + decryptor.finalize()
    return bytes(output)


def write_generated(path, data, manifest):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    temporary.replace(path)
    manifest["outputs"][str(path)] = {
        "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
    }


def unpack(game, output, keys, reference):
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    sys.path.insert(0, str(reference / "tools"))
    from erfmt import bnd4, dcx
    output.mkdir(parents=True, exist_ok=True)
    wanted = {path_hash(f"chr/{pack}.anibnd.dcx"): f"chr/{pack}.anibnd.dcx" for pack in PACKS}
    manifest = {"game_directory": str(game), "source_headers": {}, "outputs": {}}
    for archive in ARCHIVES:
        if not wanted:
            break
        header_path = game / (archive + ".bhd")
        data_path = game / (archive + ".bdt")
        if not header_path.exists() or not data_path.exists():
            continue
        encrypted = header_path.read_bytes()
        print(f"Reading {header_path.name}...", flush=True)
        header = decrypt_header(encrypted, keys / (archive + ".pem"))
        manifest["source_headers"][archive] = hashlib.sha256(encrypted).hexdigest()
        archive_size = data_path.stat().st_size
        with data_path.open("rb") as source:
            for endian, entry in selected_entries(header, wanted):
                hashed, padded, size, start, _sha_offset, aes_offset = entry
                if padded < 0 or start < 0 or start + padded > archive_size or size > padded:
                    raise ValueError("Invalid BDT file extent")
                source.seek(start)
                data = source.read(padded)
                if len(data) != padded:
                    raise ValueError("Truncated BDT file data")
                data = decrypt_ranges(data, header, aes_offset, endian)
                # Some installed versions write zero rather than the unpadded
                # size. Keep the extent; the DCX reader owns payload trimming.
                if size > 0:
                    data = data[:size]
                # Decode once to verify the selected file before recording it.
                if dcx(data)[:4] != b"BND4":
                    raise ValueError("Player animation archive is not BND4")
                name = wanted.pop(hashed)
                write_generated(output / name, data, manifest)
                print(f"Extracted {name} ({len(data):,} bytes)", flush=True)
    if wanted:
        raise ValueError("Missing player assets: " + ", ".join(wanted.values()))

    regulation = (game / "regulation.bin").read_bytes()
    if len(regulation) < 32 or len(regulation) % 16:
        raise ValueError("Invalid regulation.bin extent")
    decryptor = Cipher(algorithms.AES(REGULATION_KEY), modes.CBC(regulation[:16])).decryptor()
    container = dcx(decryptor.update(regulation[16:]) + decryptor.finalize())
    count = 0
    for _file_id, name, data in bnd4(container):
        basename = PureWindowsPath(name).name
        if basename.endswith(".param"):
            write_generated(output / "regulation-bin" / basename, data, manifest)
            count += 1
    manifest["regulation_sha256"] = hashlib.sha256(regulation).hexdigest()
    manifest["parameter_count"] = count
    (output / "extraction-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Extracted {count} parameter files; game installation unchanged.", flush=True)


def main():
    workspace = Path(__file__).resolve().parents[2]
    reference_default = workspace / "ELDEN-RING-Combat-Rewrite"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", type=Path, required=True, help="Installed Elden Ring Game folder")
    parser.add_argument("--keys-dir", type=Path, required=True, help="BinderKeys EldenRing_PC/Key folder")
    parser.add_argument("--reference-dir", type=Path, default=reference_default)
    parser.add_argument("--output", type=Path, help="Output folder; defaults to reference er-files")
    parser.add_argument("--generate", action="store_true", help="Generate timing and animation data after unpacking")
    args = parser.parse_args()
    game = args.game_dir.resolve()
    reference = args.reference_dir.resolve()
    output = (args.output or reference / "er-files").resolve()
    if output == game or game in output.parents:
        parser.error("Output must be outside the installed game directory")
    if not (game / "regulation.bin").is_file():
        parser.error("Game directory has no regulation.bin")
    if not (reference / "tools" / "erfmt.py").is_file():
        parser.error("Reference directory has no tools/erfmt.py")
    os.environ["ER_GAME_DIR"] = str(game)
    os.environ["ER_FILES"] = str(output)
    unpack(game, output, args.keys_dir.resolve(), reference)
    if args.generate:
        subprocess.run([sys.executable, str(reference / "tools" / "setup.py")], cwd=reference, check=True)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, ImportError) as error:
        sys.exit(str(error))
