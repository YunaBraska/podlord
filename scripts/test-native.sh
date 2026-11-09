#!/bin/sh
set -eu
[ "$#" -eq 0 ] || { printf 'No arguments are supported. Set PODLORD_NATIVE_BUILD_DIR to select the build directory.\n' >&2; exit 2; }
ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
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
# Run existing default and UI canaries first, without repeating them in the full cohort.
PREFLIGHT='^native\.(alert_store\.missing|application\.startup_default|settings\.read_denied|read_overlap\.snapshot_fields|forward\.form_refresh_(Basic|Fusion)|authentication\.aws_sso_(success|device_expired)|ui\.(inspector_edit_close_space|ui_density_(filters|single_tab|close|tabs)_Basic|sources_narrow_basic|radar(_map)?_info_panel.*))$'
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
"$COV" report "$@" -instr-profile="$BUILD/coverage/native.profdata" -show-functions -dump \
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
    "$ROOT/native/src/resource_display_model.h" \
    | awk '/ResourceDisplayModelC[12]E/ { print }' > "$BUILD/coverage/display-prefix-functions.txt"
cat "$BUILD/coverage/display-prefix-functions.txt"
if ! awk '{ total+=$(NF-2); missed+=$(NF-1) }
          END {
              if (total<=0) exit 1
              printf "Display-prefix constructor-family branches: %d/%d %.2f%%\n",total-missed,total,100*(total-missed)/total
              if (100*(total-missed)/total<90) exit 1
          }' "$BUILD/coverage/display-prefix-functions.txt"; then
    printf 'Native display-prefix coverage gate failed: require 90%% branches across the constructor and its source-change callbacks.\n' >&2
    exit 1
fi
"$COV" report "$@" -instr-profile="$BUILD/coverage/native.profdata" -show-functions \
    "$ROOT/native/src/alerts.cpp" \
    | awk '/AlertsC[12]E|Alerts8dispatchE/ { print }' > "$BUILD/coverage/alarm-functions.txt"
cat "$BUILD/coverage/alarm-functions.txt"
if ! awk '
/_ZN7podlord6AlertsC[12]E/ { constructor=1 }
/_ZN7podlord6Alerts8dispatchEv/ { dispatch=1 }
{ total+=$(NF-2); missed+=$(NF-1) }
END {
    if (!constructor || !dispatch || total<=0) exit 1
    printf "Alarm constructor/dispatch-family branches: %d/%d %.2f%%\n",total-missed,total,100*(total-missed)/total
    if (100*(total-missed)/total<90) exit 1
}' "$BUILD/coverage/alarm-functions.txt"; then
    printf 'Native alarm coverage gate failed: require 90%% branches across construction, expiry callbacks and dispatch result/reuse projection.\n' >&2
    exit 1
fi
"$COV" report "$@" -instr-profile="$BUILD/coverage/native.profdata" -show-functions \
    "$ROOT/native/src/radar_island.cpp" "$ROOT/native/src/workspace.cpp" "$ROOT/native/src/workspace_map.cpp" "$ROOT/native/src/map_previews.cpp" "$ROOT/native/src/kubeconfig_store.cpp" \
    | awk '/RadarIsland|RadarTiles|oscillation|radar_island.cpp:.*5place|Workspace[0-9]+(contexts|setWorkspacePage|map|showMap|navigateMap|visitMap|rememberMap|reconcileMap|captureMap)|MapPreviews|KubeconfigStore4list/ { print }' \
    > "$BUILD/coverage/radar-map-functions.txt"
cat "$BUILD/coverage/radar-map-functions.txt"
if ! awk '
/_ZN7podlord11RadarIsland11synchronizeEv/ { sync=1 }
/_ZN7podlord11RadarIsland6layout/ { layout=1 }
/_ZN7podlord11RadarIsland5paint/ { paint=1 }
/_ZN7podlord11RadarIsland12setTextColor/ { color=1 }
/_ZN7podlord9Workspace16setWorkspacePage/ { view=1 }
/MapPreviews[0-9]+persist/ { preview=1 }
/Workspace[0-9]+navigateMap/ { history=1 }
/Workspace[0-9]+captureMapPreview/ { capture=1 }
/KubeconfigStore4list/ { catalog=1 }
/RadarTiles[0-9]+advanceFrame/ { clock=1 }
/RadarIsland[0-9]+project/ { projection=1 }
/radar_island.cpp:.*5place/ { placement=1 }
{ total+=$(NF-2); missed+=$(NF-1) }
END {
    if (!sync || !layout || !paint || !color || !view || !preview || !history || !capture || !catalog || !clock || !projection || !placement || total<=0) exit 1
    printf "Radar/Map function-family branches: %d/%d %.2f%%\n",total-missed,total,100*(total-missed)/total
    if (100*(total-missed)/total<90) exit 1
}' "$BUILD/coverage/radar-map-functions.txt"; then
    printf 'Native Radar/Map coverage gate failed: require geometry, batched painting, projection, animation clock, workspace selection, map navigation, preview storage and capture owners with 90%% branches across their complete function families.\n' >&2
    exit 1
fi
"$COV" report "$@" -instr-profile="$BUILD/coverage/native.profdata" -show-functions \
    "$ROOT/native/src/workspace_sources.cpp" "$ROOT/native/src/kubeconfig_store.cpp" "$ROOT/native/src/workspace.h" \
    | awk '/Workspace[0-9]+(clearSourceInspection|inspectSourceContext|canEditSourceYaml|beginSourceEdit|setSourceDraft|applySourceDraft|sourceInspection|sourceYaml|sourceInspectionError|sourceInspectionLoading|sourceEditing|sourceDraft)|sourceInspectionMetadata|readOwnedContext|KubeconfigStore[0-9]+(ownedYaml|editOwnedYaml)/ { print }' \
    > "$BUILD/coverage/source-editor-functions.txt"
cat "$BUILD/coverage/source-editor-functions.txt"
if ! awk '
/Workspace[0-9]+inspectSourceContext/ { inspect=1 }
/Workspace[0-9]+applySourceDraft/ { apply=1 }
/Workspace[0-9]+sourceInspectionEv/ { getters=1 }
/readOwnedContext/ { read=1 }
/KubeconfigStore[0-9]+editOwnedYaml/ { persist=1 }
{ total+=$(NF-2); missed+=$(NF-1) }
END {
    if (!inspect || !apply || !getters || !read || !persist || total<=0) exit 1
    printf "Source editor new-function-family branches: %d/%d %.2f%%\n",total-missed,total,100*(total-missed)/total
    if (100*(total-missed)/total<90) exit 1
}' "$BUILD/coverage/source-editor-functions.txt"; then
    printf 'Native source-editor coverage gate failed: require 90%% branches across the new controller, getters and protected owned-snapshot read/write function families.\n' >&2
    exit 1
fi
"$COV" report "$@" -instr-profile="$BUILD/coverage/native.profdata" -show-functions \
    "$ROOT/native/src/kubeconfig_store.cpp" \
    | awk '/importAwsProfile|importedAwsProfile|awsCredentialPath/ { print }' > "$BUILD/coverage/aws-import-functions.txt"
cat "$BUILD/coverage/aws-import-functions.txt"
if ! awk '/importAwsProfile/ { import=1 }
          /importedAwsProfile/ { read=1 }
          /awsCredentialPath/ { path=1 }
          { total+=$(NF-2); missed+=$(NF-1) }
          END { if (!import || !read || !path || !total || 100*(total-missed)/total<90) exit 1 }' "$BUILD/coverage/aws-import-functions.txt"; then
    printf 'Native AWS import coverage gate failed: require 90%% branches across protected profile import, read and context-owned path functions.\n' >&2
    exit 1
fi
"$COV" report "$@" -instr-profile="$BUILD/coverage/native.profdata" -show-functions \
    "$ROOT/native/src/workspace.cpp" "$ROOT/native/src/credential_process.cpp" \
    | awk '/Workspace[0-9]+(authenticationChallenge|submitAuthenticationCode)|CredentialProcess[0-9]+(challenge|submitMfa)/ { print }' \
    > "$BUILD/coverage/aws-mfa-functions.txt"
cat "$BUILD/coverage/aws-mfa-functions.txt"
if ! awk '/Workspace[0-9]+authenticationChallenge/ { view=1 }
          /Workspace[0-9]+submitAuthenticationCode/ { submit=1 }
          /CredentialProcess[0-9]+challenge/ { owner=1 }
          /CredentialProcess[0-9]+submitMfa/ { route=1 }
          { total+=$(NF-2); missed+=$(NF-1) }
          END { if (!view || !submit || !owner || !route || !total || 100*(total-missed)/total<90) exit 1 }' "$BUILD/coverage/aws-mfa-functions.txt"; then
    printf 'Native AWS MFA coverage gate failed: require 90%% branches across context-bound challenge presentation and submission.\n' >&2
    exit 1
fi
if ! awk -v requireApple="$APPLE_TLS" '$1=="background_http.cpp" { http=1; if ($13+0<90) fail=1 }
          $1=="platform_tls_apple.mm" { apple=1; if ($13+0<90) fail=1 }
          $1=="native_eks.cpp" { eks=1; if ($13+0<90) fail=1 }
          $1=="native_aws_login.cpp" { aws=1; if ($13+0<90) fail=1 }
          $1=="native_gke.cpp" { gke=1; if ($13+0<90) fail=1 }
          $1=="native_oidc.cpp" { oidc=1; if ($13+0<90) fail=1 }
          $1=="native_oci.cpp" { oci=1; if ($13+0<90) fail=1 }
          /^TOTAL/ { seen=1; if ($10+0<95 || $13+0<85) fail=1 }
          END { if (!seen || !http || !eks || !aws || !gke || !oidc || !oci || (requireApple && !apple) || fail) exit 1 }' "$BUILD/coverage/report.txt"; then
    printf 'Native coverage gate failed: require 95%% lines, 85%% branches overall, and 90%% branches in background HTTP, native AWS-SSO/EKS/GKE/OIDC/OCI and macOS-tested Apple trust logic.\n' >&2
    exit 1
fi
"$COV" report "$@" -instr-profile="$BUILD/coverage/native.profdata" -show-functions \
    "$ROOT/native/src/window_host.cpp" \
    | awk '/_ZNK7podlord10WindowHost6layoutEv[[:space:]]|_ZN7podlord10WindowHost7restoreEv[[:space:]]|_ZN7podlord10WindowHost11cancelCloseEv[[:space:]]|_ZN7podlord10WindowHost18confirmLayoutCloseERNS_9WorkspaceEb[[:space:]]/' \
    > "$BUILD/coverage/window-layout-functions.txt"
cat "$BUILD/coverage/window-layout-functions.txt"
if ! awk '
/WindowHost6layoutEv/ { layout=1 }
/WindowHost7restoreEv/ { restore=1 }
/WindowHost11cancelCloseEv/ { cancel=1 }
/WindowHost18confirmLayoutCloseE/ { confirm=1 }
{ total+=$(NF-2); missed+=$(NF-1) }
END {
    if (!layout || !restore || !cancel || !confirm || total<=0) exit 1
    printf "Window restoration new-method branches: %d/%d %.2f%%\n",total-missed,total,100*(total-missed)/total
    if (100*(total-missed)/total<90) exit 1
}' "$BUILD/coverage/window-layout-functions.txt"; then
    printf 'Native window-restoration coverage gate failed: require 90%% branches across layout, restore, cancelClose and confirmLayoutClose.\n' >&2
    exit 1
fi
"$COV" report "$@" -instr-profile="$BUILD/coverage/native.profdata" -show-functions     "$ROOT/native/src/app_main.cpp" "$ROOT/native/src/workspace.cpp"     | awk '/PodlordApplication5event|Workspace11importFiles/' > "$BUILD/coverage/file-input-functions.txt"
cat "$BUILD/coverage/file-input-functions.txt"
if ! awk '
/PodlordApplication5event/ { event=1; events+=$(NF-2); eventMissed+=$(NF-1) }
/Workspace11importFiles/ { import=1; imports+=$(NF-2); importMissed+=$(NF-1) }
END {
    if (!event || !import || events<=0 || imports<=0) exit 1
    printf "File event branches: %d/%d %.2f%%\n",events-eventMissed,events,100*(events-eventMissed)/events
    printf "Batch import family branches: %d/%d %.2f%%\n",imports-importMissed,imports,100*(imports-importMissed)/imports
    if (100*(events-eventMissed)/events<90 || 100*(imports-importMissed)/imports<90) exit 1
}' "$BUILD/coverage/file-input-functions.txt"; then
    printf 'Native file-input coverage failed: require 90%% branches in the application event adapter and complete batch-import family.\n' >&2
    exit 1
fi
if ! awk '$1=="window_layout.cpp" { seen=1; if ($13+0<90) fail=1 }
          END { if (!seen || fail) exit 1 }' "$BUILD/coverage/report.txt"; then
    printf 'Native window-layout storage coverage gate failed: require 90%% branches in window_layout.cpp.\n' >&2
    exit 1
fi
[ "$TEST_STATUS" -eq 0 ] || exit "$TEST_STATUS"
