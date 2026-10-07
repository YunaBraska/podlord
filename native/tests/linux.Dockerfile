FROM ubuntu:26.04@sha256:3595d7fc4286a33fad0fd853a4063e654287a9c3787437d7937c94ca3f7a804e

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    ca-certificates build-essential clang llvm libclang-rt-dev cmake ninja-build nodejs openssl \
    libyaml-cpp-dev qt6-base-dev qt6-declarative-dev qt6-multimedia-dev qt6-websockets-dev \
    qml6-module-qtqml qml6-module-qtqml-models qml6-module-qtqml-workerscript \
    qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-dialogs \
    qml6-module-qtquick-layouts qml6-module-qtquick-templates qml6-module-qtquick-window \
    qml6-module-qtmultimedia fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/*

ENV CC=clang CXX=clang++ HOME=/tmp/podlord-home CMAKE_PREFIX_PATH=/usr \
    PODLORD_NATIVE_BUILD_DIR=/output/build PODLORD_BUILD_JOBS=4 PODLORD_TEST_JOBS=6 \
    QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software LANG=C.UTF-8 LC_ALL=C.UTF-8
WORKDIR /source
CMD ["sh", "scripts/test-native.sh"]
