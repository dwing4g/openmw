#!/usr/bin/env bash
# Prepare a native Linux reader for the Oodle DLL from a locally owned game.
# Keeps downloaded source, public binder keys, binaries and licenses together
# in an ignored build directory. Does not install system packages or game files.
set -euo pipefail
_elden_repository="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
_elden_decoder_dir="${1:-$_elden_repository/build-tools-local/elden-assets}"
mkdir -p -- "$_elden_decoder_dir"
_elden_decoder_dir="$(cd -- "$_elden_decoder_dir" && pwd)"
if [[ -x "$_elden_repository/build-tools-local/bin/c++" ]]; then
    source "$_elden_repository/CI/eldenring-build-env.sh"
fi

fetch_source() {
    local directory="$1" repository="$2" revision="$3"
    mkdir -p -- "$directory"
    git -C "$directory" init -q
    if [[ "${ELDEN_ASSET_DECODER_OFFLINE:-0}" != 1 ]]; then
        (cd -- "$directory" && git fetch --depth=1 "$repository" "$revision")
    fi
    git -C "$directory" checkout -q --detach "$revision"
}
fetch_source "$_elden_decoder_dir/linoodle" https://github.com/McSimp/linoodle.git \
    90b8d825f7f89272f03f52b5d1db4708e07eb83f
fetch_source "$_elden_decoder_dir/linoodle/pe-parse" https://github.com/trailofbits/pe-parse.git \
    76e3d41c4ca8e0490184460eb848db9cde534249
fetch_source "$_elden_decoder_dir/BinderKeys" https://github.com/JKAnderson/BinderKeys.git \
    b95a32c509e35e5b18306c1d37a7778ea4034f38

python3 - "$_elden_decoder_dir/linoodle" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
path = root / "windows_api.cpp"
source = path.read_text()
if "#include <locale.h>" not in source:
    source = source.replace("#include <stdlib.h>", "#include <stdlib.h>\n#include <locale.h>")
path.write_text(source)

path = root / "linoodle.cpp"
source = path.read_text()
if "#include <cstdlib>" not in source:
    source = source.replace('#include "windows_library.h"', '#include "windows_library.h"\n#include <cstdlib>')
source = source.replace('WindowsLibrary::Load("oo2core_8_win64.dll")',
                        'WindowsLibrary::Load(std::getenv("ER_OODLE_DLL"))')
if 'extern "C" int elden_kraken_decompress' not in source:
    source += '''
extern "C" int elden_kraken_decompress(uint8_t* source, size_t sourceLength,
                                     uint8_t* output, size_t outputLength) {
    return OodleLZ_Decompress(source, sourceLength, output, outputLength,
                             1, 0, 0, nullptr, 0, nullptr, nullptr, nullptr, 0, 3);
}
'''
path.write_text(source)
PY

cmake -S "$_elden_decoder_dir/linoodle" -B "$_elden_decoder_dir/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_COMMAND_LINE_TOOLS=OFF
cmake --build "$_elden_decoder_dir/build" --target linoodle --parallel 4
cat > "$_elden_decoder_dir/MANIFEST.txt" <<'TXT'
Native Linux asset-extraction helper; no game DLL is included.
linoodle: https://github.com/McSimp/linoodle 90b8d825f7f89272f03f52b5d1db4708e07eb83f (MIT)
pe-parse: https://github.com/trailofbits/pe-parse 76e3d41c4ca8e0490184460eb848db9cde534249 (MIT)
BinderKeys: https://github.com/JKAnderson/BinderKeys b95a32c509e35e5b18306c1d37a7778ea4034f38 (Apache-2.0)
Local changes: include locale.h, read ER_OODLE_DLL, export elden_kraken_decompress C ABI.
Original licenses are preserved in the respective source directories.
TXT
printf 'ER_KRAKEN_LIBRARY=%q\n' "$_elden_decoder_dir/build/liblinoodle.so"
printf 'Binder keys: %s\n' "$_elden_decoder_dir/BinderKeys/EldenRing_PC/Key"
printf '%s\n' 'Set ER_OODLE_DLL to your installed Game/oo2core_6_win64.dll before loading this helper.'
