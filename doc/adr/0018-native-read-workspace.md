# 0018 - How Does The Native Read Workspace Own Its Work?

## Status

Accepted implementation boundary; full behavioral conformance remains pending.

## Decision

Use the existing native source/session stores, Qt Network for direct Kubernetes HTTP/TLS, Qt Concurrent for local store operations, and a thin Qt Quick Controls presentation. No companion process, browser surface, custom HTTP stack or second persistence index is introduced.

The source store owns connection resolution from the verified owned bytes. Referenced files resolve against the original source directory, not the snapshot directory. Static bearer/token-file, basic and client-certificate authentication are implemented. CA data/file and peer-name configuration are passed to Qt TLS. Exec credential authentication follows [ADR 0019](0019-confirmed-exec-authentication.md), with explicit confirmation and no terminal stdin. Unimplemented terminal-only exec, reserved exec cluster configuration extensions, auth-provider, explicit proxy and impersonation configurations fail explicitly without running providers or falling back to another identity. These are required remaining migration capabilities, not removed scope. Ambient proxy handling and compression controls also remain to be reconciled.

One process-owned serialized queue binds every request and result to its session and credential context. Inspector reads precede waiting background reads; equivalent queued/running reads are shared only within that session. Closing a tab or replacing an inspector selection removes obsolete unsent work. Sent reads finish and update their originating cache. A 401 suspends the credential context; a separate confirmation authorizes one retry. Redirects are refused and authentication/cookie reuse is disabled so another endpoint cannot receive credentials implicitly. Raw response and credential bodies are never used as error messages.

Discovery uses core versions and preferred API-group versions rather than a fixed list of resource kinds. Namespaced scopes and pagination retain their original collection. Malformed or failed lists retain the previous collection instead of replacing it with partial pages. Kubernetes may omit an item's `kind`; the discovery contract supplies it. Resource instance names are encoded as path segments, including valid RBAC names containing colons.

The transient read cache uses the existing 24-hour list and five-minute detail cutoffs. Closing a tab does not renew their timestamps. A hot session within the existing 25-second list freshness window opens without another scan. Explicit refresh rediscovers the API. [ADR 0020](0020-native-sync-cache-ownership.md) adds central synchronization, visible-model expiry, expired-cache reclamation and the configurable request-rate ceiling. Independent discovery freshness and membership reconciliation remain pending. This work does not introduce persistent startup resource caches or claim complete SYN conformance.

QML reads table and catalog snapshots. Status changes do not republish unchanged resource tables. Table updates insert/remove/update rows by stable resource path; filters and tri-state sorting run on the local model. Identity color is an additional marker, not the text contrast or sole identifier. The initial inspector exposes only metadata summaries, never Secret data or a misleading YAML editor.

## Verification And Rollback

Source display aliases remain owned by the existing kubeconfig snapshot store,
not a second metadata file or UI cache. Records without aliases retain format 1;
records with aliases use format 2 and keep the original YAML bytes, content hash,
canonical context name and credential identity. Both formats are validated on
read. The existing source lock and atomic file publication own imports and alias
updates. Identical reimports preserve metadata; changed content creates a separate
snapshot. Older native builds reject format 2 instead of silently dropping aliases.
Resetting all aliases restores format 1. The separate C# reference profile is not
changed. Source deletion across session bindings remains a separate decision.

The test map owns UI, application-CLI and real local Kubernetes evidence and the still-failing whole-native coverage gate. External HTTP tests simulate Kubernetes only; owned stores, request handling and UI are real. The real Kubernetes runner owns one loopback-only k3s container and removes it and its anonymous volumes on success, failure or interruption. It retains the pinned image and never prunes shared Docker state.

The native development bundle has a distinct identity and requires an explicit private profile. The preserved legacy reference and launch scripts remain independently runnable. No legacy code or data format is retired; actual dual-lane scenario parity, migration/rollback, packaged dependencies, performance budgets and other platforms remain separate gates.
