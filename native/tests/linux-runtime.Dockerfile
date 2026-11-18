ARG RUNTIME_IMAGE=debian:13.7-slim@sha256:f6b08b09a05725f9431502d3b5714f5c4576e82c1a84fa1debe0c24a7e0c3ec4
FROM ${RUNTIME_IMAGE}
# Ordinary OS/display libraries only. No Qt, compiler, SDK, kubectl or .NET.
RUN apt-get update && apt-get install --yes --no-install-recommends \
    ca-certificates fonts-dejavu-core fontconfig libgl1 libegl1 libopengl0 \
    libdbus-1-3 libglib2.0-0t64 libxkbcommon-x11-0 libxcb-cursor0 libxcb-icccm4 \
    libxcb-image0 libxcb-keysyms1 libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 \
    libxcb-xfixes0 libxcb-randr0 libx11-xcb1 libxrender1 libxi6 libxext6 libxfixes3 \
    libpulse0 libasound2t64 libwayland-client0 libwayland-cursor0 libwayland-egl1 \
    xvfb xauth x11-utils weston \
    && rm -r /var/lib/apt/lists/*
ENV LANG=C.UTF-8
