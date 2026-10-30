#!/bin/sh
set -eu
umask 077
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD=${1:?Usage: measure-native-performance.sh BUILD_DIRECTORY FRESH_EVIDENCE_DIRECTORY [--background-metal-diagnostic]}
OUT=${2:?A fresh evidence directory is required}
MODE=${3:-}
[ "$#" -le 3 ] && { [ -z "$MODE" ] || [ "$MODE" = --background-metal-diagnostic ]; } || {
    printf '%s\n' 'Unsupported performance mode. Use --background-metal-diagnostic or omit the mode.' >&2
    exit 2
}
NODE=${PODLORD_NODE:-node}
BUILD=$(CDPATH= cd -- "$BUILD" && pwd)
BIN="$BUILD/performance-ui-test"
BUNDLE=
if [ -x "$BUILD/performance-ui-test.app/Contents/MacOS/performance-ui-test" ]; then
    BUNDLE="$BUILD/performance-ui-test.app"
    BIN="$BUILD/performance-ui-test.app/Contents/MacOS/performance-ui-test"
fi
[ -x "$BIN" ] || { printf '%s\n' 'Build the performance-ui-test target first.' >&2; exit 2; }
if [ "$MODE" = --background-metal-diagnostic ]; then
    [ "${QT_QPA_PLATFORM:-cocoa}" = cocoa ] && [ -n "$BUNDLE" ] || {
        printf '%s\n' 'Background Metal diagnostics require a macOS Cocoa application bundle.' >&2
        exit 2
    }
fi
[ ! -e "$OUT" ] || { printf '%s\n' 'Evidence output already exists; refusing to replace it.' >&2; exit 2; }
mkdir -p "$OUT"
OUT=$(CDPATH= cd -- "$OUT" && pwd)
RUN=$(mktemp -d "${TMPDIR:-/tmp}/podlord-performance.XXXXXX")
mkdir "$RUN/tmp"
TMPDIR="$RUN/tmp"
export TMPDIR
SERVER=
APP=
LAUNCHER=
owned_app_pid() {
    /bin/ps -axo pid=,command= | awk -v binary="$BIN" -v config="$RUN/kubeconfig" -v mode="$MODE" '
        { pid=$1; sub(/^[[:space:]]*[0-9]+[[:space:]]+/, "") }
        $0 == binary " " config (mode == "" ? "" : " " mode) { print pid }'
}
cleanup() {
    if [ -z "$APP" ] && [ -n "$LAUNCHER" ]; then APP=$(owned_app_pid); fi
    if [ -n "$APP" ]; then kill "$APP" 2>/dev/null || :; fi
    if [ -n "$LAUNCHER" ]; then kill "$LAUNCHER" 2>/dev/null || :; wait "$LAUNCHER" 2>/dev/null || :; fi
    if [ -n "$SERVER" ]; then kill "$SERVER" 2>/dev/null || :; wait "$SERVER" 2>/dev/null || :; fi
    if [ -f "$RUN/api-summary.json" ]; then cp "$RUN/api-summary.json" "$OUT/api-summary.json"; fi
    find "$RUN" -depth -delete
}
trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
{ uname -a; /usr/bin/sw_vers; /usr/sbin/sysctl -n hw.model hw.memsize hw.logicalcpu; /usr/bin/shasum -a 256 "$BIN"; } > "$OUT/environment.txt"
if [ "$MODE" = --background-metal-diagnostic ]; then
    printf '%s\n' 'Background Metal diagnostic only. Not release evidence. Audio muted in a disposable profile.' > "$OUT/diagnostic-only.txt"
fi
"$NODE" "$ROOT/native/tests/performance_api.mjs" "$RUN" > "$OUT/api.log" 2>&1 &
SERVER=$!
attempt=0
while [ ! -f "$RUN/ready" ]; do
    kill -0 "$SERVER" 2>/dev/null || { cat "$OUT/api.log" >&2; exit 1; }
    attempt=$((attempt + 1))
    [ "$attempt" -lt 100 ] || { printf '%s\n' 'Local API did not become ready.' >&2; exit 1; }
    sleep 0.1
done
export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-cocoa} QT_QUICK_CONTROLS_STYLE=Fusion
if [ "$QT_QPA_PLATFORM" = offscreen ]; then
    QT_QUICK_BACKEND=software; export QT_QUICK_BACKEND
else
    unset QT_QUICK_BACKEND
fi
if [ "$QT_QPA_PLATFORM" = cocoa ] && [ -n "$BUNDLE" ]; then
    set --
    if [ "$MODE" = --background-metal-diagnostic ]; then set -- -g --env QSG_RHI_BACKEND=metal; fi
    /usr/bin/open -n -W "$@" -a "$BUNDLE" --stdout "$OUT/measurements.jsonl" --stderr "$OUT/ui.log" \
        --env QT_QPA_PLATFORM=cocoa --env "QT_QUICK_CONTROLS_STYLE=$QT_QUICK_CONTROLS_STYLE" \
        --env "DYLD_LIBRARY_PATH=${DYLD_LIBRARY_PATH:-}" --env "TMPDIR=$TMPDIR" \
        --env QSG_INFO=1 --args "$RUN/kubeconfig" ${MODE:+"$MODE"} > "$OUT/launch.log" 2>&1 &
    LAUNCHER=$!
    attempt=0
    while [ -z "$APP" ] && kill -0 "$LAUNCHER" 2>/dev/null; do
        APP=$(owned_app_pid)
        attempt=$((attempt + 1))
        [ "$attempt" -lt 100 ] || { printf '%s\n' 'The launched performance process was not identifiable.' >&2; exit 1; }
        [ -n "$APP" ] || sleep 0.1
    done
else
    "$BIN" "$RUN/kubeconfig" ${MODE:+"$MODE"} > "$OUT/measurements.jsonl" 2> "$OUT/ui.log" &
    APP=$!
    LAUNCHER=$APP
fi
while [ -n "$APP" ] && kill -0 "$APP" 2>/dev/null; do
    /bin/ps -p "$APP" -o pid=,rss=,%cpu= >> "$OUT/process-samples.txt" || :
    sleep 0.1
done
STATUS=0
wait "$LAUNCHER" || STATUS=$?
APP=
LAUNCHER=
# LaunchServices can return success even when the application reports failure.
if [ "$STATUS" -eq 0 ]; then
    "$NODE" -e '
        const fs = require("node:fs");
        const records = fs.readFileSync(process.argv[1], "utf8").trim().split("\n").map(JSON.parse);
        const diagnostic = process.argv[2] === "--background-metal-diagnostic";
        if (records.some(r => r.diagnosticOnly !== diagnostic || (diagnostic && r.renderingBoundary !== "background-cocoa-metal-diagnostic"))) process.exit(1);
        if (!records.some(r => r.type === "result" && r.passed === true) || records.some(r => r.type === "failure")) process.exit(1);
    ' "$OUT/measurements.jsonl" "$MODE" || STATUS=$?
fi
printf '%s\n' "$STATUS" > "$OUT/exit-status.txt"
cat "$OUT/measurements.jsonl"
[ "$STATUS" -eq 0 ] || cat "$OUT/ui.log" >&2
exit "$STATUS"
