#!/bin/sh
set -eu
umask 077
[ "$#" -eq 2 ] || { printf 'Usage: %s MARKETING_VERSION BUILD_NUMBER\n' "$0" >&2; exit 2; }
version=$1
build_number=$2
printf '%s\n' "$version" | /usr/bin/grep -Eq '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$' || { printf 'Invalid marketing version.\n' >&2; exit 2; }
printf '%s\n' "$build_number" | /usr/bin/grep -Eq '^[1-9][0-9]{0,3}(\.[0-9]{1,2}){0,2}$' || { printf 'Build number must contain up to four, two and two digits, for example 12.1.0.\n' >&2; exit 2; }
: "${APP_STORE_BUNDLE_ID:?}"
: "${APP_STORE_TEAM_ID:?}"
: "${MACOS_APP_STORE_APPLICATION_IDENTITY:?}"
: "${MACOS_APP_STORE_PROFILE_PATH:?}"
: "${PODLORD_STORE_ARM64_ARCHIVE:?}"
: "${PODLORD_STORE_X64_ARCHIVE:?}"
[ "$APP_STORE_BUNDLE_ID" = berlin.yuna.podlord ] || { printf 'Unexpected Store bundle identity.\n' >&2; exit 2; }
printf '%s\n' "$APP_STORE_TEAM_ID" | /usr/bin/grep -Eq '^[A-Z0-9]{10}$' || { printf 'Invalid Apple team identifier.\n' >&2; exit 2; }
[ -f "$MACOS_APP_STORE_PROFILE_PATH" ] && [ -r "$MACOS_APP_STORE_PROFILE_PATH" ] && [ ! -L "$MACOS_APP_STORE_PROFILE_PATH" ] || { printf 'A readable regular provisioning profile is required.\n' >&2; exit 2; }
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIST="$ROOT/dist/app-store"
[ ! -e "$DIST" ] && [ ! -L "$DIST" ] || { printf 'Store output already exists; refusing to replace it.\n' >&2; exit 2; }
mkdir -p "$DIST"
APP="$DIST/Podlord.app"
/bin/sh "$ROOT/scripts/merge-native-macos-apps.sh" "$PODLORD_STORE_ARM64_ARCHIVE" "$PODLORD_STORE_X64_ARCHIVE" "$APP"
[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP/Contents/Info.plist")" = "$version" ] || { printf 'Candidate version differs from the requested source version.\n' >&2; exit 1; }
INSTALLED=$(find "$APP" -type f -exec /usr/bin/stat -f '%z' '{}' + | awk '{n+=$1} END {printf "%.0f\n",n}')
printf 'Universal2 installed bytes: %s\nLimit: 100000000\n' "$INSTALLED" > "$DIST/size-evidence.txt"
[ "$INSTALLED" -le 100000000 ] || { cat "$DIST/size-evidence.txt" >&2; printf 'Universal2 exceeds the unchanged installed-size gate.\n' >&2; exit 1; }
cp "$MACOS_APP_STORE_PROFILE_PATH" "$APP/Contents/embedded.provisionprofile"
/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $build_number" "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Add :LSApplicationCategoryType string public.app-category.developer-tools' "$APP/Contents/Info.plist"
ENTITLEMENTS="$DIST/Podlord.entitlements"
cp "$ROOT/native/macos/AppStore.entitlements" "$ENTITLEMENTS"
/usr/libexec/PlistBuddy -c "Add :com.apple.application-identifier string $APP_STORE_TEAM_ID.$APP_STORE_BUNDLE_ID" "$ENTITLEMENTS"
/usr/libexec/PlistBuddy -c "Add :com.apple.developer.team-identifier string $APP_STORE_TEAM_ID" "$ENTITLEMENTS"
find "$APP" -type f > "$DIST/runtime-files.txt"
while IFS= read -r binary; do
    if /usr/bin/lipo -archs "$binary" >/dev/null 2>&1; then
        /usr/bin/codesign --force --options runtime --timestamp --sign "$MACOS_APP_STORE_APPLICATION_IDENTITY" "$binary"
    fi
done < "$DIST/runtime-files.txt"
rm "$DIST/runtime-files.txt"
find "$APP/Contents/Frameworks" -type d -name '*.framework' -prune -print > "$DIST/frameworks.txt"
while IFS= read -r framework; do
    /usr/bin/codesign --force --options runtime --timestamp --sign "$MACOS_APP_STORE_APPLICATION_IDENTITY" "$framework"
done < "$DIST/frameworks.txt"
rm "$DIST/frameworks.txt"
/usr/bin/codesign --force --entitlements "$ENTITLEMENTS" --options runtime --timestamp --sign "$MACOS_APP_STORE_APPLICATION_IDENTITY" "$APP"
/usr/bin/codesign --verify --deep --strict --verbose=2 "$APP"
printf 'implementation=cpp\nversion=%s\nbuild=%s\narchitectures=arm64 x86_64\n' "$version" "$build_number" > "$DIST/native-build.txt"
