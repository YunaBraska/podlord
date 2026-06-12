#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DOTNET="$ROOT_DIR/.tools/dotnet/dotnet"

if [ ! -x "$DOTNET" ]; then
  DOTNET=dotnet
fi

cd "$ROOT_DIR"
export HOME="${PODLORD_TEST_HOME:-/tmp/podlord-test-home}"
export XDG_CONFIG_HOME="${PODLORD_TEST_CONFIG_HOME:-/tmp/podlord-test-config}"
export PODLORD_CONFIG_HOME="$XDG_CONFIG_HOME/podlord"
export PODLORD_HOME="$HOME"
mkdir -p "$HOME" "$XDG_CONFIG_HOME" "$PODLORD_CONFIG_HOME"
"$ROOT_DIR/scripts/bootstrap-k3d.sh"
rm -rf "$ROOT_DIR/TestResults" "$ROOT_DIR/tests"/*/TestResults
"$DOTNET" test Podlord.slnx --settings "$ROOT_DIR/coverage.runsettings" --collect:"XPlat Code Coverage"
python3 "$ROOT_DIR/scripts/check-coverage.py" "$ROOT_DIR"
