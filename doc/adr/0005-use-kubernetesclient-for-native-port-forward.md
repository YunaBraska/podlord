# 0005 Use KubernetesClient For Native Port Forward

## Status

Accepted

## Context

Podlord must not depend on `kubectl` for normal runtime behavior. Port forwarding is the last user-facing path that still needs Kubernetes streaming semantics instead of plain REST JSON calls.

Kubernetes port-forward uses the pod `portforward` subresource over a WebSocket streaming protocol. The transport includes Kubernetes-specific channel framing and port-forward payload handling. A local hand-rolled WebSocket tunnel was able to open the socket but failed against a real k3d cluster because it did not correctly handle the Kubernetes stream demuxing behavior.

## Decision

Use the official C# `KubernetesClient` package for the WebSocket port-forward connection and `StreamDemuxer`.

Podlord still owns:

- local loopback listener lifecycle
- Pod/Service target resolution
- UI task state
- local port validation
- start/stop behavior

The Kubernetes client library owns:

- kubeconfig-backed WebSocket authentication
- Kubernetes WebSocket subprotocol negotiation
- stream demuxing
- port-forward channel framing

## Consequences

Runtime port forwarding is native, cross-platform, and does not require `kubectl`.

`kubectl` may remain useful as an optional fallback later, but it must not be the default path.

The k3d integration test verifies the native path against a real Kubernetes API server.
