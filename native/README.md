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

`sh scripts/test-native.sh` runs CLI/UI checks, benchmarks and the unchanged 95% line/90% branch gates. `sh scripts/test-native-kubernetes.sh` exercises real local Kubernetes through Colima/Docker and removes only its own container/volumes. Only macOS arm64 has execution evidence. Signing, platform, visual, performance and coverage gates still block release.

[Contract](../doc/spec/podlord-operational-spec.md) | [Roadmap](../doc/roadmap.md) | [Test evidence](../doc/spec/k3d-test-map.md) | [Read workspace](../doc/adr/0018-native-read-workspace.md)
