#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DOTNET="$ROOT_DIR/.tools/dotnet/dotnet"
PATH="$ROOT_DIR/.tools/bin:$PATH"
export PATH

cleanup_k3d_test_artifacts() {
  if command -v k3d >/dev/null 2>&1; then
    k3d cluster list -o json 2>/dev/null \
      | python3 -c 'import json, sys
for cluster in json.load(sys.stdin):
    name = cluster.get("name", "")
    if name.startswith("podlord-it-"):
        print(name)' \
      | while IFS= read -r cluster; do
          [ -n "$cluster" ] || continue
          k3d cluster delete "$cluster" >/dev/null 2>&1 || true
        done
  fi

  if command -v docker >/dev/null 2>&1; then
    docker ps -aq --filter "name=^k3d-podlord-it-" \
      | while IFS= read -r container; do
          [ -n "$container" ] || continue
          docker rm -f "$container" >/dev/null 2>&1 || true
        done

    docker volume ls -q --filter "name=^k3d-podlord-it-" \
      | while IFS= read -r volume; do
          [ -n "$volume" ] || continue
          docker volume rm "$volume" >/dev/null 2>&1 || true
        done

    docker image ls --format '{{.Repository}}:{{.Tag}} {{.ID}}' \
      | awk '/^k3d-podlord-it-/{print $2}' \
      | tail -n +2 \
      | while IFS= read -r image_id; do
          [ -n "$image_id" ] || continue
          docker image rm -f "$image_id" >/dev/null 2>&1 || true
        done

    docker image prune -f >/dev/null 2>&1 || true
  fi
}

trap cleanup_k3d_test_artifacts EXIT INT TERM

if [ ! -x "$DOTNET" ]; then
  DOTNET=dotnet
fi

cd "$ROOT_DIR"
"$ROOT_DIR/scripts/bootstrap-k3d.sh"
cleanup_k3d_test_artifacts
TEST_HOME="${PODLORD_TEST_HOME:-/tmp/podlord-test-home}"
TEST_CONFIG_HOME="${PODLORD_TEST_CONFIG_HOME:-/tmp/podlord-test-config}"
export PODLORD_CONFIG_HOME="$TEST_CONFIG_HOME/podlord"
export PODLORD_HOME="$TEST_HOME"
mkdir -p "$TEST_HOME" "$TEST_CONFIG_HOME" "$PODLORD_CONFIG_HOME"
rm -rf "$ROOT_DIR/TestResults" "$ROOT_DIR/tests"/*/TestResults
"$DOTNET" test Podlord.slnx --settings "$ROOT_DIR/coverage.runsettings" --collect:"XPlat Code Coverage"
python3 "$ROOT_DIR/scripts/check-coverage.py" "$ROOT_DIR"
