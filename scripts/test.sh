#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DOTNET="$ROOT_DIR/.tools/dotnet/dotnet"
PATH="$ROOT_DIR/.tools/bin:$PATH"
export PATH

if [ ! -x "$DOTNET" ]; then
  DOTNET=dotnet
fi

cd "$ROOT_DIR"
"$ROOT_DIR/scripts/bootstrap-k3d.sh"
TEST_HOME="${PODLORD_TEST_HOME:-/tmp/podlord-test-home}"
TEST_CONFIG_HOME="${PODLORD_TEST_CONFIG_HOME:-/tmp/podlord-test-config}"
export PODLORD_CONFIG_HOME="$TEST_CONFIG_HOME/podlord"
export PODLORD_HOME="$TEST_HOME"
mkdir -p "$TEST_HOME" "$TEST_CONFIG_HOME" "$PODLORD_CONFIG_HOME"
rm -rf "$ROOT_DIR/TestResults" "$ROOT_DIR/tests"/*/TestResults
"$DOTNET" test Podlord.slnx --settings "$ROOT_DIR/coverage.runsettings" --collect:"XPlat Code Coverage"
python3 "$ROOT_DIR/scripts/check-coverage.py" "$ROOT_DIR"
