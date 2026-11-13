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
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "$JOBS" in ''|*[!0-9]*|0*) printf 'Build parallel level must be a positive integer without leading zeros.\n' >&2; exit 2 ;; esac
if [ -e "$ROOT" ] || [ -L "$ROOT" ]; then
    printf 'Choose a new toolchain directory. Existing paths are never overwritten.\n' >&2
    exit 2
fi
if [ "$(uname -s)" != Darwin ]; then
    printf 'The iOS toolchain requires macOS and Xcode.\n' >&2
    exit 2
fi
for tool in cmake ninja make perl curl shasum tar xcrun xcodebuild install_name_tool otool lipo nm git; do
    command -v "$tool" >/dev/null 2>&1 || { printf 'Required tool is missing: %s\n' "$tool" >&2; exit 2; }
done
if [ ! -x "$HOST/bin/qmake" ] || [ "$("$HOST/bin/qmake" -query QT_VERSION)" != 6.11.3 ] || [ ! -x "$HOST/bin/qsb" ]; then
    printf 'Host Qt 6.11.3 including qtshadertools is required.\n' >&2
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
    lipo "$framework/$name" -verify_arch "$ARCH"
    xcodebuild -create-xcframework -framework "$framework" -output "$ROOT/media/lib/$name.xcframework" >> "$ROOT/ffmpeg.log" 2>&1
done

while read -r module checksum; do
    fetch "$module" "$checksum" "https://download.qt.io/archive/qt/6.11/6.11.3/submodules/$module-everywhere-src-6.11.3.tar.xz"
    if [ "$module" = qtdeclarative ]; then
        cmake "-DQT_SOURCE_DIR=$ROOT/$module" -P "$SCRIPT_DIR/qt-quick-shutdown.cmake"
    fi
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
    if [ "$module" = qtdeclarative ]; then
        cp "$ROOT/$module/podlord-qtquick-patch.txt" "$ROOT/qt/podlord-qtquick-patch.txt"
    fi
    printf 'Installed Qt module: %s\n' "$module"
done <<'EOF'
qtbase 46de0710b594945a5d446ae44354daa386141c6903c53781b2ca547bc62f8f8f
qtshadertools d86079425ec71eecbb05f4d20623b0a403a7d21a5cfa7efb9734ea3d538d801b
qtdeclarative 6d6db328a02ae3bb0b1e6944aeef8dfe44cd6b55ab59b1708bbf0400f5759cb5
qtsvg 9bdf6fa4b5a2d91c3eaa2edd12263fdff0ae19d0ac25d3c99946cd4189a07123
qtimageformats 2759929c7abbc5376120394d1ccac1abbb816bc2791b34ece6c1412d323e6213
qtwebsockets 19b39acc37ec67a344fe0f080201c2c64d04d2a3d0c7dfa84955033f1016983b
qtmultimedia 3d172de9bdd137686c3871762a4adff988a1ec0faec15df0c4560cfc417288e6
EOF

test -f "$ROOT/qt/plugins/tls/libqopensslbackend.a"
test ! -e "$ROOT/qt/plugins/tls/libqsecuretransportbackend.a"
cat > "$ROOT/build-inputs.txt" <<EOF
Qt: 6.11.3, pinned upstream Quick shutdown fix, linked OpenSSL, SecureTransport disabled
Host Qt: $HOST
Target: $SDK $ARCH, iOS 16.0 minimum
OpenSSL: 3.5.9 LTS, embedded static libraries
FFmpeg: 7.1.5, embedded frameworks, GPL/nonfree components disabled
yaml-cpp: 0.9.0
Source SHA256 values: scripts/build-native-ios-toolchain.sh
EOF
cat "$ROOT/qt/podlord-qtquick-patch.txt" >> "$ROOT/build-inputs.txt"
cmake "-DPODLORD_IOS_SDK_ROOT=$ROOT" -P "$(dirname "$0")/install-native-ios-privacy.cmake"
trap - EXIT HUP INT TERM
printf 'Toolchain ready: %s/qt\nYAML package: %s/yaml/lib/cmake/yaml-cpp\n' "$ROOT" "$ROOT"
