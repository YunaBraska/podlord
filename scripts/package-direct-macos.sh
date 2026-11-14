#!/bin/sh
set -eu
umask 077

[ "$#" -eq 6 ] || {
    printf 'Usage: %s VERSION SIGNING_IDENTITY NOTARY_PROFILE INPUT_ZIP ARCH OUTPUT_DIRECTORY\n' "$0" >&2
    exit 2
}

VERSION=$1
IDENTITY=$2
PROFILE=$3
ARCHIVE=$4
ARCH=$5
OUTPUT=$6

printf '%s\n' "$VERSION" | /usr/bin/grep -Eq '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$' || {
    printf 'Version must be a three-part semantic version.\n' >&2
    exit 2
}
[ -n "$IDENTITY" ] && [ -n "$PROFILE" ] || {
    printf 'Developer ID signing and notarization credentials are required.\n' >&2
    exit 2
}
[ -f "$ARCHIVE" ] && [ ! -L "$ARCHIVE" ] || {
    printf 'Input must be a regular candidate archive.\n' >&2
    exit 2
}
case "$ARCH" in
    arm64) EXPECTED_ARCH=arm64 ;;
    x64) EXPECTED_ARCH=x86_64 ;;
    *) printf 'Architecture must be arm64 or x64.\n' >&2; exit 2 ;;
esac
[ -d "$OUTPUT" ] && [ ! -L "$OUTPUT" ] || {
    printf 'Output directory must already exist and not be a symlink.\n' >&2
    exit 2
}

ZIP="$OUTPUT/podlord-native-macos-$ARCH.zip"
DMG="$OUTPUT/podlord-native-macos-$ARCH.dmg"
[ ! -e "$ZIP" ] && [ ! -L "$ZIP" ] && [ ! -e "$DMG" ] && [ ! -L "$DMG" ] || {
    printf 'Refusing to replace an existing direct distribution asset.\n' >&2
    exit 2
}

RUN=$(mktemp -d "${TMPDIR:-/tmp}/podlord-direct-package.XXXXXX")
cleanup() {
    rm -rf "$RUN"
}
trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

/usr/bin/zipinfo -1 "$ARCHIVE" > "$RUN/entries"
if /usr/bin/grep -E '(^/|(^|/)\.\.(/|$))' "$RUN/entries" >/dev/null; then
    printf 'Input archive has an unsafe path.\n' >&2
    exit 1
fi
/usr/bin/tar -xf "$ARCHIVE" -C "$RUN"
APP="$RUN/podlord-native.app"
[ -d "$APP" ] && [ ! -L "$APP" ] || {
    printf 'Input archive does not contain the expected application bundle.\n' >&2
    exit 1
}
[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$APP/Contents/Info.plist")" = berlin.yuna.podlord ]
[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP/Contents/Info.plist")" = "$VERSION" ] || {
    printf 'Candidate application version differs from the release version.\n' >&2
    exit 1
}
[ "$(/usr/bin/lipo -archs "$APP/Contents/MacOS/podlord-native")" = "$EXPECTED_ARCH" ] || {
    printf 'Candidate application architecture differs from the requested asset.\n' >&2
    exit 1
}

find "$APP" -type f \( -name '*.dylib' -o -path '*/Contents/MacOS/*' \) -print > "$RUN/binaries"
while IFS= read -r binary; do
    /usr/bin/codesign --force --options runtime --timestamp --sign "$IDENTITY" "$binary"
done < "$RUN/binaries"
find "$APP/Contents/Frameworks" -type d -name '*.framework' -prune -print > "$RUN/frameworks"
while IFS= read -r framework; do
    /usr/bin/codesign --force --options runtime --timestamp --sign "$IDENTITY" "$framework"
done < "$RUN/frameworks"
/usr/bin/codesign --force --options runtime --timestamp --sign "$IDENTITY" "$APP"
/usr/bin/codesign --verify --deep --strict --verbose=2 "$APP"

SUBMISSION="$RUN/podlord-notarization.zip"
/usr/bin/ditto -c -k --sequesterRsrc --keepParent "$APP" "$SUBMISSION"
xcrun notarytool submit "$SUBMISSION" --keychain-profile "$PROFILE" --wait
xcrun stapler staple "$APP"
/usr/bin/codesign --verify --deep --strict --verbose=2 "$APP"
spctl --assess --type execute --verbose=4 "$APP"

/usr/bin/ditto -c -k --sequesterRsrc --keepParent "$APP" "$ZIP"
IMAGE_ROOT="$RUN/image"
mkdir "$IMAGE_ROOT"
/usr/bin/ditto "$APP" "$IMAGE_ROOT/Podlord.app"
hdiutil create -volname Podlord -srcfolder "$IMAGE_ROOT" -ov -format UDZO "$DMG"

printf 'Signed and notarized direct macOS %s assets.\n' "$ARCH"
