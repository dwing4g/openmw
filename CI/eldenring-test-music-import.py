#!/usr/bin/env python3
"""Music importer regressions; synthetic banks and recordings, no game data."""

from pathlib import Path
import struct
import sys
import tempfile
import unittest
from collections import Counter
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import import_eldenring_music as music
from eldenring_music import wem
from eldenring_music import generate_cells
from eldenring_music.catalog import REGIONS, LEGACY_REGIONS, BOSS_PROFILES, PROFILES


def chunk(tag, data):
    return tag + struct.pack("<I", len(data)) + data


def node(kind, oid, body):
    return struct.pack("<BII", kind, len(body) + 4, oid) + body


def bank(version=135, event=True, stream=1, mid=104016490, track=138805394):
    source = struct.pack("<IBIIB", 0x40001, stream, mid, 64, 0)
    objects = [node(11, track, b"\0" + struct.pack("<I", 1) + source)]
    if event:
        objects.append(node(4, music.MUSIC_EVENT, b"\0"))
    # An unrelated node mentioning the same ID must never become a source.
    objects.append(node(2, 999, struct.pack("<I", mid)))
    return chunk(b"BKHD", struct.pack("<I", version)) + chunk(
        b"HIRC", struct.pack("<I", len(objects)) + b"".join(objects))


class MusicImportTests(unittest.TestCase):
    def test_requires_streamed_music_track_not_embedded_prefetch_or_byte_match(self):
        self.assertEqual(music.music_sources(bank()), {104016490: [138805394]})
        self.assertEqual(music.music_sources(bank(stream=0)), {})
        self.assertEqual(music.music_sources(bank(stream=2)), {104016490: [138805394]})

    def test_unknown_layout_and_missing_music_event_fail(self):
        for data in (bank(version=140), bank(event=False), bank()[:-1]):
            with self.subTest(data=data), self.assertRaises(ValueError):
                music.music_sources(data)

    def test_object_cannot_overrun_bank(self):
        bad = chunk(b"BKHD", struct.pack("<I", 135)) + chunk(
            b"HIRC", struct.pack("<IBII", 1, 11, 0xFFFFFFFF, 12))
        with self.assertRaisesRegex(ValueError, "object extent"):
            music.music_sources(bad)

    def test_wem_prefetch_cannot_masquerade_as_full_recording(self):
        data = b"RIFF" + struct.pack("<I", 108) + b"WAVE" + bytes(104)
        music.validate_wem(data)
        with self.assertRaisesRegex(ValueError, "truncated"):
            music.validate_wem(data[:40])

    def test_packed_sound_archive_uses_paths_without_sd_prefix(self):
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory)
            (game / "sd").mkdir()
            data = b"example full WEM payload"
            # One bucket, one nonencrypted file at data offset zero.
            header = b"BHD5\xff\x01\0\0" + struct.pack("<iiiii", 1, 76, 1, 28, 0)
            header += struct.pack("<ii", 1, 36)
            name = "wem/10/104016490.wem"
            header += struct.pack("<Qiiqqq", music.path_hash(name), len(data), len(data), 0, 0, 0)
            (game / "sd/sd.bhd").write_bytes(header)
            (game / "sd/sd.bdt").write_bytes(data)
            assets, sources = music.read_assets(game, game / "absent-keys", [name])
            self.assertEqual(assets[name], data)
            self.assertEqual(sources["sd/sd.bhd"], music.digest(header))

    def test_archive_extent_is_checked_before_read(self):
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory)
            (game / "sd").mkdir()
            header = b"BHD5\xff\x01\0\0" + struct.pack("<iiiii", 1, 76, 1, 28, 0)
            header += struct.pack("<ii", 1, 36)
            name = "wem/10/104016490.wem"
            header += struct.pack("<Qiiqqq", music.path_hash(name), 1000, 1000, 0, 0, 0)
            (game / "sd/sd.bhd").write_bytes(header)
            (game / "sd/sd.bdt").write_bytes(b"short")
            with self.assertRaisesRegex(ValueError, "extent"):
                music.read_assets(game, game, [name])

    def test_loose_installation_needs_neither_key_nor_crypto(self):
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory)
            (game / "sd/wem/10").mkdir(parents=True)
            name = "wem/10/104016490.wem"
            (game / "sd" / name).write_bytes(b"loose payload")
            assets, sources = music.read_assets(game, game / "absent-keys", [name])
            self.assertEqual(assets[name], b"loose payload")
            self.assertEqual(sources, {})

    def test_ogg_requires_end_of_stream_and_complete_pages(self):
        writer = wem.OggWriter()
        writer.page(b"\x01vorbis" + bytes(23), 0)
        writer.page(b"\x03vorbis", 0)
        writer.page(b"\x05vorbis", 0)
        writer.page(b"fake audio", 48000, last=True)
        data = bytes(writer.out)
        music.validate_ogg(data)
        with self.assertRaises(ValueError):
            music.validate_ogg(data[:-1])
        without_eos = bytearray(data)
        final_page = without_eos.rfind(b"OggS")
        without_eos[final_page + 5] &= ~4
        with self.assertRaisesRegex(ValueError, "complete"):
            music.validate_ogg(without_eos)

    def test_output_must_not_overwrite_game_installation(self):
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory)
            with self.assertRaisesRegex(ValueError, "outside"):
                music.import_music(game, game / "data", game, game / "missing-decoder.py")

    def test_catalog_preserves_original_tracks_and_covers_interior_profiles(self):
        self.assertEqual(len(music.TRACKS), 20)
        self.assertTrue(LEGACY_REGIONS < REGIONS)
        self.assertEqual(len({track[2] for track in music.TRACKS}), len(music.TRACKS))
        self.assertTrue({'caves', 'tunnels', 'catacombs', 'roundtable_hold', 'snowfield',
                         'eternal_city', 'farum_azula', 'shunning_grounds'} <= REGIONS)

    def test_boss_catalog_is_separate_from_ambient_and_has_unique_recordings(self):
        self.assertEqual(len(BOSS_PROFILES), 8)
        self.assertEqual(len(PROFILES), 28)
        self.assertFalse(REGIONS & BOSS_PROFILES)
        ids = [track[2] for track in music.TRACKS + music.BOSS_TRACKS]
        self.assertEqual(len(set(ids)), len(ids))
        self.assertEqual(dict((row[0], row[2]) for row in music.BOSS_TRACKS)['boss_mohg'], 163095055)

    def test_import_contains_bosses_and_preserves_ambient_filenames(self):
        writer = wem.OggWriter()
        for packet in (b'\x01vorbis' + bytes(23), b'\x03vorbis', b'\x05vorbis'):
            writer.page(packet, 0)
        writer.page(b'fake audio', 48000 * 60, last=True)
        ogg = bytes(writer.out)
        decoder = SimpleNamespace(info=lambda _: (2, 48000, 48000 * 60), to_ogg=lambda _: ogg)
        objects = [node(4, music.MUSIC_EVENT, b'\0')]
        assets = {}
        for index, row in enumerate(music.TRACKS + music.BOSS_TRACKS):
            mid = row[2]
            source = struct.pack('<IBIIB', 0x40001, 1, mid, 64, 0)
            objects.append(node(11, 100 + index, b'\0' + struct.pack('<I', 1) + source))
            assets[f'wem/{str(mid)[:2]}/{mid}.wem'] = b'RIFF' + struct.pack('<I', 108) + b'WAVE' + bytes(104)
        assets[music.MUSIC_BANK] = chunk(b'BKHD', struct.pack('<I', 135)) + chunk(
            b'HIRC', struct.pack('<I', len(objects)) + b''.join(objects))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with patch.object(music, 'read_assets', return_value=(assets, {})), \
                    patch.object(music, 'load_decoder', return_value=decoder), patch('builtins.print'):
                result = music.import_music(root / 'game', root / 'output', root, root)
            self.assertEqual(result['format_version'], 3)
            self.assertEqual(len(result['tracks']), 28)
            expected = {f'{profile}/day.ogg' for profile in REGIONS}
            expected |= {f'{profile}/battle.ogg' for profile in BOSS_PROFILES}
            self.assertEqual({track['file'] for track in result['tracks']}, expected)
            for track in result['tracks']:
                self.assertEqual((root / 'output/music/eldenring' / track['file']).read_bytes(), ogg)
                self.assertEqual(track['profile_type'], 'boss' if track['region'] in BOSS_PROFILES else 'ambient')

    def test_named_uses_precede_architecture(self):
        fixtures = (
            ('fort frostmoth, prison', Counter({'in_impbig_wall_01': 30}), 'shunning_grounds'),
            ('a nameless cave', Counter({'in_moldcave2_s_04': 10}), 'caves'),
            ('a nameless ruin', Counter({'in_dwrv_corr2_00': 10}), 'eternal_city'),
            ('a shrine', Counter({'in_dae_room_l_floor_01': 10}), 'farum_azula'),
            ('balmora, temple', Counter({'in_hlaalu_room_corner': 10}), 'divine_tower'),
            ('mournhold, great bazaar', Counter(), 'leyndell'),
        )
        for name, refs, expected in fixtures:
            with self.subTest(cell=name):
                flags = 129 if 'bazaar' in name else 1
                self.assertEqual(generate_cells.classify(name, refs, flags), expected)

    def test_cell_generator_uses_statics_and_merges_expansion_overrides(self):
        def record(kind, fields):
            body = b''.join(chunk(tag, value) for tag, value in fields)
            return kind + struct.pack('<III', len(body), 0, 0) + body

        header = record(b'TES3', [])
        cave = b'in_moldcave2_s_04\0'
        enemy = b'in_dae_all_lev+0\0'
        base = header + record(b'STAT', [(b'NAME', cave)])
        base += record(b'LEVC', [(b'NAME', enemy)])
        base += record(b'CELL', [(b'NAME', b'Hidden hollow\0'), (b'DATA', struct.pack('<Iii', 1, 0, 0))]
                       + [(b'NAME', cave)] * 5 + [(b'NAME', enemy)] * 10)
        override = header + record(b'CELL', [(b'NAME', b'Hidden hollow\0'),
                                            (b'DATA', struct.pack('<Iii', 1, 0, 0)),
                                            (b'NAME', b'quest_npc\0')])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'Morrowind.esm').write_bytes(base)
            (root / 'Tribunal.esm').write_bytes(override)
            (root / 'Bloodmoon.esm').write_bytes(header)
            output = root / 'cells.lua'
            self.assertEqual(generate_cells.generate(root, output), {'caves': 1})
            self.assertIn('"hidden hollow"', output.read_text())
            self.assertNotIn('quest_npc', output.read_text())

    def test_cell_generator_rejects_truncated_records(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'bad.esm'
            path.write_bytes(b'TES3' + struct.pack('<III', 0, 0, 0)
                             + b'CELL' + struct.pack('<III', 1000, 0, 0))
            with self.assertRaisesRegex(ValueError, 'record extent'):
                list(generate_cells.interiors(path))


if __name__ == "__main__":
    unittest.main()
