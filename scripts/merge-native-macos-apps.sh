#!/bin/sh
set -eu
umask 077
[ "$#" -eq 3 ] || { printf 'Usage: %s ARM64_ZIP X64_ZIP NEW_APP_PATH\n' "$0" >&2; exit 2; }
ARM=$1
INTEL=$2
OUTPUT=$3
[ "$(uname -s)" = Darwin ] || { printf 'Universal2 merging requires macOS.\n' >&2; exit 2; }
case "$OUTPUT" in /*.app) ;; *) printf 'Output must be an absolute .app path.\n' >&2; exit 2 ;; esac
[ ! -e "$OUTPUT" ] && [ ! -L "$OUTPUT" ] || { printf 'Output already exists.\n' >&2; exit 2; }
RUN=$(mktemp -d "${TMPDIR:-/tmp}/podlord-universal.XXXXXX")
trap 'rm -rf "$RUN"' 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
for ARCH in arm64 x86_64; do
    if [ "$ARCH" = arm64 ]; then ARCHIVE=$ARM; else ARCHIVE=$INTEL; fi
    [ -f "$ARCHIVE" ] && [ ! -L "$ARCHIVE" ] || { printf 'Missing regular input archive.\n' >&2; exit 1; }
    summary=$(/usr/bin/zipinfo -t "$ARCHIVE")
    bytes=$(printf '%s\n' "$summary" | awk '{print $3}')
    case "$bytes" in ''|*[!0-9]*) printf 'Invalid archive size metadata.\n' >&2; exit 1 ;; esac
    [ "$bytes" -le 100000000 ] || { printf 'Input exceeds the existing thin-app installed-size limit.\n' >&2; exit 1; }
    /usr/bin/zipinfo -1 "$ARCHIVE" > "$RUN/entries"
    if /usr/bin/grep -E '(^/|(^|/)\.\.(/|$))' "$RUN/entries" >/dev/null; then
        printf 'Unsafe archive entry.\n' >&2; exit 1
    fi
    mkdir "$RUN/$ARCH"
    /usr/bin/tar -xf "$ARCHIVE" -C "$RUN/$ARCH"
    APP="$RUN/$ARCH/podlord-native.app"
    /usr/bin/codesign --verify --deep --strict "$APP"
    [ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$APP/Contents/Info.plist")" = berlin.yuna.podlord ]
    [ "$(/usr/bin/lipo -archs "$APP/Contents/MacOS/podlord-native")" = "$ARCH" ] || { printf 'Expected one native %s slice.\n' "$ARCH" >&2; exit 1; }
    find "$APP" -type l -print > "$RUN/links"
    node - "$APP" "$RUN/links" <<'JS'
const fs = require('node:fs');
const path = require('node:path');
const root = fs.realpathSync(process.argv[2]) + path.sep;
for (const link of fs.readFileSync(process.argv[3], 'utf8').split('\n').filter(Boolean)) {
    if (!fs.realpathSync(link).startsWith(root)) throw new Error('Bundle symlink escapes the application.');
}
JS
    find "$APP" -type d -name _CodeSignature -prune -exec rm -rf '{}' +
    (cd "$APP" && find . \( -type f -o -type l \) -print | LC_ALL=C sort) > "$RUN/$ARCH.files"
done
cmp "$RUN/arm64.files" "$RUN/x86_64.files"
A="$RUN/arm64/podlord-native.app"
B="$RUN/x86_64/podlord-native.app"
while IFS= read -r relative; do
    if [ -L "$A/$relative" ]; then
        [ "$(readlink "$A/$relative")" = "$(readlink "$B/$relative")" ]
    elif /usr/bin/lipo -archs "$A/$relative" >/dev/null 2>&1; then
        /usr/bin/lipo "$A/$relative" -verify_arch arm64
        /usr/bin/lipo "$B/$relative" -verify_arch x86_64
        /usr/bin/codesign --remove-signature "$A/$relative"
        /usr/bin/codesign --remove-signature "$B/$relative"
        /usr/bin/lipo -create "$A/$relative" "$B/$relative" -output "$RUN/universal-binary"
        /usr/bin/lipo "$RUN/universal-binary" -verify_arch arm64
        /usr/bin/lipo "$RUN/universal-binary" -verify_arch x86_64
        cat "$RUN/universal-binary" > "$A/$relative"
    else
        cmp "$A/$relative" "$B/$relative"
    fi
done < "$RUN/arm64.files"
mkdir -p "$(dirname -- "$OUTPUT")"
/usr/bin/ditto "$A" "$OUTPUT"
printf 'Merged native Universal2 app. It must be signed before distribution: %s\n' "$OUTPUT"
