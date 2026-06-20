#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DOTNET="$ROOT_DIR/.tools/dotnet/dotnet"
CONFIGURATION=${CONFIGURATION:-Release}
RID=${1:-}
REQUESTED_RID=${1:-}
APP_NAME=Podlord
BUNDLE_ID=${BUNDLE_ID:-dev.podlord.app}
VERSION=${VERSION:-0.0.0}
CODESIGN_IDENTITY=${CODESIGN_IDENTITY:--}

if [ ! -x "$DOTNET" ]; then
  DOTNET=dotnet
fi

if [ -z "$RID" ]; then
  case "$(uname -m)" in
    arm64) RID=osx-arm64 ;;
    x86_64) RID=osx-x64 ;;
    *) echo "Unsupported macOS architecture. Pass macos-arm64 or macos-x64 explicitly." >&2; exit 1 ;;
  esac
fi

case "$RID" in
  macos-arm64) RID=osx-arm64 ;;
  macos-x64) RID=osx-x64 ;;
esac

case "$RID" in
  osx-arm64|osx-x64) ;;
  *) echo "Unsupported macOS runtime '${REQUESTED_RID:-$RID}'. Use macos-arm64 or macos-x64." >&2; exit 1 ;;
esac

if ! command -v iconutil >/dev/null 2>&1; then
  echo "iconutil is required to build a macOS app bundle." >&2
  exit 1
fi

if ! command -v sips >/dev/null 2>&1; then
  echo "sips is required to build a macOS app bundle icon." >&2
  exit 1
fi

PUBLISH_DIR="$ROOT_DIR/out/podlord-$RID-publish"
BUNDLE_DIR="$ROOT_DIR/out/$APP_NAME.app"
CONTENTS_DIR="$BUNDLE_DIR/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"
RESOURCES_DIR="$CONTENTS_DIR/Resources"
ICON_TMP=$(mktemp -d "${TMPDIR:-/tmp}/podlord-icon.XXXXXX")
ICONSET_DIR="$ICON_TMP/$APP_NAME.iconset"

cleanup() {
  rm -rf "$ICON_TMP"
}
trap cleanup EXIT INT TERM

cd "$ROOT_DIR"
rm -rf "$PUBLISH_DIR" "$BUNDLE_DIR"

"$DOTNET" publish src/Podlord.App/Podlord.App.csproj \
  -c "$CONFIGURATION" \
  -r "$RID" \
  --self-contained true \
  -p:PublishSingleFile=false \
  -p:UseAppHost=true \
  -p:IncludeNativeLibrariesForSelfExtract=false \
  -p:DebugType=none \
  -p:DebugSymbols=false \
  -p:Version="$VERSION" \
  -o "$PUBLISH_DIR"

mkdir -p "$MACOS_DIR" "$RESOURCES_DIR" "$ICONSET_DIR"
cp -R "$PUBLISH_DIR"/. "$MACOS_DIR"/
chmod +x "$MACOS_DIR/Podlord.App"

ICON_DIR="$ROOT_DIR/src/Podlord.App/Assets/Brand/Icons"
sips -z 16 16 "$ICON_DIR/podlord-icon-32.png" --out "$ICONSET_DIR/icon_16x16.png" >/dev/null
cp "$ICON_DIR/podlord-icon-32.png" "$ICONSET_DIR/icon_16x16@2x.png"
cp "$ICON_DIR/podlord-icon-32.png" "$ICONSET_DIR/icon_32x32.png"
cp "$ICON_DIR/podlord-icon-64.png" "$ICONSET_DIR/icon_32x32@2x.png"
cp "$ICON_DIR/podlord-icon-128.png" "$ICONSET_DIR/icon_128x128.png"
cp "$ICON_DIR/podlord-icon-256.png" "$ICONSET_DIR/icon_128x128@2x.png"
cp "$ICON_DIR/podlord-icon-256.png" "$ICONSET_DIR/icon_256x256.png"
cp "$ICON_DIR/podlord-icon-512.png" "$ICONSET_DIR/icon_256x256@2x.png"
cp "$ICON_DIR/podlord-icon-512.png" "$ICONSET_DIR/icon_512x512.png"
cp "$ICON_DIR/podlord-icon-1024.png" "$ICONSET_DIR/icon_512x512@2x.png"
iconutil -c icns "$ICONSET_DIR" -o "$RESOURCES_DIR/$APP_NAME.icns"

cat > "$CONTENTS_DIR/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key>
  <string>$APP_NAME</string>
  <key>CFBundleDisplayName</key>
  <string>$APP_NAME</string>
  <key>CFBundleIdentifier</key>
  <string>$BUNDLE_ID</string>
  <key>CFBundleVersion</key>
  <string>$VERSION</string>
  <key>CFBundleShortVersionString</key>
  <string>$VERSION</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleExecutable</key>
  <string>Podlord.App</string>
  <key>CFBundleIconFile</key>
  <string>$APP_NAME.icns</string>
  <key>LSMinimumSystemVersion</key>
  <string>12.0</string>
  <key>NSHighResolutionCapable</key>
  <true/>
</dict>
</plist>
EOF

if [ "$CODESIGN_IDENTITY" != "skip" ]; then
  if ! command -v codesign >/dev/null 2>&1; then
    echo "codesign is required to sign the macOS app bundle. Set CODESIGN_IDENTITY=skip to build without signing." >&2
    exit 1
  fi

  SIGN_LOG="$ICON_TMP/codesign.log"
  if ! codesign --force --deep --sign "$CODESIGN_IDENTITY" "$BUNDLE_DIR" >"$SIGN_LOG" 2>&1; then
    cat "$SIGN_LOG" >&2
    exit 1
  fi

  if ! codesign --verify --deep --strict "$BUNDLE_DIR" >"$SIGN_LOG" 2>&1; then
    cat "$SIGN_LOG" >&2
    exit 1
  fi
fi

echo "$BUNDLE_DIR"
