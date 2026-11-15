# Native Runtime

C++/Qt Quick application with owned kubeconfig snapshots, cache-first Kubernetes views, logs, confirmed YAML updates, deletion and direct API port forwarding. Full migration and release gates remain open. Normal operations do not require `kubectl`.

On macOS, install the pinned Qt/yaml-cpp/OpenSSL SDK into a new absolute directory and build from the repository root:

```sh
toolchain="$HOME/.local/share/podlord-toolchain"
/bin/sh scripts/install-native-macos-toolchain.sh "$toolchain"
export CMAKE_PREFIX_PATH="$toolchain/qt-sdk;$toolchain/yaml-cpp;$toolchain/openssl"
export PATH="$toolchain/qt-sdk/bin:$PATH"
cmake -S native -B ../podlord-native-build -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH" -DOPENSSL_ROOT_DIR="$toolchain/openssl"
cmake --build ../podlord-native-build
../podlord-native-build/podlord-native.app/Contents/MacOS/podlord-native --profile /absolute/private/profile
```

The installer refuses existing destinations. Requires C++20, CMake, Ninja, Python, curl, Git and the Xcode command-line tools. On other desktop systems launch `podlord-native` directly; the macOS installer is not a cross-platform installer. No ambient kubeconfig is imported.

For a filter-only change, run `ctest --test-dir ../podlord-native-build -L behavior -R '^native.field_filter\.' --parallel 6 --output-on-failure`. The `behavior` label excludes duplicate Fusion/Basic runs; `style-variant` retains them for theme/style checks. API-only filter contracts do not load QML and run once, without meaningless style duplicates. Target the affected executable when building. Do not rebuild and run the complete suite for each local UI edit.

`sh scripts/test-native.sh` runs the complete CLI/UI suite and benchmarks, including style variants. The release coverage gates are 95% lines and 85% branches overall, with explicit 90% branch gates for new production components. See [ADR 0035](../doc/adr/0035-native-preflight-gates-before-release.md) for the distinction between release requirements and diagnostic CI reporting. `sh scripts/test-native-kubernetes.sh` exercises real local Kubernetes through Colima/Docker and removes only its own container/volumes. Linux CI uses the pinned [container toolchain](tests/linux.Dockerfile) on x86_64 and arm64; macOS uses the pinned SDK on arm64 and Intel. Matrix configuration is not passing execution evidence. Functional, platform, visual and performance gates remain open. Stable-release CI imports the stored Developer ID certificate, signs and notarizes the direct macOS assets after native qualification. A hosted successful release remains the required evidence for that path.

## Android SDK preparation

Use matching Qt 6.11.3 host and Android kits, Android SDK/NDK, JDK 21, and the
ABI-matched yaml-cpp/OpenSSL dependencies. Set the variables below to absolute
development paths. Choose new destinations for both `QT_ANDROID_PREFIX` and
`QT_FIX_WORK`. Never patch the shared stock kit or reuse an old app build cache.

```sh
cmake -E copy_directory "$QT_ANDROID_STOCK" "$QT_ANDROID_PREFIX"
cmake -DQT_SDK_PREFIX="$QT_ANDROID_PREFIX" -DWORK_ROOT="$QT_FIX_WORK" \
  -DQT_HOST_PATH="$QT_HOST_PREFIX" -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
  -DANDROID_ABI=arm64-v8a -P scripts/qt-quick-shutdown.cmake
cmake -S native -B "$ANDROID_BUILD_DIR" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$QT_ANDROID_PREFIX/lib/cmake/Qt6/qt.toolchain.cmake" \
  -DQT_HOST_PATH="$QT_HOST_PREFIX" -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
  -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" -DANDROID_ABI=arm64-v8a \
  "-DCMAKE_PREFIX_PATH=$QT_ANDROID_PREFIX;$ANDROID_YAML_PREFIX" \
  -DPODLORD_ANDROID_OPENSSL_ROOT="$ANDROID_OPENSSL_PREFIX" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "$ANDROID_BUILD_DIR" --target aab --parallel 4
```

The helper verifies the upstream archive and patch, rebuilds Quick and its
required plugins/Java assets, then records the fix in the SDK itself. App
configuration rejects a missing or mismatched manifest at the actual
`Qt6Quick_DIR` prefix. It never downloads or modifies an SDK automatically.
Host build tools are not runtime dependencies of the installed application.

The local test package is `podlord-native-android-arm64.apk.zip`. A release build
uses the `aab` target and a monotonically increasing Android version code derived
from the same three-part PodLord release version. An AAB is uploaded to Google
Play and is not installed directly. All runtime dependencies remain embedded.
The 50 MB download and 100 MB installed-footprint figures are separate advisory
targets, not uniform cross-platform release gates.

Linux standalone packaging is being verified separately in the
[distribution trial](../.github/workflows/native-linux-package.yml). It builds
the pinned source toolchain, runs native behavior/coverage checks, creates the
archive, and starts its extracted payload on current Ubuntu and Debian with X11
and Wayland but no installed Qt. Trial artifacts are not release approval and
are not collected by the release-candidate workflow yet.

The Linux trial reuses only an exact compiler/architecture/recipe-matched public
SDK prefix. System dependencies, including OpenSSL, are rebuilt fresh. Cache
restoration does not skip application builds, tests, coverage or package gates.

[Contract](../doc/spec/podlord-operational-spec.md) | [Roadmap](../doc/ROADMAP.md) | [Test evidence](../doc/spec/k3d-test-map.md) | [Read workspace](../doc/adr/0018-native-read-workspace.md)
