#!/bin/sh
set -eu
[ "$#" -eq 0 ] || { printf 'No arguments are supported. Set PODLORD_NATIVE_BUILD_DIR to select the build directory.\n' >&2; exit 2; }
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD=${PODLORD_NATIVE_BUILD_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/podlord/native-build}
find_tool() {
    if command -v "$1" >/dev/null 2>&1; then command -v "$1"
    elif [ -x "/opt/homebrew/bin/$1" ]; then printf '%s\n' "/opt/homebrew/bin/$1"
    else printf 'Required tool is missing: %s\n' "$1" >&2; return 1
    fi
}
find_llvm() {
    if command -v "$1" >/dev/null 2>&1; then command -v "$1"
    elif command -v xcrun >/dev/null 2>&1; then xcrun --find "$1"
    else printf 'Required LLVM tool is missing: %s\n' "$1" >&2; return 1
    fi
}
CMAKE=$(find_tool cmake)
CTEST=$(find_tool ctest)
NINJA=$(find_tool ninja)
COV=$(find_llvm llvm-cov)
PROFDATA=$(find_llvm llvm-profdata)
PREFIX=${CMAKE_PREFIX_PATH:-/opt/homebrew}
"$CMAKE" -S "$ROOT/native" -B "$BUILD" -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" \
    -DCMAKE_PREFIX_PATH="$PREFIX" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DPODLORD_COVERAGE=ON
"$CMAKE" --build "$BUILD" --parallel "${PODLORD_BUILD_JOBS:-4}"
mkdir -p "$BUILD/coverage"
RUN=$(mktemp -d "$BUILD/coverage/run.XXXXXX")
APP="$BUILD/podlord-native"
if [ -x "$BUILD/podlord-native.app/Contents/MacOS/podlord-native" ]; then APP="$BUILD/podlord-native.app/Contents/MacOS/podlord-native"; fi
cleanup() {
    find "$RUN" -depth -delete
}
trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir "$RUN/tmp"
TMPDIR="$RUN/tmp"
export TMPDIR
LLVM_PROFILE_FILE="$RUN/%p-%m.profraw"
export LLVM_PROFILE_FILE
TEST_STATUS=0
# Reject broken defaults or application startup before spending a full suite on the same failure.
PREFLIGHT='^native\.(alert_store\.missing|application\.startup_default)$'
"$CTEST" --test-dir "$BUILD" --parallel "${PODLORD_TEST_JOBS:-4}" --output-on-failure --no-tests=error --tests-regex "$PREFLIGHT" || TEST_STATUS=$?
if [ "$TEST_STATUS" -eq 0 ]; then
    "$CTEST" --test-dir "$BUILD" --parallel "${PODLORD_TEST_JOBS:-4}" --output-on-failure --no-tests=error --exclude-regex "$PREFLIGHT" || TEST_STATUS=$?
fi
if [ "$TEST_STATUS" -eq 0 ]; then
BENCHMARK=$("$BUILD/session_cli_test" "$BUILD/podlord-session" benchmark)
printf '%s\n' "$BENCHMARK" > "$BUILD/coverage/benchmark.json"
printf '%s\n' "$BENCHMARK"
SOURCE_BENCHMARK=$("$BUILD/session_cli_test" "$BUILD/podlord-source" source.benchmark)
printf '%s\n' "$SOURCE_BENCHMARK" > "$BUILD/coverage/source-benchmark.json"
printf '%s\n' "$SOURCE_BENCHMARK"
fi
find "$RUN" -maxdepth 1 -type f -name '*.profraw' > "$RUN/profiles.txt"
"$PROFDATA" merge -sparse --input-files="$RUN/profiles.txt" -o "$BUILD/coverage/native.profdata"
set -- "$APP"
while IFS= read -r binary; do
    [ -x "$binary" ] || { printf 'Configured coverage executable is missing: %s\n' "$binary" >&2; exit 1; }
    set -- "$@" -object "$binary"
done < "$BUILD/coverage/objects-Release.txt"
APPLE_TLS=0
if [ "$(uname -s)" = Darwin ]; then
    APPLE_TLS=1
    set -- "$@" "$ROOT/native/src/platform_tls_apple.mm"
fi
"$COV" report "$@" \
    -instr-profile="$BUILD/coverage/native.profdata" "$ROOT"/native/src/*.cpp "$ROOT"/native/src/*.h > "$BUILD/coverage/report.txt"
cat "$BUILD/coverage/report.txt"
"$COV" report "$@" -instr-profile="$BUILD/coverage/native.profdata" -show-functions \
    "$ROOT/native/src/resource_client.cpp" "$ROOT/native/src/workspace.cpp" \
    | awk '/credentialRevision|detailRevision|forgetCredentials|yamlDraftCredentialsCurrent/' \
    > "$BUILD/coverage/authority-functions.txt"
cat "$BUILD/coverage/authority-functions.txt"
if ! awk '/credentialRevision/ { credential=1 }
          /detailRevision/ { detail=1 }
          /forgetCredentials/ { revoke=1 }
          /yamlDraftCredentialsCurrent/ { draft=1 }
          { if ($NF+0<90) fail=1 }
          END { if (!credential || !detail || !revoke || !draft || fail) exit 1 }' "$BUILD/coverage/authority-functions.txt"; then
    printf 'Native authority coverage gate failed: require 90%% branches in credential/detail revision, revocation and draft authority checks.\n' >&2
    exit 1
fi
"$COV" report "$@" -instr-profile="$BUILD/coverage/native.profdata" -show-functions \
    "$ROOT/native/src/radar_island.cpp" "$ROOT/native/src/workspace.cpp" \
    | awk '/RadarIsland(11synchronize|6layout|5paint|12setTextColor)|Workspace14setResourceMap/ { print }' \
    > "$BUILD/coverage/radar-map-functions.txt"
cat "$BUILD/coverage/radar-map-functions.txt"
if ! awk '
/_ZN7podlord11RadarIsland11synchronizeEv/ { sync=1 }
/_ZN7podlord11RadarIsland6layout/ { layout=1 }
/_ZN7podlord11RadarIsland5paint/ { paint=1 }
/_ZN7podlord11RadarIsland12setTextColor/ { color=1 }
/_ZN7podlord9Workspace14setResourceMap/ { view=1 }
{ total+=$(NF-2); missed+=$(NF-1) }
END {
    if (!sync || !layout || !paint || !color || !view || total<=0) exit 1
    printf "Radar/Map function-family branches: %d/%d %.2f%%\n",total-missed,total,100*(total-missed)/total
    if (100*(total-missed)/total<90) exit 1
}' "$BUILD/coverage/radar-map-functions.txt"; then
    printf 'Native Radar/Map coverage gate failed: require all five owning functions and 90%% branches across them and their local functions.\n' >&2
    exit 1
fi
if ! awk -v requireApple="$APPLE_TLS" '$1=="background_http.cpp" { http=1; if ($13+0<90) fail=1 }
          $1=="platform_tls_apple.mm" { apple=1; if ($13+0<90) fail=1 }
          $1=="native_eks.cpp" { eks=1; if ($13+0<90) fail=1 }
          $1=="native_gke.cpp" { gke=1; if ($13+0<90) fail=1 }
          $1=="native_oidc.cpp" { oidc=1; if ($13+0<90) fail=1 }
          $1=="native_oci.cpp" { oci=1; if ($13+0<90) fail=1 }
          /^TOTAL/ { seen=1; if ($10+0<95 || $13+0<85) fail=1 }
          END { if (!seen || !http || !eks || !gke || !oidc || !oci || (requireApple && !apple) || fail) exit 1 }' "$BUILD/coverage/report.txt"; then
    printf 'Native coverage gate failed: require 95%% lines, 85%% branches overall, and 90%% branches in background HTTP, native EKS/GKE/OIDC/OCI and macOS-tested Apple trust logic.\n' >&2
    exit 1
fi
[ "$TEST_STATUS" -eq 0 ] || exit "$TEST_STATUS"
