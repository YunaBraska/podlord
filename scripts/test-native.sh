#!/bin/sh
set -eu
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
"$COV" report "$@" \
    -instr-profile="$BUILD/coverage/native.profdata" "$ROOT"/native/src/*.cpp "$ROOT"/native/src/*.h > "$BUILD/coverage/report.txt"
cat "$BUILD/coverage/report.txt"
COVERAGE_STATUS=0
awk '/^TOTAL/ { seen=1; line=$10+0; branch=$13+0; if (line<95 || branch<90) exit 1 }
     END { if (!seen) exit 1 }' "$BUILD/coverage/report.txt" || COVERAGE_STATUS=$?
[ "$TEST_STATUS" -eq 0 ] || exit "$TEST_STATUS"
exit "$COVERAGE_STATUS"
