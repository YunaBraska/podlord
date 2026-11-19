# 0015 - C++ and Qt Quick for device coverage

Status: accepted.

PodLord uses C++ for its domain, Kubernetes client, cache and platform services.
Qt Quick presents the shared workspace on macOS, Windows, Linux, Android and
iOS. Platform-specific wrappers are kept small. The product has no embedded
browser frontend.

The choice of toolkit does not establish performance or platform parity. Measure
render latency, request counts, package size and runtime behavior at public
boundaries. Package only modules needed by supported features. Authentication,
files, networking, audio and port forwarding require target-specific evidence.

The [operational specification](../spec/podlord-operational-spec.md) owns behavior
and release requirements. [ADR 0035](0035-native-preflight-gates-before-release.md)
owns qualification of native artifacts.
