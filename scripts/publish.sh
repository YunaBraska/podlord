#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DOTNET="$ROOT_DIR/.tools/dotnet/dotnet"
CONFIGURATION=${CONFIGURATION:-Release}
REQUESTED_RID=${1:-all}
VERSION=${VERSION:-0.1.0}

if [ ! -x "$DOTNET" ]; then
  DOTNET=dotnet
fi

if [ "$REQUESTED_RID" = "all" ]; then
  RIDS="osx-arm64 osx-x64 linux-x64 linux-arm64 win-x64 win-arm64"
else
  RIDS="$REQUESTED_RID"
fi

cd "$ROOT_DIR"

for rid in $RIDS; do
  echo "Publishing Podlord for $rid"
  "$DOTNET" publish src/Podlord.App/Podlord.App.csproj \
    -c "$CONFIGURATION" \
    -r "$rid" \
    --self-contained true \
    -p:PublishSingleFile=true \
    -p:IncludeNativeLibrariesForSelfExtract=true \
    -p:Version="$VERSION" \
    -o "out/podlord-$rid"
done
