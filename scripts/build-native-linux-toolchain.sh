#!/bin/sh
set -eu
umask 022
if [ "$#" -ne 1 ]; then
    printf 'Usage: %s /absolute/new/toolchain-directory\n' "$0" >&2
    exit 2
fi
case "$1" in /*) ROOT=$1 ;; *) printf 'Absolute toolchain path required.\n' >&2; exit 2 ;; esac
if [ "$(uname -s)" != Linux ] || [ -e "$ROOT" ] || [ -L "$ROOT" ]; then
    printf 'Requires Linux and a new toolchain directory.\n' >&2
    exit 2
fi
JOBS=${PODLORD_BUILD_JOBS:-4}
mkdir -p "$ROOT/sources" "$ROOT/logs"
CURRENT_LOG=
report_failure() {
    status=$?
    if [ "$status" -ne 0 ] && [ -n "$CURRENT_LOG" ] && [ -f "$CURRENT_LOG" ]; then
        tail -n 80 "$CURRENT_LOG" >&2
    fi
}
trap report_failure 0
fetch() {
    name=$1; checksum=$2; url=$3
    archive="$ROOT/sources/${url##*/}"
    curl --fail --location --retry 3 --connect-timeout 30 --speed-time 60 --speed-limit 1024 "$url" -o "$archive"
    printf '%s  %s\n' "$checksum" "$archive" | sha256sum --check -
    mkdir "$ROOT/$name"
    tar -xf "$archive" --strip-components=1 -C "$ROOT/$name"
}
fetch yaml-cpp 25cb043240f828a8c51beb830569634bc7ac603978e0f69d6b63558dadefd49a \
    https://github.com/jbeder/yaml-cpp/archive/refs/tags/yaml-cpp-0.9.0.tar.gz
CURRENT_LOG="$ROOT/logs/yaml-cpp.log"
cmake -S "$ROOT/yaml-cpp" -B "$ROOT/yaml-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$ROOT/yaml" -DYAML_BUILD_SHARED_LIBS=OFF \
    -DYAML_CPP_BUILD_TESTS=OFF -DYAML_CPP_BUILD_TOOLS=OFF > "$ROOT/logs/yaml-cpp.log" 2>&1
cmake --build "$ROOT/yaml-build" --parallel "$JOBS" >> "$ROOT/logs/yaml-cpp.log" 2>&1
cmake --install "$ROOT/yaml-build" >> "$ROOT/logs/yaml-cpp.log" 2>&1
rm -r "$ROOT/yaml-cpp" "$ROOT/yaml-build"

# Same FFmpeg release/configuration family as the maintained iOS toolchain.
fetch ffmpeg de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f \
    https://ffmpeg.org/releases/ffmpeg-7.1.5.tar.xz
mkdir "$ROOT/ffmpeg-build"
cd "$ROOT/ffmpeg-build"
CURRENT_LOG="$ROOT/logs/ffmpeg.log"
"$ROOT/ffmpeg/configure" --prefix="$ROOT/media" --disable-programs --disable-doc \
    --disable-static --enable-shared --enable-network --disable-autodetect \
    --cc="${CC:-cc}" --cxx="${CXX:-c++}" > "$ROOT/logs/ffmpeg.log" 2>&1
make -j"$JOBS" >> "$ROOT/logs/ffmpeg.log" 2>&1
make install >> "$ROOT/logs/ffmpeg.log" 2>&1
cp "$ROOT/ffmpeg/COPYING.LGPLv2.1" "$ROOT/media/FFmpeg-LICENSE.txt"
cd "$ROOT"
rm -r "$ROOT/ffmpeg-build" "$ROOT/ffmpeg"

while read -r module checksum; do
    fetch "$module" "$checksum" "https://download.qt.io/archive/qt/6.11/6.11.2/submodules/$module-everywhere-src-6.11.2.tar.xz"
    mkdir "$ROOT/$module-build"
    cd "$ROOT/$module-build"
    CURRENT_LOG="$ROOT/logs/$module.log"
    if [ "$module" = qtbase ]; then
        "$ROOT/qtbase/configure" -release -shared -opensource -confirm-license \
            -prefix "$ROOT/qt" -no-icu -no-widgets -openssl-runtime -sbom \
            -qt-pcre -qt-harfbuzz -qt-libpng -qt-libjpeg \
            -nomake tests -nomake examples -- -DPython3_EXECUTABLE=/opt/sbom/bin/python \
            > "$ROOT/logs/$module.log" 2>&1
    elif [ "$module" = qtmultimedia ]; then
        "$ROOT/qt/bin/qt-configure-module" "$ROOT/$module" -- \
            -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF -DFFMPEG_DIR="$ROOT/media" \
            -DQT_FEATURE_ffmpeg=ON -DQT_DEPLOY_FFMPEG=ON > "$ROOT/logs/$module.log" 2>&1
    else
        "$ROOT/qt/bin/qt-configure-module" "$ROOT/$module" -- \
            -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF > "$ROOT/logs/$module.log" 2>&1
    fi
    cmake --build . --parallel "$JOBS" >> "$ROOT/logs/$module.log" 2>&1
    cmake --install . >> "$ROOT/logs/$module.log" 2>&1
    cd "$ROOT"
    rm -r "${ROOT:?}/${module:?}-build" "${ROOT:?}/${module:?}"
    printf 'Installed Qt module: %s\n' "$module"
done <<'EOF'
qtbase 5b2e00eccaf5a4d8c14134ffa0ea8dfd0a35ae1ffc7f8d87fa4305a1ed23cf22
qtshadertools 805046b8b7757665586890b375940047e874ae3ab00adb6d3f2b38fc6b200b1c
qtdeclarative 215b7b70517e380123eabc6b92243f3c47b6f016a91d126057dbe53551c6b430
qtsvg d594337feca84c26fb67fe87b85e6a5c12fda404b611d905f9d138210c311876
qtwebsockets 2bfe25c383c37f7690e3c59b02c9907d2744cf8527b1f3f1fc2c785b2ff3c9a3
qtwayland 8eb7615e39332a10f506e8dd70f02d5954bb5949ff54f6dcbf8bd6168222f9df
qtmultimedia 967b5e02ec6b793cdb360622cd6e703132836af983208d678dae4b50f109cd9f
EOF
test "$("$ROOT/qt/bin/qmake" -query QT_VERSION)" = 6.11.2
if readelf -d "$ROOT/qt/lib/libQt6Core.so.6" | grep 'NEEDED.*libicu'; then
    printf 'Unexpected ICU runtime dependency.\n' >&2
    exit 1
fi
{
    printf 'Qt: 6.11.2, shared, ICU and unused Widgets disabled, no source patches\n'
    printf 'FFmpeg: 7.1.5, shared, GPL/nonfree/autodetected external components disabled\n'
    printf 'yaml-cpp: 0.9.0, static\nArchitecture: '; uname -m
    printf 'Build ABI: '; ldd --version | head -n 1
    printf 'Source archive SHA256 values:\n'
    sha256sum "$ROOT/sources/"*
    cmake --version
    "${CXX:-c++}" --version
} > "$ROOT/build-inputs.txt"
