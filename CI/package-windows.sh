#!/bin/bash
# Package OpenMW Windows build as a portable flat directory + zip.
# Mirrors the official nightly layout (OpenMW_MSVC2022_64_RelWithDebInfo_master.zip).
#
# Usage: CI/package-windows.sh [build_dir] [source_dir] [output_name]
#   build_dir   - build directory containing openmw.exe + DLLs (default: build)
#   source_dir  - openmw source tree (default: repo root)
#   output_name - zip name without extension (default: openmw-windows-x64)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${1:-build}"
SOURCE_DIR="${2:-${ROOT_DIR}}"
OUTPUT_NAME="${3:-openmw-windows-x64}"
PKG_DIR="${OUTPUT_NAME}"

if [ ! -f "${BUILD_DIR}/openmw.exe" ]; then
    echo "Error: ${BUILD_DIR}/openmw.exe not found. Build first."
    exit 1
fi

rm -rf "${PKG_DIR}"
mkdir -p "${PKG_DIR}"

# --- Executables ---
EXES=(openmw.exe openmw-launcher.exe openmw-cs.exe openmw-wizard.exe
      openmw-essimporter.exe openmw-iniimporter.exe openmw-navmeshtool.exe
      openmw-bulletobjecttool.exe bsatool.exe esmtool.exe niftest.exe)

for exe in "${EXES[@]}"; do
    src="${BUILD_DIR}/${exe}"
    if [ -f "$src" ]; then
        cp "$src" "${PKG_DIR}/"
        echo "  + ${exe}"
    fi
done

# --- DLLs (everything except PDBs, ILKs, and other junk) ---
echo "Copying DLLs..."
for dll in "${BUILD_DIR}"/*.dll; do
    [ -f "$dll" ] && cp "$dll" "${PKG_DIR}/"
done

# --- Plugin directories ---
for subdir in osgPlugins-3.6.5 platforms styles iconengines imageformats; do
    if [ -d "${BUILD_DIR}/${subdir}" ]; then
        cp -r "${BUILD_DIR}/${subdir}" "${PKG_DIR}/"
        echo "  + ${subdir}/"
    fi
done

# --- Resources ---
if [ -d "${BUILD_DIR}/resources" ]; then
    cp -r "${BUILD_DIR}/resources" "${PKG_DIR}/"
    echo "  + resources/"
fi

# --- Config files ---
for cfg in defaults.bin defaults-cs.bin gamecontrollerdb.txt openmw.cfg; do
    if [ -f "${BUILD_DIR}/${cfg}" ]; then
        cp "${BUILD_DIR}/${cfg}" "${PKG_DIR}/"
        echo "  + ${cfg}"
    fi
done

# --- License ---
if [ -f "${SOURCE_DIR}/LICENSE" ]; then
    cp "${SOURCE_DIR}/LICENSE" "${PKG_DIR}/license.txt"
    echo "  + license.txt"
fi

# --- CI-ID.txt ---
COMMIT=$(git -C "${SOURCE_DIR}" rev-parse HEAD 2>/dev/null || echo "unknown")
BRANCH=$(git -C "${SOURCE_DIR}" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")
printf "Branch %s\nCommit %s\nBuilt %s\n" "$BRANCH" "$COMMIT" "$(date -u +%Y-%m-%d)" > "${PKG_DIR}/CI-ID.txt"
echo "  + CI-ID.txt"

# --- Zip ---
echo ""
echo "Creating ${OUTPUT_NAME}.zip..."
if command -v 7z &>/dev/null; then
    7z a -tzip "${OUTPUT_NAME}.zip" "${PKG_DIR}/" -mx=5
else
    # Fall back to powershell Compress-Archive on Windows
    powershell -Command "Compress-Archive -Path '${PKG_DIR}/*' -DestinationPath '${OUTPUT_NAME}.zip' -Force"
fi

echo ""
echo "Done: ${OUTPUT_NAME}.zip"
echo "Contents: $(find "${PKG_DIR}" -type f | wc -l) files"
