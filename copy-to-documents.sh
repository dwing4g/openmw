#!/usr/bin/env bash
# Copy this working tree and its locally imported assets from WSL to Windows.
set -euo pipefail

_elden_source="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
_elden_destination=''
_elden_dry_run=false
_elden_destination_set=false

for _elden_arg in "$@"; do
    case "$_elden_arg" in
        --dry-run) _elden_dry_run=true ;;
        -h|--help)
            echo 'Usage: ./copy-to-documents.sh destination-directory [--dry-run]'
            echo 'Example: ./copy-to-documents.sh /mnt/c/Users/YourName/Documents/openmw'
            echo 'Copies sources, generated timings, animations, music and combat sounds; preserves existing builds/profiles/saves.'
            exit 0
            ;;
        -*) echo "Unknown option: $_elden_arg" >&2; exit 1 ;;
        *)
            if $_elden_destination_set; then
                echo 'Supply at most one destination directory.' >&2
                exit 1
            fi
            _elden_destination="$_elden_arg"
            _elden_destination_set=true
            ;;
    esac
done

if ! $_elden_destination_set; then
    echo 'Supply the Windows destination directory; see --help.' >&2
    exit 1
fi

for _elden_tool in rsync realpath sha256sum python3; do
    if ! command -v "$_elden_tool" >/dev/null 2>&1; then
        echo "Required WSL command is missing: $_elden_tool" >&2
        exit 1
    fi
done
_elden_destination="$(realpath -m -- "$_elden_destination")"
case "$_elden_destination/" in
    "$_elden_source/"*) echo 'Destination must be outside the source tree.' >&2; exit 1 ;;
esac
case "$_elden_source/" in
    "$_elden_destination/"*) echo 'Destination must not contain the source tree.' >&2; exit 1 ;;
esac

for _elden_required in CMakeLists.txt apps/openmw/CMakeLists.txt components/CMakeLists.txt \
    extern/sol3/sol/sol.hpp extern/oics/CMakeLists.txt components/eldencombat/imported_timings.hpp \
    CI/eldenring-build-windows.ps1 CI/eldenring-audit-animations.py \
    scripts/install_eldenring_animations.py scripts/verify_eldenring_copy.py; do
    if [[ ! -f "$_elden_source/$_elden_required" ]]; then
        echo "Source file is missing: $_elden_required" >&2
        exit 1
    fi
done
_elden_animation="$_elden_source/local-assets/player_anims.bin"
if [[ ! -f "$_elden_animation" ]]; then
    _elden_animation="$_elden_source/../ELDEN-RING-Combat-Rewrite/assets/player_anims.bin"
fi
if [[ ! -f "$_elden_animation" ]]; then
    echo 'Missing player_anims.bin: place your baked bank in openmw/local-assets/ before copying.' >&2
    exit 1
fi
# Validate the complete clip inventory, including both phases of each seal
# gesture, before copying sources or replacing a usable destination bank.
python3 "$_elden_source/scripts/install_eldenring_animations.py" --source "$_elden_animation" \
    --timings-header "$_elden_source/components/eldencombat/imported_timings.hpp" --check-only
_elden_timing_hash="$(sha256sum < "$_elden_source/components/eldencombat/imported_timings.hpp")"
_elden_animation_hash="$(sha256sum < "$_elden_animation")"
_elden_music="$_elden_source/local-assets/music/eldenring"
if [[ ! -f "$_elden_music/manifest.json" ]]; then
    _elden_music="$_elden_source/build-eldenring-assets/music/eldenring"
fi
if [[ -f "$_elden_music/manifest.json" ]]; then
    python3 "$_elden_source/scripts/install_eldenring_music.py" --source "$_elden_music" --check-only
else
    _elden_music=''
fi
# Prefer the complete block/counter library over an older staged dodge-only copy.
_elden_sounds="$(python3 "$_elden_source/scripts/install_eldenring_sounds.py" --select-source \
    "$_elden_source/local-assets/sound/eldenring/dodge" \
    "$_elden_source/build-eldenring-assets/sound/eldenring/dodge")"
if [[ -n "$_elden_sounds" ]]; then
    python3 "$_elden_source/scripts/install_eldenring_sounds.py" --source "$_elden_sounds" --check-only
else
    _elden_sounds=''
    echo 'No imported combat sounds found; import them with scripts/import_eldenring_sounds.py to include them.' >&2
fi

# Preserve Windows filesystem permissions. No --delete: destination-only files stay.
# Do not use Git's file list: required generated timings and vendored code are ignored by Git.
_elden_flags=(-rt --checksum --omit-dir-times --human-readable --stats)
for _elden_pattern in '.git*' '.hg*' '.svn*' '.bzr*' '.mailmap' \
    '.cache/' '.codex/' '.agents/' '.aws/' '.idea/' '.vscode/' '.vs/' \
    '__pycache__/' '.pytest_cache/' '.mypy_cache/' '.ruff_cache/' '.ipynb_checkpoints/' \
    '.venv/' 'venv/' 'node_modules/' 'build*/' 'cmake-build-*/' 'MSVC*/' \
    'deps/' 'prebuilt/' 'CMakeFiles/' 'OpenMW-EldenCombat-Windows-*/' \
    'local-assets/' 'elden-profile/' 'profiles/' 'saves/' 'screenshots/' \
    '/resources/' '/Makefile' '/openmw.cfg' '/settings.cfg' '/input_v3.xml' \
    'CMakeCache.txt' 'cmake_install.cmake' 'compile_commands.json' \
    '.ninja_deps' '.ninja_log' 'build.ninja' 'rules.ninja' \
    '*.pyc' '*.pyo' '*.o' '*.obj' '*.a' '*.so' '*.pdb' '*.dll' '*.exe' '*.lib' \
    '*.dmp' '*.omwsave' 'OpenMW-EldenCombat-Windows-*.zip*'; do
    _elden_flags+=(--exclude="$_elden_pattern")
done
if $_elden_dry_run; then
    _elden_flags+=(--dry-run)
else
    mkdir -p -- "$_elden_destination"
fi

echo "Copying source: $_elden_source"
echo "Destination: $_elden_destination"
rsync "${_elden_flags[@]}" -- "$_elden_source/" "$_elden_destination/"
if $_elden_dry_run; then
    echo "Would also copy animations to: $_elden_destination/local-assets/player_anims.bin"
    if [[ -n "$_elden_music" ]]; then
        echo "Would also copy regional music to: $_elden_destination/local-assets/music/eldenring"
    fi
    if [[ -n "$_elden_sounds" ]]; then
        echo "Would also copy combat sounds to: $_elden_destination/local-assets/sound/eldenring/dodge"
    fi
    echo 'Dry run complete; no files copied.'
    exit 0
fi

mkdir -p -- "$_elden_destination/local-assets"
rsync -rt --checksum --omit-dir-times -- "$_elden_animation" "$_elden_destination/local-assets/player_anims.bin"
if [[ "$(sha256sum < "$_elden_destination/components/eldencombat/imported_timings.hpp")" != "$_elden_timing_hash" \
    || "$(sha256sum < "$_elden_destination/local-assets/player_anims.bin")" != "$_elden_animation_hash" ]]; then
    echo 'Timing or animation copy verification failed; rerun after source edits finish.' >&2
    exit 1
fi
if [[ -n "$_elden_music" ]]; then
    python3 "$_elden_source/scripts/install_eldenring_music.py" --source "$_elden_music" \
        --library "$_elden_destination/local-assets"
fi
if [[ -n "$_elden_sounds" ]]; then
    python3 "$_elden_source/scripts/install_eldenring_sounds.py" --source "$_elden_sounds" \
        --library "$_elden_destination/local-assets"
fi
_elden_verify=(--source "$_elden_source" --destination "$_elden_destination" --animations-source "$_elden_animation")
if [[ -n "$_elden_music" ]]; then _elden_verify+=(--music-source "$_elden_music"); fi
if [[ -n "$_elden_sounds" ]]; then _elden_verify+=(--sounds-source "$_elden_sounds"); fi
python3 "$_elden_source/scripts/verify_eldenring_copy.py" "${_elden_verify[@]}"
echo 'Copy complete, including available local assets. Existing builds, profiles and saves were preserved.'
echo 'Next, run CI/eldenring-build-windows.ps1 from the copied folder in Windows PowerShell.'
