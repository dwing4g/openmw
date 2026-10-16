#!/usr/bin/env python3
"""Compile the combat integration translation units with the local native toolchain.

This checks real C++ code generation; it does not link the full OpenMW engine.
Usage: python3 CI/eldenring-compile-objects.py --jobs 2 [source.cpp ...]
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--jobs', type=int, choices=range(1, 5), default=2)
parser.add_argument('--compiler-flag', action='append', default=[], help='extra native compiler flag (use = for -D flags)')
parser.add_argument('sources', nargs='*')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
tools = root / 'build-tools-local'
output = root / 'build-eldencombat' / 'engine-objects'
output.mkdir(parents=True, exist_ok=True)
default_sources = [
    'mwmechanics/character.cpp', 'mwmechanics/eldencombat.cpp', 'mwmechanics/aicombat.cpp',
    'mwmechanics/combat.cpp', 'mwmechanics/actors.cpp', 'mwmechanics/creaturestats.cpp', 'mwmechanics/obstacle.cpp',
    'mwclass/actor.cpp', 'mwclass/npc.cpp', 'mwclass/creature.cpp', 'mwclass/weapon.cpp', 'mwworld/actionequip.cpp',
    'mwinput/actionmanager.cpp', 'mwinput/bindingsmanager.cpp', 'mwinput/inputmanagerimp.cpp', 'mwinput/keyboardmanager.cpp',
    'mwinput/mousemanager.cpp', 'mwinput/gyromanager.cpp', 'mwrender/camera.cpp',
    'mwrender/eldenanimation.cpp', 'mwlua/inputbindings.cpp',
    'mwphysics/actor.cpp', 'mwphysics/physicssystem.cpp', 'mwphysics/mtphysics.cpp', 'mwphysics/movementsolver.cpp',
    'mwsound/soundmanagerimp.cpp',
]
sources = [Path(s).resolve() for s in args.sources] if args.sources else [root / 'apps/openmw' / s for s in default_sources]
flags = ['-std=c++20', '-O0', '-g0', '-c']
flags += args.compiler_flag
for include in (root, root / 'extern/oics/include', root / 'extern/oics/tinyxml',
                root / 'extern/sol_config', root / 'extern/sol3', root / 'extern/osg-ffmpeg-videoplayer/include'):
    flags += ['-I', str(include)]
for dependency in ('osg', 'bullet', 'sdl', 'mygui', 'Recast', 'Detour', 'DetourCrowd', 'DetourTileCache',
                   'lua', 'audio', 'audio/AL'):
    flags += ['-isystem', str(tools / 'engine-headers' / dependency)]


def compile_source(source):
    relative = source.relative_to(root)
    stem = str(relative).replace('/', '_')
    obj = output / (stem + '.o')
    result = subprocess.run([str(tools / 'bin/c++'), *flags, str(source), '-o', str(obj)],
                            cwd=root, capture_output=True, text=True)
    (output / (stem + '.log')).write_text(result.stdout + result.stderr)
    status = 'PASS' if result.returncode == 0 else 'FAIL'
    print(f'{status} {relative}', flush=True)
    if result.returncode:
        print(result.stdout + result.stderr, flush=True)
    return f'{status} {relative}', result.returncode


with ThreadPoolExecutor(max_workers=args.jobs) as executor:
    results = list(executor.map(compile_source, sources))
failed = sum(status != 0 for _, status in results)
report = output.parent / ('validation-selected.txt' if args.sources else 'validation.txt')
report.write_text('Native Clang 18 C++20 object compilation (full engine linking not performed)\n'
                  + '\n'.join(line for line, _ in results)
                  + f'\n{len(results) - failed}/{len(results)} translation units passed.\n')
print(f'Validation report: {report}', flush=True)
raise SystemExit(bool(failed))
