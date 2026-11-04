#!/bin/sh
set -eu
umask 077
if [ "$#" -ne 2 ]; then
    printf 'Usage: %s /absolute/package.tar.gz /absolute/new/evidence-directory\n' "$0" >&2
    exit 2
fi
ARCHIVE=$1
OUTPUT=$2
case "$ARCHIVE:$OUTPUT" in /*:/*) ;; *) printf 'Absolute paths are required.\n' >&2; exit 2 ;; esac
if [ ! -f "$ARCHIVE" ] || [ -e "$OUTPUT" ] || [ -L "$OUTPUT" ]; then
    printf 'Input archive must exist and evidence directory must be new.\n' >&2
    exit 2
fi
if dpkg-query -W -f='${binary:Package}\n' 2>/dev/null | grep -E '^(libqt[56]|qt[56]-|qml[56]-)' >/dev/null; then
    printf 'Clean-runtime check refuses a system with Qt packages installed.\n' >&2
    exit 1
fi
tar -tzf "$ARCHIVE" | awk '
    $0 !~ /^podlord(\/|$)/ || $0 ~ /(^|\/)\.\.(\/|$)/ { bad=1 }
    END { exit bad }
'
mkdir -p "$(dirname -- "$OUTPUT")"
mkdir "$OUTPUT"
WORK=$(mktemp -d "$(dirname -- "$OUTPUT")/.podlord-linux-runtime.XXXXXX")
cleanup() { if [ -d "$WORK" ]; then rm -r "$WORK"; fi; }
trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
tar -xzf "$ARCHIVE" --no-same-owner -C "$WORK"
APP="$WORK/podlord"
test -x "$APP/podlord"
test -f "$APP/lib/libssl.so.3"
test -f "$APP/lib/libcrypto.so.3"
find "$APP" -type l -exec sh -c '
    root=$1; shift
    for link do
        target=$(readlink -f "$link")
        case "$target" in "$root"/*) ;; *) printf "External package symlink: %s\n" "$link" >&2; exit 1 ;; esac
    done
' sh "$APP" {} +
find "$APP" -type f -exec sh -c '
    root=$1; shift
    for binary do
        case "$binary" in */podlord-native|*.so|*.so.*)
            printf "\n%s\n" "$binary"
            LD_LIBRARY_PATH="$root/lib" ldd "$binary"
            ;;
        esac
    done
' sh "$APP" {} + > "$OUTPUT/runtime-dependencies.txt" 2>&1
if grep 'not found' "$OUTPUT/runtime-dependencies.txt"; then exit 1; fi
export APP OUTPUT
for style in Basic Fusion; do
    export STYLE=$style
    # This child shell deliberately expands its own profile and process variables.
    # shellcheck disable=SC2016
    xvfb-run --auto-servernum --server-args='-screen 0 1024x768x24' sh -eu -c '
        PROFILE=$(mktemp -d)
        pid=
        compositor=
        name=setup
        finish() {
            status=$?
            if [ "$status" -ne 0 ]; then
                for log in "$OUTPUT/startup-$name.log" "$OUTPUT/weston-$STYLE.log"; do
                    if [ -f "$log" ]; then printf "\n%s\n" "$log" >&2; tail -n 20 "$log" >&2; fi
                done
            fi
            if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || :; wait "$pid" 2>/dev/null || :; fi
            if [ -n "$compositor" ]; then kill "$compositor" 2>/dev/null || :; wait "$compositor" 2>/dev/null || :; fi
            rm -r "$PROFILE"
            return "$status"
        }
        trap finish 0
        trap "exit 143" TERM
        export HOME="$PROFILE" XDG_CONFIG_HOME="$PROFILE/config" XDG_DATA_HOME="$PROFILE/data" XDG_CACHE_HOME="$PROFILE/cache"
        export KUBECONFIG="$PROFILE/missing-config" PODLORD_DISABLE_UPDATE_CHECK=1 PODLORD_DISABLE_AUDIO=1
        export QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE="$STYLE" QSG_INFO=1 QSG_RENDER_TIMING=1
        export QT_LOGGING_RULES="qt.scenegraph.time.*=true"
        export XDG_RUNTIME_DIR="$PROFILE/run" WAYLAND_DISPLAY=podlord-test
        mkdir -m 700 "$XDG_RUNTIME_DIR"
        weston --backend=headless-backend.so --renderer=pixman --socket="$WAYLAND_DISPLAY" --idle-time=0 > "$OUTPUT/weston-$STYLE.log" 2>&1 &
        compositor=$!
        n=0
        while [ ! -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]; do
            kill -0 "$compositor"
            n=$((n + 1)); test "$n" -lt 100
            sleep 0.1
        done
        for platform in xcb wayland; do
        export QT_QPA_PLATFORM=$platform
        name="$platform-$STYLE"
        "$APP/podlord" --help > "$OUTPUT/help-$name.txt" 2>&1
        "$APP/podlord" > "$OUTPUT/startup-$name.log" 2>&1 &
        pid=$!
        ready=false
        n=0
        while [ "$n" -lt 100 ]; do
            kill -0 "$pid"
            if [ "$platform" = xcb ]; then xwininfo -root -tree > "$OUTPUT/windows-$name.txt"; fi
            if grep -F "Frame rendered with '\''software'\'' renderloop" "$OUTPUT/startup-$name.log" >/dev/null; then ready=true; break; fi
            n=$((n + 1))
            sleep 0.1
        done
        test "$ready" = true
        sleep 1
        kill -0 "$pid"
        if [ "$platform" = xcb ]; then
            xwininfo -root -tree > "$OUTPUT/windows-$name.txt"
            grep -i "\"Podlord" "$OUTPUT/windows-$name.txt" >/dev/null
        fi
        cat "/proc/$pid/maps" > "$OUTPUT/maps-$name.txt"
        awk -v root="$APP/" '\''
            $NF ~ /lib(Qt6|icu|avcodec|avformat|avutil|swresample|swscale|ssl\.so\.3|crypto\.so\.3)/ && index($NF,root) != 1 { print; bad=1 }
            END { exit bad }
        '\'' "$OUTPUT/maps-$name.txt"
        if grep -E "failed to load|QQmlApplicationEngine failed|module .* is not installed|Cannot load library" "$OUTPUT/startup-$name.log"; then exit 1; fi
        kill "$pid"; wait "$pid" 2>/dev/null || :; pid=
        done
    '
done
{
    printf 'Clean runtime: '; cat /etc/os-release
    printf '\nArchitecture: '; uname -m
    printf 'Actual X11 and Wayland frames, Basic and Fusion, Qt Quick software rendering: PASS\n'
    printf 'Resolved runtime dependencies and app-local Qt/ICU/FFmpeg origins: PASS\n'
    printf 'No installed Qt, SDK, kubectl or .NET used. No audio output.\n'
    sha256sum "$ARCHIVE"
} > "$OUTPUT/runtime-evidence.txt"
cat "$OUTPUT/runtime-evidence.txt"
