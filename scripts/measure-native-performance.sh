#!/bin/sh
set -eu
umask 077
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD=${1:?Usage: measure-native-performance.sh BUILD_DIRECTORY FRESH_EVIDENCE_DIRECTORY}
OUT=${2:?A fresh evidence directory is required}
NODE=${PODLORD_NODE:-node}
BIN="$BUILD/performance-ui-test"
[ -x "$BIN" ] || { printf '%s\n' 'Build the performance-ui-test target first.' >&2; exit 2; }
[ ! -e "$OUT" ] || { printf '%s\n' 'Evidence output already exists; refusing to replace it.' >&2; exit 2; }
mkdir -p "$OUT"
RUN=$(mktemp -d "${TMPDIR:-/tmp}/podlord-performance.XXXXXX")
mkdir "$RUN/tmp"
TMPDIR="$RUN/tmp"
export TMPDIR
SERVER=
APP=
cleanup() {
    if [ -n "$APP" ]; then kill "$APP" 2>/dev/null || :; wait "$APP" 2>/dev/null || :; fi
    if [ -n "$SERVER" ]; then kill "$SERVER" 2>/dev/null || :; wait "$SERVER" 2>/dev/null || :; fi
    if [ -f "$RUN/api-summary.json" ]; then cp "$RUN/api-summary.json" "$OUT/api-summary.json"; fi
    find "$RUN" -depth -delete
}
trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
{ uname -a; /usr/bin/sw_vers; /usr/sbin/sysctl -n hw.model hw.memsize hw.logicalcpu; /usr/bin/shasum -a 256 "$BIN"; } > "$OUT/environment.txt"
"$NODE" "$ROOT/native/tests/performance_api.mjs" "$RUN" > "$OUT/api.log" 2>&1 &
SERVER=$!
attempt=0
while [ ! -f "$RUN/ready" ]; do
    kill -0 "$SERVER" 2>/dev/null || { cat "$OUT/api.log" >&2; exit 1; }
    attempt=$((attempt + 1))
    [ "$attempt" -lt 100 ] || { printf '%s\n' 'Local API did not become ready.' >&2; exit 1; }
    sleep 0.1
done
export QT_QPA_PLATFORM=cocoa QT_QUICK_CONTROLS_STYLE=Fusion
unset QT_QUICK_BACKEND
"$BIN" "$RUN/kubeconfig" > "$OUT/measurements.jsonl" 2> "$OUT/ui.log" &
APP=$!
while kill -0 "$APP" 2>/dev/null; do
    /bin/ps -p "$APP" -o pid=,rss=,%cpu= >> "$OUT/process-samples.txt" || :
    sleep 0.1
done
STATUS=0
wait "$APP" || STATUS=$?
APP=
printf '%s\n' "$STATUS" > "$OUT/exit-status.txt"
cat "$OUT/measurements.jsonl"
[ "$STATUS" -eq 0 ] || cat "$OUT/ui.log" >&2
exit "$STATUS"
