# Native Runtime

C++/Qt Quick application with owned kubeconfig snapshots, cache-first Kubernetes views, logs, confirmed YAML updates, deletion and direct API port forwarding. Full migration and release gates remain open. Normal operations do not require `kubectl`.

On macOS, install the pinned Qt/yaml-cpp/OpenSSL SDK into a new absolute directory and build from the repository root:

```sh
toolchain="$HOME/.local/share/podlord-toolchain"
/bin/sh scripts/install-native-macos-toolchain.sh "$toolchain"
export CMAKE_PREFIX_PATH="$toolchain/qt-sdk;$toolchain/yaml-cpp;$toolchain/openssl"
export PATH="$toolchain/qt-sdk/bin:$PATH"
cmake -S native -B ../podlord-native-build -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH"
cmake --build ../podlord-native-build
../podlord-native-build/podlord-native.app/Contents/MacOS/podlord-native --profile /absolute/private/profile
```

The installer refuses existing destinations. Requires C++20, CMake, Ninja, Python, curl and the Xcode command-line tools. On other desktop systems launch `podlord-native` directly; the macOS installer is not a cross-platform installer. No ambient kubeconfig is imported.

For a filter-only change, run `ctest --test-dir ../podlord-native-build -L behavior -R '^native.field_filter\.' --parallel 6 --output-on-failure`. The `behavior` label excludes duplicate Fusion/Basic runs; `style-variant` retains them for theme/style checks. API-only filter contracts do not load QML and run once, without meaningless style duplicates. Target the affected executable when building. Do not rebuild and run the complete suite for each local UI edit.

`sh scripts/test-native.sh` runs the complete CLI/UI suite, benchmarks and reports 95% line/90% branch coverage targets, including style variants. Coverage is diagnostic and does not fail functional verification. `sh scripts/test-native-kubernetes.sh` exercises real local Kubernetes through Colima/Docker and removes only its own container/volumes. Linux CI uses the pinned [container toolchain](tests/linux.Dockerfile) on x86_64 and arm64; macOS uses the pinned SDK on arm64 and Intel. Matrix configuration is not passing execution evidence. Functional, platform, visual and performance gates remain open. Developer-ID signing and notarization are deferred for the private release.

[Contract](../doc/spec/podlord-operational-spec.md) | [Roadmap](../doc/roadmap.md) | [Test evidence](../doc/spec/k3d-test-map.md) | [Read workspace](../doc/adr/0018-native-read-workspace.md)
