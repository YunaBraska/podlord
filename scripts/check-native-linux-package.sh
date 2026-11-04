#!/bin/sh
set -eu
umask 077
ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
if [ "$#" -ne 1 ]; then
    printf 'Usage: %s /absolute/new/evidence-directory\n' "$0" >&2
    exit 2
fi
case "$1" in /*) OUTPUT=$1 ;; *) printf 'An absolute output directory is required.\n' >&2; exit 2 ;; esac
if [ -e "$OUTPUT" ] || [ -L "$OUTPUT" ]; then
    printf 'Refusing to replace existing evidence: %s\n' "$OUTPUT" >&2
    exit 2
fi
if [ "$(uname -s)" != Linux ]; then
    printf 'This package check requires Linux.\n' >&2
    exit 2
fi
case "$(uname -m)" in aarch64|arm64) ARCH=arm64 ;; x86_64) ARCH=x64 ;; *) printf 'Unsupported architecture.\n' >&2; exit 2 ;; esac
mkdir -p "$(dirname -- "$OUTPUT")"
WORK=$(mktemp -d "$(dirname -- "$OUTPUT")/.podlord-linux-package.XXXXXX")
BUILD=${PODLORD_LINUX_PACKAGE_BUILD_DIR:-$WORK/build}
cleanup() { if [ -d "$WORK" ]; then rm -r "$WORK"; fi; }
trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
cmake -S "$ROOT/native" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DPODLORD_COVERAGE=OFF \
    -DPODLORD_PACKAGE_LINUX=ON -DCMAKE_INSTALL_PREFIX="$WORK/podlord" \
    -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH:-}" -DOPENSSL_ROOT_DIR="${OPENSSL_ROOT_DIR:-/usr}"
cmake --build "$BUILD" --target podlord-native --parallel "${PODLORD_BUILD_JOBS:-4}"
cmake --install "$BUILD"
# Match the reference SDK's image codecs. The optional extra imageformats
# module is not used by the product and would pull in TIFF/WebP system libraries.
for codec in icns tga tiff wbmp webp; do
    plugin="$WORK/podlord/plugins/imageformats/libq$codec.so"
    if [ -f "$plugin" ]; then rm "$plugin"; fi
done
# Remove only unneeded ELF symbol/debug sections. Dynamic symbols remain replaceable.
find "$WORK/podlord" -type f -exec sh -c '
    for binary do
        if readelf -h "$binary" >/dev/null 2>&1; then strip --strip-unneeded "$binary"; fi
    done
' sh {} +
mkdir "$OUTPUT"
mv "$WORK/podlord" "$OUTPUT/podlord"
find "$OUTPUT/podlord" -type f -exec sh -c '
    for binary do
        if readelf -h "$binary" >/dev/null 2>&1; then
            printf "\n%s\n" "$binary"
            readelf -d "$binary"
            readelf --version-info "$binary"
        fi
    done
' sh {} + > "$OUTPUT/elf-dependencies.txt"
INSTALLED=$(find "$OUTPUT/podlord" -type f -printf '%s\n' | awk '{total+=$1} END {printf "%.0f\n", total}')
ARCHIVE="podlord-native-linux-$ARCH.tar.gz"
tar -czf "$OUTPUT/$ARCHIVE" -C "$OUTPUT" podlord
DOWNLOAD=$(stat -c '%s' "$OUTPUT/$ARCHIVE")
{
    printf 'Platform: Linux %s\nInstalled bytes: %s\nDownload bytes: %s\n' "$ARCH" "$INSTALLED" "$DOWNLOAD"
    printf 'Limits: installed 100000000 bytes; download 50000000 bytes.\n'
    printf 'Build baseline: '; ldd --version | head -n 1
    printf 'Scope: package construction and size, not clean-runtime execution or full release approval.\n'
    (cd "$OUTPUT" && sha256sum "$ARCHIVE")
} > "$OUTPUT/package-evidence.txt"
cat "$OUTPUT/package-evidence.txt"
if [ "$INSTALLED" -gt 100000000 ] || [ "$DOWNLOAD" -gt 50000000 ]; then
    printf 'Package size gate failed. Evidence retained at %s\n' "$OUTPUT" >&2
    exit 1
fi
