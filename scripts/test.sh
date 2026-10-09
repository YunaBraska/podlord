#!/bin/sh
set -eu
umask 077
ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DOTNET="$ROOT_DIR/.tools/dotnet/dotnet"
PATH="$ROOT_DIR/.tools/bin:$PATH"
export PATH
[ -x "$DOTNET" ] || DOTNET=dotnet
RUN=$(mktemp -d "${TMPDIR:-/tmp}/podlord-test.XXXXXX")
OWNER=$(basename "$RUN" | tr '.' '-')
export PODLORD_TEST_RUN_OWNER="$OWNER"
export PODLORD_CONFIG_HOME="$RUN/config/podlord"
export PODLORD_HOME="$RUN/home"
export KUBECONFIG="$RUN/kubeconfig"
export PODLORD_DISABLE_AUDIO=1
export PODLORD_DISABLE_UPDATE_CHECK=1
PID=
cleanup() {
    status=$?
    trap - 0
    if [ -n "$PID" ]; then kill "$PID" 2>/dev/null || :; wait "$PID" 2>/dev/null || :; fi
    if ! /bin/sh "$ROOT_DIR/scripts/cleanup-k3d-test-run.sh" "$OWNER"; then status=1; fi
    find "$RUN/home" "$RUN/config" -depth -delete 2>/dev/null || :
    find "$RUN" -maxdepth 1 -type f -delete
    printf 'Test results retained in %s/results\n' "$RUN"
    exit "$status"
}
trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir -p "$PODLORD_HOME" "$PODLORD_CONFIG_HOME"
cd "$ROOT_DIR"
# Reject remote Docker contexts before bootstrapping or starting a cluster.
/bin/sh "$ROOT_DIR/scripts/cleanup-k3d-test-run.sh" "$OWNER"
"$ROOT_DIR/scripts/bootstrap-k3d.sh"
"$DOTNET" test Podlord.slnx --settings "$ROOT_DIR/coverage.runsettings" --collect:"XPlat Code Coverage" --results-directory "$RUN/results" "$@" &
PID=$!
wait "$PID"
PID=
if ! python3 "$ROOT_DIR/scripts/check-coverage.py" "$ROOT_DIR" "$RUN/results"; then
    printf 'Coverage is reported for review and does not fail functional verification.\n' >&2
fi
