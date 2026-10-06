# How does the native application own Kubernetes port forwards?

Status: accepted for the implemented desktop forwarding increment.

## Decision

Use Qt WebSockets for RFC 6455, TLS and socket buffering. The native transport
owns only Kubernetes `v4.channel.k8s.io` channel framing and the loopback TCP
listener. Neither `kubectl`, KubernetesClient nor a .NET companion is required
by this runtime path. ADR 0005 remains the decision for the retained C# reference.

Reuse the existing request owner for authentication material, captured server,
foreground admission, request limits and backoff. A handshake occupies an
admission slot until its outcome; a connected stream does not monopolize the
REST request queue. Explicit writes and Inspector detail/log reads precede
queued forward handshakes; forwarding still precedes passive work.
Streaming uses `Accept: */*`, not the REST JSON header.
TLS preserves configured certificate verification and peer name, with HTTP/1.1
for the WebSocket upgrade. Redirects do not forward credentials to another host.

Bind the requested IPv4 loopback port before starting target resolution. One
forward belongs to one open session. Switching tabs preserves it; stopping it,
closing its session or shutting down releases its listener and connections.
Unsent obsolete work is removed. Already dispatched Service reads complete
without recreating a stopped forward. Other sessions remain unaffected.

Running Pods use current collection membership without requiring an additional
`pods/get` permission. Services require a fresh identity-checked Service read,
its label-selected Pod list and a Running, non-terminating backing Pod. A valid
`v1/PodList` supplies the type of items with omitted type fields; contradictory
types are rejected. Service TCP ports resolve numeric or named Pod target ports.
The resolved Pod is fixed for this forward, not silently reselected on failure.
The Kubernetes subresource addresses a Pod by name; it does not provide a UID
precondition comparable to deletion.

There are at most 64 active/queued TCP connections per listener. Reads use
bounded chunks; incoming frame/message and local output budgets are 1 MiB.
The socket receive budget covers the accepted frame budget plus framing overhead.
A smaller socket budget deadlocks otherwise valid large frames because Qt waits
for their complete payload before delivering them. Protocol/size closure reports
an immediate generic failure rather than waiting for the close-handshake timeout.
Port/channel headers, binary data and exact protocol negotiation are checked.
Remote error bodies and credentials are not copied into user feedback.
No stream retry or interactive login is started automatically.

## Evidence and limits

The public Workspace/QML/TCP suite passes 86 cases in Basic/Fusion. Its
external boundary simulates Kubernetes, not internal forwarding code. The
owned Kubernetes lane also passes real Pod and named-Service HTTP transfers,
then explicit stop and local-port reuse. The test map owns exact run identities.

Qt 6.11's public WebSocket API does not expose failed upgrade response status
and headers. Opaque upgrade failures therefore remain a generic error; full
ERR-001/004 rate-limit/authentication classification is an open release gap,
not inferred from REST target-resolution error tests. Detached-window ownership,
legacy task-table/search/open affordances, multi-platform/device execution and
paired visual parity also remain unverified.

Qt WebSockets adds a shipped module with its own licensing obligations. It is
available under LGPLv3 or GPLv2 according to the [module documentation](https://doc.qt.io/qt-6/qtwebsockets-index.html).
Exact shipped-library source, notices, relinking and target distribution checks
remain release gates under ADR 0015; no project-license change is made here.
