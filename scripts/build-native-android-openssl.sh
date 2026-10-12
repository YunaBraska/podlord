#!/bin/sh
set -eu

if [ "$#" -ne 2 ] || [ -z "${ANDROID_NDK_ROOT:-}" ]; then
    printf '%s\n' 'Usage: ANDROID_NDK_ROOT=/path/to/ndk build-native-android-openssl.sh ABI OUTPUT_ROOT' >&2
    exit 2
fi
abi=$1
output_root=$2
case "$abi" in
    arm64-v8a) target=android-arm64 ;;
    armeabi-v7a) target=android-arm ;;
    x86_64) target=android-x86_64 ;;
    x86) target=android-x86 ;;
    *) printf 'Unsupported Android ABI: %s\n' "$abi" >&2; exit 2 ;;
esac
case "$(uname -s)" in
    Darwin) host=darwin-x86_64 ;;
    Linux) host=linux-x86_64 ;;
    *) printf '%s\n' 'Android OpenSSL builds require macOS or Linux.' >&2; exit 2 ;;
esac
ndk_bin="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/$host/bin"
if [ ! -x "$ndk_bin/clang" ] || ! command -v curl >/dev/null 2>&1 || ! command -v patchelf >/dev/null 2>&1; then
    printf '%s\n' 'Android NDK clang, curl and patchelf are required.' >&2
    exit 2
fi
mkdir -p "$output_root"
output_root=$(cd "$output_root" && pwd -P)
archive="$output_root/openssl-3.5.9.tar.gz"
if [ ! -s "$archive" ]; then
    if ! curl -fL --retry 2 --max-time 180 -o "$archive" 'https://github.com/openssl/openssl/releases/download/openssl-3.5.9/openssl-3.5.9.tar.gz'; then
        rm -f "$archive"
        exit 1
    fi
fi
case "$(uname -s)" in
    Darwin) digest=$(shasum -a 256 "$archive" | awk '{print $1}') ;;
    Linux) digest=$(sha256sum "$archive" | awk '{print $1}') ;;
esac
if [ "$digest" != '603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a' ]; then
    printf '%s\n' 'OpenSSL 3.5.9 source checksum mismatch.' >&2
    exit 1
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' 0 HUP INT TERM
tar -xzf "$archive" -C "$work"
export PATH="$ndk_bin:$PATH"
cd "$work/openssl-3.5.9"
./Configure "$target" -D__ANDROID_API__=28 shared no-tests no-apps
make -s -j "${PODLORD_BUILD_JOBS:-4}" SHLIB_VERSION_NUMBER= build_libs
mkdir -p "$output_root/$abi"
cp libcrypto.so "$output_root/$abi/libcrypto_3.so"
cp libssl.so "$output_root/$abi/libssl_3.so"
patchelf --set-soname libcrypto_3.so "$output_root/$abi/libcrypto_3.so"
patchelf --set-soname libssl_3.so "$output_root/$abi/libssl_3.so"
patchelf --replace-needed libcrypto.so libcrypto_3.so "$output_root/$abi/libssl_3.so"
"$ndk_bin/llvm-readelf" -d "$output_root/$abi/libcrypto_3.so" | grep -F '[libcrypto_3.so]' >/dev/null
"$ndk_bin/llvm-readelf" -d "$output_root/$abi/libssl_3.so" | grep -F '[libcrypto_3.so]' >/dev/null
"$ndk_bin/llvm-readelf" -d "$output_root/$abi/libssl_3.so" | grep -F '[libssl_3.so]' >/dev/null
printf 'Android OpenSSL 3.5.9: %s\n' "$output_root/$abi"
