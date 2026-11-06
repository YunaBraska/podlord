# One ABI baseline for both architectures and newer Ubuntu/Debian desktops.
ARG SDK_STAGE=source-sdk
FROM node:24-bookworm-slim@sha256:c86ecead9e55a5210ac1727de38a4ca840bf03dcaf34dc9749addbf383b1920c AS node-tools
FROM ubuntu:24.04@sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852 AS toolchain-dependencies
RUN apt-get update && apt-get install --yes --no-install-recommends \
    ca-certificates curl build-essential clang llvm libclang-rt-dev cmake ninja-build pkg-config nasm fonts-dejavu-core \
    python3 python3-venv xz-utils libssl-dev libgl-dev libegl-dev libopengl-dev \
    libdbus-1-dev libfontconfig1-dev libfreetype-dev libglib2.0-dev libgtk-3-dev \
    libxcb1-dev libxcb-glx0-dev libxcb-keysyms1-dev libxcb-image0-dev libxcb-shm0-dev \
    libxcb-icccm4-dev libxcb-sync-dev libxcb-xfixes0-dev libxcb-shape0-dev \
    libxcb-randr0-dev libxcb-render-util0-dev libxcb-util-dev libxcb-cursor-dev \
    libxcb-xinerama0-dev libxcb-xkb-dev libxkbcommon-dev libxkbcommon-x11-dev \
    libx11-xcb-dev libxrender-dev libxi-dev libxext-dev libxfixes-dev libpulse-dev \
    libasound2-dev libwayland-dev wayland-protocols \
    && rm -r /var/lib/apt/lists/*
RUN python3 -m venv /opt/sbom && /opt/sbom/bin/pip install --no-cache-dir spdx-tools==0.8.3
COPY --from=node-tools /usr/local/bin/node /usr/local/bin/node
ENV CC=clang CXX=clang++ LANG=C.UTF-8
FROM toolchain-dependencies AS source-sdk
COPY scripts/build-native-linux-toolchain.sh /opt/build-native-linux-toolchain.sh
ARG SDK_CACHE_KEY=
RUN sh /opt/build-native-linux-toolchain.sh /opt/podlord-toolchain \
    && printf '%s\n' "$SDK_CACHE_KEY" > /opt/podlord-toolchain/cache-key.txt

FROM toolchain-dependencies AS cached-sdk
ARG SDK_CACHE_KEY=
RUN test -n "$SDK_CACHE_KEY"
COPY --from=toolchain-cache / /opt/podlord-toolchain

FROM ${SDK_STAGE} AS sdk
ARG SDK_CACHE_KEY=
RUN set -eux; \
    printf '%s\n' "$SDK_CACHE_KEY" | cmp - /opt/podlord-toolchain/cache-key.txt; \
    test "$(/opt/podlord-toolchain/qt/bin/qmake -query QT_VERSION)" = 6.11.3; \
    test "$(/opt/podlord-toolchain/qt/bin/qmake -query QT_INSTALL_PREFIX)" = /opt/podlord-toolchain/qt; \
    grep -Eq '^#define QT_FEATURE_icu -1[[:space:]]*$' /opt/podlord-toolchain/qt/include/QtCore/6.11.3/QtCore/private/qtcore-config_p.h; \
    readelf -d /opt/podlord-toolchain/qt/lib/libQt6Core.so.6 > /tmp/qtcore-needed.txt; \
    if grep -q 'NEEDED.*libicu' /tmp/qtcore-needed.txt; then exit 1; fi; \
    test -s /opt/podlord-toolchain/yaml/lib/libyaml-cpp.a; \
    test -s /opt/podlord-toolchain/media/lib/libavcodec.so; \
    grep -q '^FFmpeg: 7.1.5,' /opt/podlord-toolchain/build-inputs.txt; \
    grep -q '^yaml-cpp: 0.9.0,' /opt/podlord-toolchain/build-inputs.txt; \
    find /opt/podlord-toolchain -name 'libssl.so*' -o -name 'libcrypto.so*' > /tmp/cached-openssl.txt; \
    test ! -s /tmp/cached-openssl.txt; \
    rm /tmp/qtcore-needed.txt /tmp/cached-openssl.txt
ENV CMAKE_PREFIX_PATH=/opt/podlord-toolchain/qt:/opt/podlord-toolchain/yaml \
    PATH=/opt/podlord-toolchain/qt/bin:/opt/sbom/bin:$PATH
WORKDIR /source
