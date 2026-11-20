# Local Kubernetes test map

The native test suites use disposable local Kubernetes fixtures. The test
scripts own their containers, profiles and cleanup. They must not remove
unrelated resources.

| Boundary | Command | Result to check |
| --- | --- | --- |
| Native behavior and UI | `sh scripts/test-native.sh` | CTest result and coverage report |
| Kubernetes API and UI | `sh scripts/test-native-kubernetes.sh` | Real resource reads, actions and owned cleanup |
| Platform packages | `.github/workflows/native-qualify.yml` | Installation and runtime checks for each built target |
| Release | `.github/workflows/release.yml` | Qualified artifacts, signing, Store package and publication gates |

A test result applies to its exact source revision and environment. A green
component test alone does not prove a packaged app or a live provider login.
Record a new release qualification with its commit, platform artifacts and
remaining limitations in the release or pull request. Do not copy transient
logs into this specification.
