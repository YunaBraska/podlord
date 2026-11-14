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
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
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
    fetch "$module" "$checksum" "https://download.qt.io/archive/qt/6.11/6.11.3/submodules/$module-everywhere-src-6.11.3.tar.xz"
    if [ "$module" = qtdeclarative ]; then
        cmake "-DQT_SOURCE_DIR=$ROOT/$module" -P "$SCRIPT_DIR/qt-quick-shutdown.cmake"
    fi
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
    if [ "$module" = qtdeclarative ]; then
        cp "$ROOT/$module/podlord-qtquick-patch.txt" "$ROOT/qt/podlord-qtquick-patch.txt"
    fi
    cd "$ROOT"
    rm -r "${ROOT:?}/${module:?}-build" "${ROOT:?}/${module:?}"
    printf 'Installed Qt module: %s\n' "$module"
done <<'EOF'
qtbase 46de0710b594945a5d446ae44354daa386141c6903c53781b2ca547bc62f8f8f
qtshadertools d86079425ec71eecbb05f4d20623b0a403a7d21a5cfa7efb9734ea3d538d801b
qtdeclarative 6d6db328a02ae3bb0b1e6944aeef8dfe44cd6b55ab59b1708bbf0400f5759cb5
qtsvg 9bdf6fa4b5a2d91c3eaa2edd12263fdff0ae19d0ac25d3c99946cd4189a07123
qtwebsockets 19b39acc37ec67a344fe0f080201c2c64d04d2a3d0c7dfa84955033f1016983b
qtwayland 7f6185eb6e7d75202c42dfaf18dc54ae96f2d9a7a265358d061a6f85366f6c97
qtmultimedia 3d172de9bdd137686c3871762a4adff988a1ec0faec15df0c4560cfc417288e6
EOF
test "$("$ROOT/qt/bin/qmake" -query QT_VERSION)" = 6.11.3
if readelf -d "$ROOT/qt/lib/libQt6Core.so.6" | grep 'NEEDED.*libicu'; then
    printf 'Unexpected ICU runtime dependency.\n' >&2
    exit 1
fi
{
    printf 'Qt: 6.11.3, shared, ICU and unused Widgets disabled, pinned upstream Quick shutdown fix\n'
    cat "$ROOT/qt/podlord-qtquick-patch.txt"
    printf 'FFmpeg: 7.1.5, shared, GPL/nonfree/autodetected external components disabled\n'
    printf 'yaml-cpp: 0.9.0, static\nArchitecture: '; uname -m
    printf 'Build ABI: '; ldd --version | head -n 1
    openssl version -a
    dpkg-query -W -f='${binary:Package} ${Version}\n' libssl3t64 libc6
    ssl_libdir=$(pkg-config --variable=libdir libcrypto)
    sha256sum "$ssl_libdir/libcrypto.so.3" "$ssl_libdir/libssl.so.3"
    printf 'Source archive SHA256 values:\n'
    sha256sum "$ROOT/sources/"*
    cmake --version
    "${CXX:-c++}" --version
} > "$ROOT/build-inputs.txt"
CURRENT_LOG="$ROOT/logs/qt-shutdown-check.log"
cmake -S "$SCRIPT_DIR/../native/tests/qt_shutdown" -B "$ROOT/shutdown-check" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$ROOT/qt" > "$CURRENT_LOG" 2>&1
cmake --build "$ROOT/shutdown-check" --parallel "$JOBS" >> "$CURRENT_LOG" 2>&1
ctest --test-dir "$ROOT/shutdown-check" --output-on-failure >> "$CURRENT_LOG" 2>&1
rm -r "$ROOT/shutdown-check"
