#!/bin/bash
# Package OpenMW as a portable flat directory with bundled shared libraries.
# Mirrors the official nightly tarball layout.
#
# Usage: CI/package-linux.sh [build_dir] [output_name]
#   build_dir   - cmake build directory (default: build)
#   output_name - tarball name without extension (default: openmw-linux-x64)

set -uo pipefail

BUILD_DIR="${1:-build}"
OUTPUT_NAME="${2:-openmw-linux-x64}"
PKG_DIR="${OUTPUT_NAME}"

if [ ! -f "${BUILD_DIR}/openmw" ]; then
    echo "Error: ${BUILD_DIR}/openmw not found. Build first."
    exit 1
fi

rm -rf "${PKG_DIR}"
mkdir -p "${PKG_DIR}/lib" "${PKG_DIR}/plugins" "${PKG_DIR}/resources" "${PKG_DIR}/desktop files"

# --- Binaries ---
BINS=(openmw openmw-launcher openmw-essimporter openmw-iniimporter
      openmw-navmeshtool openmw-bulletobjecttool bsatool esmtool niftest)

for bin in "${BINS[@]}"; do
    src="${BUILD_DIR}/${bin}"
    if [ -f "$src" ]; then
        cp "$src" "${PKG_DIR}/${bin}.x86_64"
    fi
done

echo "Stripping debug symbols from binaries..."
strip --strip-debug "${PKG_DIR}"/*.x86_64

# --- Wrapper scripts ---
generate_wrapper() {
    local name="$1"
    local has_qt="$2"

    local qt_export=""
    if [ "$has_qt" = "yes" ]; then
        qt_export='export QT_PLUGIN_PATH="./plugins"'
    fi

    cat > "${PKG_DIR}/${name}" << 'HEADER'
#!/bin/sh

# check if we got started from launcher also run under gdb
# then we are already in a gdb session and need to directly
# start BINNAME
if [ -n "$DEBUG" ] && [ -n "$DEBUG_START_FROM_LAUNCHER" ]
then
  exec ./BINNAME.x86_64 "$@"
fi

readlink() {
  path=$1

  if [ -L "$path" ]
  then
    ls -l "$path" | sed 's/^.*-> //'
  else
    return 1
  fi
}

SCRIPT="$0"
COUNT=0
while [ -L "${SCRIPT}" ]
do
  SCRIPT=$(readlink "${SCRIPT}")
  COUNT=$(expr "${COUNT}" + 1)
  if [ "${COUNT}" -gt 100 ]
  then
    echo "Too many symbolic links"
    exit 1
  fi
done
GAMEDIR=$(dirname "${SCRIPT}")

cd "$GAMEDIR" || (echo "Failed to enter game directory $GAMEDIR" && exit 1)

export LD_LIBRARY_PATH="./lib"
HEADER

    if [ -n "$qt_export" ]; then
        echo "$qt_export" >> "${PKG_DIR}/${name}"
    fi

    cat >> "${PKG_DIR}/${name}" << 'FOOTER'

# run BINNAME in debugger if DEBUG env var is set
if [ -n "$DEBUG" ]
then
  which gdb >/dev/null 2>/dev/null
  if [ "$?" -ne 0 ]
  then
    echo "Could not find gdb"
    exit 1
  fi
  export DEBUGINFOD_URLS=""
  gdb --args ./BINNAME.x86_64 "$@"
  exit $?
fi

./BINNAME.x86_64 "$@"
FOOTER

    sed -i "s/BINNAME/${name}/g" "${PKG_DIR}/${name}"
    chmod +x "${PKG_DIR}/${name}"
}

# openmw gets a special wrapper (DEBUG_START_FROM_LAUNCHER + fork handling)
cat > "${PKG_DIR}/openmw" << 'EOF'
#!/bin/sh

# check if we got started from launcher also run under gdb
# then we are already in a gdb session and need to directly
# start openmw
if [ -n "$DEBUG" ] && [ -n "$DEBUG_START_FROM_LAUNCHER" ]
then
  exec ./openmw.x86_64 "$@"
fi

readlink() {
  path=$1

  if [ -L "$path" ]
  then
    ls -l "$path" | sed 's/^.*-> //'
  else
    return 1
  fi
}

SCRIPT="$0"
COUNT=0
while [ -L "${SCRIPT}" ]
do
  SCRIPT=$(readlink "${SCRIPT}")
  COUNT=$(expr "${COUNT}" + 1)
  if [ "${COUNT}" -gt 100 ]
  then
    echo "Too many symbolic links"
    exit 1
  fi
done
GAMEDIR=$(dirname "${SCRIPT}")

cd "$GAMEDIR" || (echo "Failed to enter game directory $GAMEDIR" && exit 1)

export LD_LIBRARY_PATH="./lib"

# run openmw in debugger if DEBUG env var is set
if [ -n "$DEBUG" ]
then
  which gdb >/dev/null 2>/dev/null
  if [ "$?" -ne 0 ]
  then
    echo "Could not find gdb"
    exit 1
  fi
  export DEBUGINFOD_URLS=""
  gdb --args ./openmw.x86_64 "$@"
  exit $?
fi

./openmw.x86_64 "$@"
EOF
chmod +x "${PKG_DIR}/openmw"

# launcher gets QT_PLUGIN_PATH + fork-follow for gdb
cat > "${PKG_DIR}/openmw-launcher" << 'EOF'
#!/bin/sh

readlink() {
  path=$1

  if [ -L "$path" ]
  then
    ls -l "$path" | sed 's/^.*-> //'
  else
    return 1
  fi
}

SCRIPT="$0"
COUNT=0
while [ -L "${SCRIPT}" ]
do
  SCRIPT=$(readlink "${SCRIPT}")
  COUNT=$(expr "${COUNT}" + 1)
  if [ "${COUNT}" -gt 100 ]
  then
    echo "Too many symbolic links"
    exit 1
  fi
done
GAMEDIR=$(dirname "${SCRIPT}")

cd "$GAMEDIR" || (echo "Failed to enter game directory $GAMEDIR" && exit 1)

export LD_LIBRARY_PATH="./lib"
export QT_PLUGIN_PATH="./plugins"

# run openmw-launcher in debugger if DEBUG env var is set
if [ -n "$DEBUG" ]
then
  which gdb >/dev/null 2>/dev/null
  if [ "$?" -ne 0 ]
  then
    echo "Could not find gdb"
    exit 1
  fi
  export DEBUGINFOD_URLS=""
  export DEBUG_START_FROM_LAUNCHER=1
  gdb -iex "set follow-fork-mode child" -iex "set detach-on-fork off" -iex "set follow-exec-mode same" --args ./openmw-launcher.x86_64 "$@"
  exit $?
fi

./openmw-launcher.x86_64 "$@"
EOF
chmod +x "${PKG_DIR}/openmw-launcher"

# Standard wrappers for the rest
for bin in openmw-essimporter openmw-iniimporter openmw-navmeshtool openmw-bulletobjecttool bsatool esmtool niftest; do
    if [ -f "${PKG_DIR}/${bin}.x86_64" ]; then
        generate_wrapper "$bin" "no"
    fi
done

# --- Shared libraries ---
# Collect all shared lib dependencies, exclude system basics (libc, libm, libdl, libpthread, ld-linux, libX*, libGL, libdrm, libgbm)
collect_libs() {
    local binary="$1"
    ldd "$binary" 2>/dev/null | awk '/=>/ && !/not found/ {print $3}' | while read -r lib; do
        case "$(basename "$lib")" in
            libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|ld-linux*) continue ;;
            libX*.so*|libxcb.so*|libxcb-shm*|libxcb-xfixes*|libxcb-shape*|libxcb-render*|libxcb-randr*|libxcb-keysyms*|libxcb-image*|libxcb-icccm*|libxcb-sync*|libxcb-xkb*|libxcb-util*|libxcb-glx*|libxcb-dri*|libxcb-present*) continue ;;
            libGL.so*|libGLX.so*|libGLdispatch.so*|libEGL.so*|libOpenGL.so*) continue ;;
            libdrm.so*|libgbm.so*|libwayland-*.so*) continue ;;
            libstdc++.so*|libgcc_s.so*) continue ;;
            libnvidia*|libvulkan*) continue ;;
            *) echo "$lib" ;;
        esac
    done
}

echo "Collecting shared libraries..."
ALL_LIBS=$(mktemp)
for bin_file in "${PKG_DIR}"/*.x86_64; do
    collect_libs "$bin_file" >> "$ALL_LIBS"
done
sort -u "$ALL_LIBS" | while read -r lib; do
    cp -n "$lib" "${PKG_DIR}/lib/" 2>/dev/null || true
done
rm -f "$ALL_LIBS"

# --- OSG plugins ---
OSG_PLUGIN_DIR=$(pkg-config --variable=plugindir openscenegraph 2>/dev/null)
[ -z "$OSG_PLUGIN_DIR" ] && OSG_PLUGIN_DIR=$(find /usr/lib -name "osgPlugins-*" -type d 2>/dev/null | head -1)
if [ -d "$OSG_PLUGIN_DIR" ]; then
    mkdir -p "${PKG_DIR}/lib/$(basename "$OSG_PLUGIN_DIR")"
    for plugin in osgdb_bmp osgdb_dds osgdb_dae osgdb_freetype osgdb_glsl osgdb_jpeg osgdb_osg osgdb_png osgdb_serializers_osg osgdb_tga; do
        src="${OSG_PLUGIN_DIR}/${plugin}.so"
        if [ -f "$src" ]; then
            cp "$src" "${PKG_DIR}/lib/$(basename "$OSG_PLUGIN_DIR")/"
        fi
    done
    # Collect libs needed by plugins too
    for plugin_so in "${PKG_DIR}/lib/$(basename "$OSG_PLUGIN_DIR")"/*.so; do
        collect_libs "$plugin_so" | while read -r lib; do
            cp -n "$lib" "${PKG_DIR}/lib/" 2>/dev/null || true
        done
    done
fi

# --- Qt plugins ---
QT_PLUGIN_DIR=$(pkg-config --variable=plugindir Qt6Core 2>/dev/null)
[ -z "$QT_PLUGIN_DIR" ] && QT_PLUGIN_DIR=$(find /usr/lib -path "*/qt6/plugins" -type d 2>/dev/null | head -1)
if [ -d "$QT_PLUGIN_DIR" ]; then
    for subdir in platforms platformthemes platforminputcontexts imageformats iconengines xcbglintegrations; do
        if [ -d "${QT_PLUGIN_DIR}/${subdir}" ]; then
            cp -r "${QT_PLUGIN_DIR}/${subdir}" "${PKG_DIR}/plugins/"
        fi
    done
    # Collect libs needed by Qt plugins
    find "${PKG_DIR}/plugins" -name "*.so" | while read -r plugin_so; do
        collect_libs "$plugin_so" | while read -r lib; do
            cp -n "$lib" "${PKG_DIR}/lib/" 2>/dev/null || true
        done
    done
fi

# --- Config files ---
SRC_DIR="$(dirname "$0")/.."
cp "${SRC_DIR}/files/settings-default.cfg" "${PKG_DIR}/defaults.bin" 2>/dev/null || \
    cp "${BUILD_DIR}/defaults.bin" "${PKG_DIR}/" 2>/dev/null || true
cp "${BUILD_DIR}/settings-default.cfg" "${PKG_DIR}/defaults.bin" 2>/dev/null || true
# Try to find defaults.bin from the install
find "${BUILD_DIR}" -name "defaults.bin" -exec cp {} "${PKG_DIR}/" \; 2>/dev/null || true
cp "${SRC_DIR}/files/gamecontrollerdb.txt" "${PKG_DIR}/"
cp "${SRC_DIR}/files/openmw.cfg" "${PKG_DIR}/" 2>/dev/null || \
    cp "${SRC_DIR}/files/openmw.cfg.local" "${PKG_DIR}/openmw.cfg" 2>/dev/null || true
# Fix resource paths for portable layout (wrapper scripts cd into gamedir)
sed -i 's|resources=${OPENMW_RESOURCE_FILES}|resources=resources|' "${PKG_DIR}/openmw.cfg"
sed -i 's|data=${OPENMW_RESOURCE_FILES}/vfs-mw|data=resources/vfs-mw|' "${PKG_DIR}/openmw.cfg"

# --- Resources ---
if [ -d "${BUILD_DIR}/resources" ]; then
    cp -r "${BUILD_DIR}/resources" "${PKG_DIR}/"
elif [ -d "${SRC_DIR}/files/resources" ]; then
    cp -r "${SRC_DIR}/files/resources" "${PKG_DIR}/"
fi

# --- Desktop files & icons ---
cp "${SRC_DIR}/files/org.openmw.launcher.desktop" "${PKG_DIR}/desktop files/" 2>/dev/null || true
cp "${SRC_DIR}/files/org.openmw.cs.desktop" "${PKG_DIR}/desktop files/" 2>/dev/null || true
cp "${SRC_DIR}/files/launcher/images/openmw.png" "${PKG_DIR}/" 2>/dev/null || true

# --- Docs ---
cp "${SRC_DIR}/CHANGELOG.md" "${PKG_DIR}/CHANGELOG.txt" 2>/dev/null || true
cp "${SRC_DIR}/LICENSE" "${PKG_DIR}/LICENSE.txt" 2>/dev/null || true
cp "${SRC_DIR}/README.md" "${PKG_DIR}/" 2>/dev/null || true

# --- Tarball ---
echo "Creating ${OUTPUT_NAME}.tar.gz..."
tar czf "${OUTPUT_NAME}.tar.gz" "${PKG_DIR}"
echo "Done: ${OUTPUT_NAME}.tar.gz ($(du -sh "${OUTPUT_NAME}.tar.gz" | cut -f1))"
echo "Contents: $(find "${PKG_DIR}" -type f | wc -l) files, $(du -sh "${PKG_DIR}" | cut -f1) uncompressed"
