# One ABI baseline for both architectures and newer Ubuntu/Debian desktops.
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
FROM toolchain-dependencies
COPY scripts/build-native-linux-toolchain.sh /opt/build-native-linux-toolchain.sh
RUN sh /opt/build-native-linux-toolchain.sh /opt/podlord-toolchain
ENV CMAKE_PREFIX_PATH=/opt/podlord-toolchain/qt:/opt/podlord-toolchain/yaml \
    PATH=/opt/podlord-toolchain/qt/bin:/opt/sbom/bin:$PATH
WORKDIR /source
