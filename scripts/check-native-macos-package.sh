#!/bin/sh
set -eu
umask 077
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ "$#" -ne 1 ]; then
    printf 'Usage: %s /absolute/new/evidence-directory\n' "$0" >&2
    exit 2
fi
case "$1" in
    /*) OUTPUT=$1 ;;
    *) printf 'Evidence directory must be absolute.\n' >&2; exit 2 ;;
esac
if [ -e "$OUTPUT" ] || [ -L "$OUTPUT" ]; then
    printf 'Evidence directory already exists; refusing to replace it.\n' >&2
    exit 2
fi
if [ "$(uname -s)" != Darwin ]; then
    printf 'This package check supports macOS only.\n' >&2
    exit 2
fi
find_tool() {
    if command -v "$1" >/dev/null 2>&1; then command -v "$1"
    elif [ -x "/opt/homebrew/bin/$1" ]; then printf '/opt/homebrew/bin/%s\n' "$1"
    elif [ -x "/opt/homebrew/opt/qtbase/bin/$1" ]; then printf '/opt/homebrew/opt/qtbase/bin/%s\n' "$1"
    else printf 'Required build tool is missing: %s\n' "$1" >&2; return 1
    fi
}
CMAKE=$(find_tool cmake)
PARENT=$(dirname -- "$OUTPUT")
mkdir -p "$PARENT"
WORK=$(mktemp -d "$PARENT/.podlord-package.XXXXXX")
cleanup() { rm -rf "$WORK"; }
trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
"$CMAKE" -S "$ROOT/native" -B "$WORK/build" \
    -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH:-/opt/homebrew}" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DPODLORD_COVERAGE=OFF
QT_CORE_DIR=$(sed -n 's/^Qt6Core_DIR:PATH=//p' "$WORK/build/CMakeCache.txt")
if [ -z "$QT_CORE_DIR" ] || [ ! -d "$QT_CORE_DIR" ]; then
    printf 'The configured Qt Core SDK could not be resolved.\n' >&2
    exit 1
fi
QT_INSTALL=$(CDPATH= cd -- "$QT_CORE_DIR/../../.." && pwd)
DEPLOY="$QT_INSTALL/bin/macdeployqt"
if [ ! -x "$DEPLOY" ]; then
    printf 'The configured Qt SDK has no macdeployqt: %s\n' "$DEPLOY" >&2
    exit 1
fi
"$CMAKE" --build "$WORK/build" --target podlord-native --parallel "${PODLORD_BUILD_JOBS:-4}"
APP="$WORK/build/podlord-native.app"
"$DEPLOY" "$APP" -qmldir="$ROOT/native/ui" -verbose=2
# Preserve the notices and matching supplier metadata with the deployed binaries.
NOTICES="$APP/Contents/Resources/licenses"
mkdir -p "$NOTICES/qt-spdx"
cp "$ROOT/LICENSE" "$NOTICES/Podlord-LICENSE.txt"
cp "$ROOT/native/licenses/"*.txt "$NOTICES/"
for module in qtbase qtdeclarative qtmultimedia qtsvg qtwebsockets; do
    found=false
    for metadata in "$QT_INSTALL/sbom/$module-"*.spdx.json; do
        if [ -f "$metadata" ]; then
            cp "$metadata" "$NOTICES/qt-spdx/"
            found=true
        fi
    done
    if [ "$found" != true ]; then
        printf 'Required Qt supplier license metadata is missing: %s/sbom/%s\n' "$QT_INSTALL" "$module" >&2
        exit 1
    fi
done
# Product palettes use Fusion; Basic is its supported fallback/test style.
# Runtime-selected controls otherwise cause deployment of every Qt style.
for style in Imagine Material Universal FluentWinUI3 iOS macOS Windows; do
    rm -rf "$APP/Contents/Resources/qml/QtQuick/Controls/$style"
done
for style in imagine material universal fluentwinui3 ios macos windows; do
    rm -f "$APP/Contents/PlugIns/quick/libqtquickcontrols2${style}styleplugin.dylib" \
        "$APP/Contents/PlugIns/quick/libqtquickcontrols2${style}styleimplplugin.dylib"
done
for style in Imagine Material Universal FluentWinUI3 IOS MacOS Windows; do
    rm -rf "$APP/Contents/Frameworks/QtQuickControls2${style}.framework" \
        "$APP/Contents/Frameworks/QtQuickControls2${style}StyleImpl.framework"
done
# This QGuiApplication never uses QWidget controls or their style plugin.
rm -f "$APP/Contents/PlugIns/styles/libqmacstyle.dylib"
rm -rf "$APP/Contents/Frameworks/QtWidgets.framework"
rm -f "$APP/Contents/PlugIns/quick/libqtquicktimelineplugin.dylib" \
    "$APP/Contents/PlugIns/quick/libqtquicktimelineblendtreesplugin.dylib"
rm -rf "$APP/Contents/Resources/qml/QtQuick/Timeline"
rm -rf "$APP/Contents/Frameworks/QtQuickTimeline.framework" \
    "$APP/Contents/Frameworks/QtQuickTimelineBlendTrees.framework"
# The application has no ODBC, PostgreSQL or Mimer database integration.
# Keep SQLite for Qt's deployed local-storage module.
rm -f "$APP/Contents/PlugIns/sqldrivers/libqsqlodbc.dylib" \
    "$APP/Contents/PlugIns/sqldrivers/libqsqlpsql.dylib" \
    "$APP/Contents/PlugIns/sqldrivers/libqsqlmimer.dylib"
# Single-architecture apps do not need the SDK's other architecture slices.
# Universal apps retain all slices and must pass the runtime architecture gate.
ARCHITECTURES=$(/usr/bin/lipo -archs "$APP/Contents/MacOS/podlord-native")
case "$ARCHITECTURES" in
    arm64|x86_64)
        find "$APP" -type f \( -name '*.dylib' -o -path '*.framework/Versions/A/Qt*' -o -path '*/Contents/MacOS/*' \) > "$WORK/runtime-files"
        while IFS= read -r binary; do
            actual=$(/usr/bin/lipo -archs "$binary")
            if [ "$actual" != "$ARCHITECTURES" ]; then
                /usr/bin/lipo "$binary" -thin "$ARCHITECTURES" -output "$WORK/runtime-thin"
                mv "$WORK/runtime-thin" "$binary"
            fi
        done < "$WORK/runtime-files"
        ;;
esac
find "$APP/Contents/Frameworks" -type f -name '*.dylib' \
    -exec /usr/bin/codesign --force --sign - '{}' +
/usr/bin/codesign --force --sign - "$APP"
/usr/bin/codesign --verify --deep --strict "$APP"
mkdir "$WORK/evidence"
mv "$APP" "$WORK/evidence/podlord-native.app"
APP="$WORK/evidence/podlord-native.app"
find "$APP" -type f \( -name '*.dylib' -o -path '*.framework/Versions/A/Qt*' -o -path '*/Contents/MacOS/*' \) \
    -exec /usr/bin/otool -L '{}' + > "$WORK/evidence/libraries.txt"
DEPENDENCIES=pass
if ! /bin/sh "$ROOT/scripts/check-native-macos-dependencies.sh" "$APP" > "$WORK/evidence/runtime-dependencies.txt"; then DEPENDENCIES=fail; fi
/usr/bin/ditto -c -k --sequesterRsrc --keepParent "$APP" "$WORK/evidence/podlord-native.zip"
DOWNLOAD=$(/usr/bin/stat -f '%z' "$WORK/evidence/podlord-native.zip")
INSTALLED=$(find "$APP" -type f -exec /usr/bin/stat -f '%z' '{}' + | awk '{total+=$1} END {printf "%.0f\n",total}')
{
    printf 'Platform: %s\n' "$(uname -m)"
    /usr/bin/sw_vers
    printf 'Build: Release; BUILD_TESTING=OFF; PODLORD_COVERAGE=OFF\n'
    printf 'Dependency paths: %s\nDownload bytes (ZIP): %s\nInstalled bytes (regular-file logical sum): %s\n' "$DEPENDENCIES" "$DOWNLOAD" "$INSTALLED"
    printf 'Limits: download 50000000 bytes; installed 100000000 bytes.\n'
    printf 'Signing: ad-hoc local verification, not Developer ID/notarization.\n'
    printf 'Scope: packaging preflight only; clean-device startup, runtime-loaded plugins, licenses, functional parity and other targets remain separate gates.\n'
    (cd "$WORK/evidence" && /usr/bin/shasum -a 256 podlord-native.app/Contents/MacOS/podlord-native podlord-native.zip)
} > "$WORK/evidence/package-evidence.txt"
cat "$WORK/evidence/package-evidence.txt"
mkdir "$OUTPUT"
mv "$WORK/evidence/podlord-native.app" "$WORK/evidence/podlord-native.zip" \
    "$WORK/evidence/libraries.txt" "$WORK/evidence/runtime-dependencies.txt" \
    "$WORK/evidence/package-evidence.txt" "$OUTPUT/"
if [ "$DEPENDENCIES" != pass ] || [ "$DOWNLOAD" -gt 50000000 ] || [ "$INSTALLED" -gt 100000000 ]; then
    printf 'Package gate failed; measurements retained in %s.\n' "$OUTPUT" >&2
    exit 1
fi
