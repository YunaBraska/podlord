#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    printf 'Usage: %s iphoneos|iphonesimulator HOST_QT NEW_OUTPUT_DIRECTORY\n' "$0" >&2
    exit 2
fi
SDK=$1
HOST=$2
ROOT=$3
case "$SDK" in iphoneos|iphonesimulator) ;; *) printf 'Unsupported iOS SDK: %s\n' "$SDK" >&2; exit 2 ;; esac
case "$HOST" in /*) ;; *) printf 'Host Qt path must be absolute.\n' >&2; exit 2 ;; esac
case "$ROOT" in /*) ;; *) printf 'Output directory must be absolute.\n' >&2; exit 2 ;; esac
JOBS=${CMAKE_BUILD_PARALLEL_LEVEL:-6}
case "$JOBS" in ''|*[!0-9]*|0*) printf 'Build parallel level must be a positive integer without leading zeros.\n' >&2; exit 2 ;; esac
if [ -e "$ROOT" ] || [ -L "$ROOT" ]; then
    printf 'Choose a new toolchain directory. Existing paths are never overwritten.\n' >&2
    exit 2
fi
if [ "$(uname -s)" != Darwin ]; then
    printf 'The iOS toolchain requires macOS and Xcode.\n' >&2
    exit 2
fi
for tool in cmake ninja make perl curl shasum tar xcrun xcodebuild install_name_tool otool lipo nm; do
    command -v "$tool" >/dev/null 2>&1 || { printf 'Required tool is missing: %s\n' "$tool" >&2; exit 2; }
done
if [ ! -x "$HOST/bin/qmake" ] || [ "$("$HOST/bin/qmake" -query QT_VERSION)" != 6.11.2 ] || [ ! -x "$HOST/bin/qsb" ]; then
    printf 'Host Qt 6.11.2 including qtshadertools is required.\n' >&2
    exit 2
fi
ARCH=arm64
case "$SDK" in
    iphoneos) SSL_TARGET=ios64-xcrun; MINIMUM=-miphoneos-version-min=16.0; PLATFORM=iPhoneOS ;;
    iphonesimulator)
        ARCH=$(uname -m)
        case "$ARCH" in
            arm64) SSL_TARGET=iossimulator-arm64-xcrun ;;
            x86_64) SSL_TARGET=iossimulator-x86_64-xcrun ;;
            *) printf 'Unsupported simulator architecture: %s\n' "$ARCH" >&2; exit 2 ;;
        esac
        MINIMUM=-mios-simulator-version-min=16.0
        PLATFORM=iPhoneSimulator ;;
esac
SYSROOT=$(xcrun --sdk "$SDK" --show-sdk-path)
mkdir -p "$ROOT"
ROOT=$(cd "$ROOT" && pwd -P)
trap 'printf "Toolchain incomplete. Build files and logs are retained in %s\n" "$ROOT" >&2' EXIT
trap 'exit 130' INT
trap 'exit 143' HUP TERM

fetch() {
    name=$1
    checksum=$2
    url=$3
    curl --fail --location --silent --show-error --retry 2 --connect-timeout 15 --max-time 600 "$url" -o "$ROOT/$name.tar"
    printf '%s  %s\n' "$checksum" "$ROOT/$name.tar" | shasum -a 256 -c -
    mkdir "$ROOT/$name"
    tar -xf "$ROOT/$name.tar" --strip-components=1 -C "$ROOT/$name"
}

fetch openssl 603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a \
    https://github.com/openssl/openssl/releases/download/openssl-3.5.9/openssl-3.5.9.tar.gz
mkdir "$ROOT/openssl-build"
cd "$ROOT/openssl-build"
"$ROOT/openssl/Configure" "$SSL_TARGET" no-shared no-tests no-apps --prefix="$ROOT/ssl" "$MINIMUM" > "$ROOT/openssl.log" 2>&1
make -j"$JOBS" >> "$ROOT/openssl.log" 2>&1
make install_sw >> "$ROOT/openssl.log" 2>&1

fetch yaml-cpp 25cb043240f828a8c51beb830569634bc7ac603978e0f69d6b63558dadefd49a \
    https://github.com/jbeder/yaml-cpp/archive/refs/tags/yaml-cpp-0.9.0.tar.gz
cmake -S "$ROOT/yaml-cpp" -B "$ROOT/yaml-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$SDK" \
    -DCMAKE_OSX_ARCHITECTURES="$ARCH" -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 \
    -DCMAKE_INSTALL_PREFIX="$ROOT/yaml" -DBUILD_SHARED_LIBS=OFF \
    -DYAML_CPP_BUILD_TESTS=OFF -DYAML_CPP_BUILD_TOOLS=OFF -DYAML_CPP_BUILD_CONTRIB=OFF > "$ROOT/yaml.log" 2>&1
cmake --build "$ROOT/yaml-build" --parallel "$JOBS" >> "$ROOT/yaml.log" 2>&1
cmake --install "$ROOT/yaml-build" >> "$ROOT/yaml.log" 2>&1

# Same FFmpeg release as the installed Qt iOS SDK, without host-library autodetection.
fetch ffmpeg de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f \
    https://ffmpeg.org/releases/ffmpeg-7.1.5.tar.xz
mkdir "$ROOT/ffmpeg-build"
cd "$ROOT/ffmpeg-build"
"$ROOT/ffmpeg/configure" --disable-programs --disable-doc --enable-network \
    --enable-shared --disable-static --enable-cross-compile --target-os=darwin --arch="$ARCH" \
    --sysroot="$SYSROOT" --prefix="$ROOT/media" \
    --cc="xcrun --sdk $SDK clang -arch $ARCH" --cxx="xcrun --sdk $SDK clang++ -arch $ARCH" \
    --extra-cflags="$MINIMUM" --extra-cxxflags="$MINIMUM" --extra-objcflags="$MINIMUM" \
    --extra-ldflags="$MINIMUM" --install-name-dir=@rpath --disable-audiotoolbox --disable-autodetect \
    --enable-videotoolbox > "$ROOT/ffmpeg.log" 2>&1
make -j"$JOBS" >> "$ROOT/ffmpeg.log" 2>&1
make install >> "$ROOT/ffmpeg.log" 2>&1
nm -gU "$ROOT/media/lib/libavutil.dylib" | awk '$3 == "_av_map_videotoolbox_format_to_pixfmt" {found=1} END {exit !found}'
for component in avcodec avformat avutil swresample swscale; do
    name=lib$component
    framework=$ROOT/media/frameworks/$name.framework
    mkdir -p "$framework"
    cp "$ROOT/media/lib/$name.dylib" "$framework/$name"
    install_name_tool -id "@rpath/$name.framework/$name" "$framework/$name"
    otool -L "$framework/$name" | awk '/@rpath\/lib.*[.]dylib/ { print $1 }' | while IFS= read -r dependency; do
        library=${dependency##*/}
        library=${library%%.*}
        install_name_tool -change "$dependency" "@rpath/$library.framework/$library" "$framework/$name"
    done
    cat > "$framework/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>$name</string>
<key>CFBundleIdentifier</key><string>berlin.yuna.podlord.ffmpeg.$component</string>
<key>CFBundleName</key><string>$name</string>
<key>CFBundlePackageType</key><string>FMWK</string>
<key>CFBundleShortVersionString</key><string>7.1.5</string>
<key>CFBundleVersion</key><string>7.1.5</string>
<key>MinimumOSVersion</key><string>16.0</string>
<key>CFBundleSupportedPlatforms</key><array><string>$PLATFORM</string></array>
</dict></plist>
EOF
    lipo -verify_arch "$ARCH" "$framework/$name"
    xcodebuild -create-xcframework -framework "$framework" -output "$ROOT/media/lib/$name.xcframework" >> "$ROOT/ffmpeg.log" 2>&1
done

while read -r module checksum; do
    fetch "$module" "$checksum" "https://download.qt.io/archive/qt/6.11/6.11.2/submodules/$module-everywhere-src-6.11.2.tar.xz"
    mkdir "$ROOT/$module-build"
    cd "$ROOT/$module-build"
    if [ "$module" = qtbase ]; then
        "$ROOT/qtbase/configure" -platform macx-ios-clang -sdk "$SDK" -release -static \
            -opensource -confirm-license -prefix "$ROOT/qt" -qt-host-path "$HOST" \
            -openssl-linked -no-securetransport -nomake tests -nomake examples -- \
            -DCMAKE_OSX_ARCHITECTURES="$ARCH" -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 \
            -DOPENSSL_ROOT_DIR="$ROOT/ssl" -DOPENSSL_USE_STATIC_LIBS=ON > "$ROOT/$module.log" 2>&1
    elif [ "$module" = qtmultimedia ]; then
        "$ROOT/qt/bin/qt-configure-module" "$ROOT/$module" -- \
            -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF -DFFMPEG_DIR="$ROOT/media" \
            -DQT_FEATURE_ffmpeg=ON -DQT_DEPLOY_FFMPEG=ON > "$ROOT/$module.log" 2>&1
    else
        "$ROOT/qt/bin/qt-configure-module" "$ROOT/$module" -- \
            -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF > "$ROOT/$module.log" 2>&1
    fi
    cmake --build . --parallel "$JOBS" >> "$ROOT/$module.log" 2>&1
    cmake --install . >> "$ROOT/$module.log" 2>&1
    printf 'Installed Qt module: %s\n' "$module"
done <<'EOF'
qtbase 5b2e00eccaf5a4d8c14134ffa0ea8dfd0a35ae1ffc7f8d87fa4305a1ed23cf22
qtshadertools 805046b8b7757665586890b375940047e874ae3ab00adb6d3f2b38fc6b200b1c
qtdeclarative 215b7b70517e380123eabc6b92243f3c47b6f016a91d126057dbe53551c6b430
qtsvg d594337feca84c26fb67fe87b85e6a5c12fda404b611d905f9d138210c311876
qtimageformats cecd8900f34b6550076309bc94f62f828008b633a4239e0a08c86788f41001f8
qtwebsockets 2bfe25c383c37f7690e3c59b02c9907d2744cf8527b1f3f1fc2c785b2ff3c9a3
qtmultimedia 967b5e02ec6b793cdb360622cd6e703132836af983208d678dae4b50f109cd9f
EOF

test -f "$ROOT/qt/plugins/tls/libqopensslbackend.a"
test ! -e "$ROOT/qt/plugins/tls/libqsecuretransportbackend.a"
cat > "$ROOT/build-inputs.txt" <<EOF
Qt: 6.11.2, stock source, linked OpenSSL, SecureTransport disabled
Host Qt: $HOST
Target: $SDK $ARCH, iOS 16.0 minimum
OpenSSL: 3.5.9 LTS, embedded static libraries
FFmpeg: 7.1.5, embedded frameworks, GPL/nonfree components disabled
yaml-cpp: 0.9.0
Source SHA256 values: scripts/build-native-ios-toolchain.sh
EOF
trap - EXIT HUP INT TERM
printf 'Toolchain ready: %s/qt\nYAML package: %s/yaml/lib/cmake/yaml-cpp\n' "$ROOT" "$ROOT"
