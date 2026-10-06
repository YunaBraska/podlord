#!/bin/sh
set -eu
umask 077
if [ "$#" -ne 1 ]; then
    printf 'Usage: %s /absolute/new/toolchain-directory\n' "$0" >&2
    exit 2
fi
case "$1" in /*) TARGET=$1 ;; *) printf 'Toolchain directory must be absolute.\n' >&2; exit 2 ;; esac
if [ "$(uname -s)" != Darwin ] || [ -e "$TARGET" ] || [ -L "$TARGET" ]; then
    printf 'Requires macOS and a new toolchain directory.\n' >&2
    exit 2
fi
for tool in python3 cmake ninja curl shasum tar perl make; do
    command -v "$tool" >/dev/null 2>&1 || { printf 'Required tool is missing: %s\n' "$tool" >&2; exit 2; }
done
QT_VERSION=6.11.2
YAML_VERSION=0.9.0
YAML_SHA=25cb043240f828a8c51beb830569634bc7ac603978e0f69d6b63558dadefd49a
SSL_VERSION=3.5.9
SSL_SHA=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
mkdir -p "$(dirname -- "$TARGET")"
mkdir "$TARGET"
complete=false
cleanup() {
    if [ "$complete" != true ]; then
        find "$TARGET" -type f -exec rm {} +
        find "$TARGET" -type l -exec rm {} +
        find "$TARGET" -depth -type d -exec rmdir {} +
    fi
}
trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
python3 -m venv "$TARGET/installer"
"$TARGET/installer/bin/pip" install --disable-pip-version-check aqtinstall==3.3.0
"$TARGET/installer/bin/aqt" install-qt mac desktop "$QT_VERSION" clang_64 -m qtmultimedia qtwebsockets -O "$TARGET/qt"
ln -s "qt/$QT_VERSION/macos" "$TARGET/qt-sdk"
curl --fail --location --silent --show-error "https://github.com/jbeder/yaml-cpp/archive/refs/tags/yaml-cpp-$YAML_VERSION.tar.gz" -o "$TARGET/yaml-cpp-source.tar.gz"
printf '%s  %s\n' "$YAML_SHA" "$TARGET/yaml-cpp-source.tar.gz" | shasum -a 256 -c -
mkdir "$TARGET/yaml-cpp-source"
tar -xzf "$TARGET/yaml-cpp-source.tar.gz" --strip-components=1 -C "$TARGET/yaml-cpp-source"
cmake -S "$TARGET/yaml-cpp-source" -B "$TARGET/yaml-cpp-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DCMAKE_INSTALL_PREFIX="$TARGET/yaml-cpp" -DYAML_BUILD_SHARED_LIBS=ON \
    -DYAML_CPP_BUILD_TESTS=OFF -DYAML_CPP_BUILD_TOOLS=OFF
cmake --build "$TARGET/yaml-cpp-build" --parallel "${PODLORD_BUILD_JOBS:-4}"
cmake --install "$TARGET/yaml-cpp-build"
curl --fail --location --silent --show-error "https://github.com/openssl/openssl/releases/download/openssl-$SSL_VERSION/openssl-$SSL_VERSION.tar.gz" -o "$TARGET/openssl-source.tar.gz"
printf '%s  %s\n' "$SSL_SHA" "$TARGET/openssl-source.tar.gz" | shasum -a 256 -c -
mkdir "$TARGET/openssl-source" "$TARGET/openssl-build"
tar -xzf "$TARGET/openssl-source.tar.gz" --strip-components=1 -C "$TARGET/openssl-source"
(
    cd "$TARGET/openssl-build"
    CFLAGS=-mmacosx-version-min=13.0 perl "$TARGET/openssl-source/Configure" "darwin64-$(uname -m)-cc" \
        shared no-tests --prefix="$TARGET/openssl" --openssldir="$TARGET/openssl/ssl"
    make -j "${PODLORD_BUILD_JOBS:-4}"
    make install_sw
)
{
    printf 'Qt: %s (official macOS desktop archives, qtmultimedia and qtwebsockets)\n' "$QT_VERSION"
    printf 'Installer: aqtinstall 3.3.0\nyaml-cpp: %s\nyaml-cpp source SHA256: %s\n' "$YAML_VERSION" "$YAML_SHA"
    printf 'OpenSSL: %s LTS\nOpenSSL source SHA256: %s\n' "$SSL_VERSION" "$SSL_SHA"
    printf 'Build architecture: %s\nCandidate deployment floor: macOS 13.0\n' "$(uname -m)"
    cmake --version
    xcrun clang --version
} > "$TARGET/build-inputs.txt"
complete=true
printf 'Qt prefix: %s/qt/%s/macos\nyaml-cpp prefix: %s/yaml-cpp\nOpenSSL prefix: %s/openssl\n' "$TARGET" "$QT_VERSION" "$TARGET" "$TARGET"
