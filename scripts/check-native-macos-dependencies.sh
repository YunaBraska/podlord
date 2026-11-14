#!/bin/sh
set -eu
umask 077
if [ "$#" -ne 1 ]; then
    printf 'Usage: %s /absolute/application.app\n' "$0" >&2
    exit 2
fi
case "$1" in /*) APP=$1 ;; *) printf 'Application path must be absolute.\n' >&2; exit 2 ;; esac
APP=$(CDPATH= cd -P "$APP" 2>/dev/null && pwd -P) || {
    printf 'Application bundle is unavailable: %s\n' "$1" >&2; exit 2;
}
[ -x "$APP/Contents/MacOS/podlord-native" ] && [ -d "$APP/Contents/Frameworks" ] || {
    printf 'Application bundle is incomplete: %s\n' "$APP" >&2; exit 2;
}
physical_file() (
    path=$1
    remaining=40
    while :; do
        directory=$(CDPATH= cd -P "${path%/*}" 2>/dev/null && pwd -P) || exit 1
        path="$directory/${path##*/}"
        if [ ! -L "$path" ]; then printf '%s\n' "$path"; exit 0; fi
        [ "$remaining" -gt 0 ] || exit 1
        target=$(readlink "$path") || exit 1
        remaining=$((remaining - 1))
        case "$target" in /*) path=$target ;; *) path="$directory/$target" ;; esac
    done
)
RUN=$(mktemp -d "${TMPDIR:-/tmp}/podlord-dependencies.XXXXXX")
trap 'rm -rf "$RUN"' 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
find "$APP" -type l > "$RUN/links"
while IFS= read -r link; do
    target=$(physical_file "$link") || {
        printf 'Cannot resolve application bundle symlink: %s\n' "$link" >&2; exit 1;
    }
    case "$target" in
        "$APP"/*) ;;
        *) printf 'Dependency resolves outside the application bundle: %s\n' "$link" >&2; exit 1 ;;
    esac
done < "$RUN/links"
find "$APP" -type f \( -name '*.dylib' -o -path '*.framework/Versions/A/Qt*' -o -path '*/Contents/MacOS/*' \) \
    > "$RUN/files"
ARCHITECTURES=$(/usr/bin/lipo -archs "$APP/Contents/MacOS/podlord-native")
while IFS= read -r file; do
    case "$(/usr/bin/file -b "$file")" in
        Mach-O*) ;;
        *) printf 'Invalid Mach-O runtime file: %s\n' "$file" >&2; exit 1 ;;
    esac
    actual=$(/usr/bin/lipo -archs "$file")
    for required in $ARCHITECTURES; do
        case " $actual " in
            *" $required "*) ;;
            *) printf 'Missing runtime architecture: %s (%s)\n' "$required" "$file" >&2; exit 1 ;;
        esac
    done
    /usr/bin/otool -l "$file"
done < "$RUN/files" > "$RUN/load-commands"
awk '
        /:$/ {file=$0}
        $1 == "cmd" {load=($2 ~ /^LC_(LOAD_DYLIB|LOAD_WEAK_DYLIB|REEXPORT_DYLIB|LOAD_UPWARD_DYLIB|LAZY_LOAD_DYLIB)$/)}
        load && $1 == "name" {print file; print "\t" $2}
    ' "$RUN/load-commands" > "$RUN/dependencies"
(
    result=0
    file=
    tab=$(printf '\t')
    while IFS= read -r line; do
        printf '%s\n' "$line"
        case "$line" in
            *:) file=${line%:}; continue ;;
        esac
        dependency=${line#"$tab"}
        case "$dependency" in
            /System/Library/*|/usr/lib/*) continue ;;
            @executable_path/*) resolved="$APP/Contents/MacOS/${dependency#@executable_path/}" ;;
            @loader_path/*) resolved="$(dirname -- "$file")/${dependency#@loader_path/}" ;;
            @rpath/*) resolved="$APP/Contents/Frameworks/${dependency#@rpath/}" ;;
            *) printf 'External runtime dependency: %s (%s)\n' "$dependency" "$file" >&2; result=1; continue ;;
        esac
        if [ ! -f "$resolved" ]; then
            printf 'Missing bundled runtime dependency: %s (%s)\n' "$dependency" "$file" >&2
            result=1
            continue
        fi
        resolved=$(physical_file "$resolved") || {
            printf 'Cannot resolve bundled runtime dependency: %s (%s)\n' "$dependency" "$file" >&2
            result=1; continue;
        }
        case "$resolved" in
            "$APP"/*) ;;
            *) printf 'Dependency resolves outside the application bundle: %s (%s)\n' "$dependency" "$file" >&2; result=1 ;;
        esac
    done
    exit "$result"
) < "$RUN/dependencies"
for required in Contents/Frameworks/libssl.3.dylib Contents/Frameworks/libcrypto.3.dylib Contents/PlugIns/tls/libqopensslbackend.dylib; do
    [ -f "$APP/$required" ] || { printf 'Missing bundled TLS runtime: %s\n' "$required" >&2; exit 1; }
done
MINIMUM=$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' "$APP/Contents/Info.plist" 2>/dev/null) || MINIMUM=
if ! printf '%s\n' "$MINIMUM" | awk 'NF == 1 && /^[0-9]+([.][0-9]+){0,2}$/ {valid=1} END {exit !valid}'; then
    printf 'Invalid or missing LSMinimumSystemVersion: %s\n' "$APP" >&2
    exit 1
fi
awk -v minimum="$MINIMUM" '
    function exceeds(value, baseline, a, b, i) {
        split(value, a, "."); split(baseline, b, ".")
        for (i=1; i<=3; i++) if (a[i]+0 != b[i]+0) return a[i]+0 > b[i]+0
        return 0
    }
    function finish() {
        if (file != "" && !seen) {print "Missing macOS runtime version: " file > "/dev/stderr"; failed=1}
    }
    /:$/ {finish(); file=$0; seen=0}
    $1 == "cmd" {command=$2}
    command == "LC_BUILD_VERSION" && $1 == "platform" && $2 != "1" && $2 != "MACOS" {
        print "Non-macOS runtime platform: " $2 " (" file ")" > "/dev/stderr"; failed=1
    }
    (command == "LC_BUILD_VERSION" && $1 == "minos") || (command == "LC_VERSION_MIN_MACOS" && $1 == "version") {
        seen=1
        if (exceeds($2, minimum)) {
            print "Runtime minimum macOS version exceeds declared floor: " $2 " > " minimum " (" file ")" > "/dev/stderr"; failed=1
        }
    }
    END {finish(); exit failed}
' "$RUN/load-commands"
printf 'Runtime platform: macOS >= %s; architectures: %s.\n' "$MINIMUM" "$ARCHITECTURES"
