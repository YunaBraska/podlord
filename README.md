# Podlord

> Rule your clusters before they rule you.

[![CI](https://github.com/YunaBraska/podlord/actions/workflows/ci.yml/badge.svg)](https://github.com/YunaBraska/podlord/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

A cache-first Kubernetes console with an RTS-inspired map, resource tables,
filters, inspector, container terminal and port forwarding.

**C++/Qt Quick is the active implementation. Native release validation is still
in progress.** C#/.NET/Avalonia remains a separately buildable functional and
visual reference, not a runtime dependency of the replacement.

## Start Developing

Follow the [native build and launch instructions](native/README.md) for the
pinned toolchain and a separate application profile. From the repository root,
with that toolchain configured:

```sh
cmake -S native -B ../podlord-native-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build ../podlord-native-build --target podlord-native --parallel 4
```

The native guide includes platform launch paths. Kubeconfigs are explicitly
imported into immutable, app-owned snapshots. Normal use does not modify the
original file or require `kubectl`, .NET, Docker or a browser frontend. Confirmed
browser authentication opens the system browser.

## Product And Platform Contract

The first complete native release targets macOS, Windows, Linux, Android and
iOS. A passing desktop build alone does not establish that release. Packages
must include their application runtimes and pass the applicable installation,
function, visual and performance checks.

- [Operational specification](doc/spec/podlord-operational-spec.md)
- [Public behavior, Kubernetes scenarios and current evidence](doc/spec/k3d-test-map.md)
- [Native release gates and reference policy](doc/adr/0035-native-preflight-gates-before-release.md)
- [Architecture decisions](doc/adr/0015-cpp-rewrite-for-performance-and-device-coverage.md)
- [Roadmap](doc/ROADMAP.md)

## Test

```sh
sh scripts/test-native.sh
sh scripts/test-native-kubernetes.sh
```

The first command builds and runs native tests, benchmarks and coverage gates.
The second exercises real, disposable local Kubernetes and requires Docker or
Colima for tests only. Owned test containers and data must be cleaned up.

Native coverage requires 95% lines and 85% branches overall, with explicit 90%
branch gates for new production logic. Counts include meaningful style variants,
not that many different product features. Use the native guide's targeted checks
while developing rather than rebuilding the complete suite after every edit.

The [manual C# reference workflow](.github/workflows/csharp-reference.yml)
retains its tests and original coverage checks separately. Reference gaps must
not be disguised as native evidence.

## Release Candidates

The [native candidate workflow](.github/workflows/release.yml) is manually
started and collects checked artifacts. It does not create a tag or publish a
release. The [Store preparation workflow](.github/workflows/app-store.yml)
prepares native macOS packaging without uploading to the Store.

Existing [published releases](https://github.com/YunaBraska/podlord/releases)
and installation channels may still contain legacy C# builds. They are not
proof that the native replacement is released. Native distribution names start
with `podlord-native-`. Final platform support and remaining blockers belong in
the linked specification and test map, not an inferred download promise.

Developer-ID signing and notarization are deferred for the private release.
Other agreed gates remain in effect.

## Design Reference

The existing [resource explorer](doc/screenshots/resource-explorer.png) and
[inspector](doc/screenshots/inspector-settings.png) captures show the C# reference.
Current native comparisons and their build identities are recorded in the test
map. Reference screenshots are not presented as native release screenshots.

## Contributing And Security

See [CONTRIBUTING.md](CONTRIBUTING.md), [Code of Conduct](CODE_OF_CONDUCT.md)
and [SECURITY.md](SECURITY.md). Keep behavior explicit, protect credentials and
Secrets, and use the owned Kubernetes request queue.

Bugs and ideas belong in the [issue tracker](https://github.com/YunaBraska/podlord/issues/new).
Support is welcome through [GitHub Sponsors](https://github.com/sponsors/YunaBraska),
[Buy Me a Coffee](https://buymeacoffee.com/YunaBraska),
[Ko-fi](https://ko-fi.com/YunaBraska) or [Liberapay](https://liberapay.com/YunaBraska).

## License

Project code is [MIT licensed](LICENSE). Bundled libraries retain their own
[licenses and notices](native/licenses/THIRD-PARTY-NOTICES.txt).
