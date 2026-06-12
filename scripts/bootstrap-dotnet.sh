#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DOTNET_DIR="$ROOT_DIR/.tools/dotnet"
INSTALL_SCRIPT="$ROOT_DIR/.tools/dotnet-install.sh"

mkdir -p "$DOTNET_DIR"

if [ ! -x "$DOTNET_DIR/dotnet" ]; then
  curl -fsSL https://dot.net/v1/dotnet-install.sh -o "$INSTALL_SCRIPT"
  sh "$INSTALL_SCRIPT" --channel 10.0 --install-dir "$DOTNET_DIR"
fi

"$DOTNET_DIR/dotnet" --info
