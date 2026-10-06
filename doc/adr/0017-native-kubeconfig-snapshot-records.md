# How Does The Native Runtime Retain Imported Kubeconfigs Independently Of Their Original Files?

Status: implementation of the accepted snapshot identity and binding requirements in [ADR 0004](0004-content-addressed-kubeconfig-snapshots.md), within the isolated native profile established by [ADR 0016](0016-native-session-persistence-boundary.md).

## Decision

Each native import is an app-owned, versioned JSON record under the selected profile's `kubeconfigs` directory. Its filename combines SHA-256 hashes of the normalized absolute source path and original content. The record owns source identity, original bytes and import recency. No second source index is persisted.

Original bytes are stored as canonical Base64, retaining UTF-8 BOMs, unknown fields and credential-plugin configuration without invoking plugins. Base64 is not encryption. On the verified macOS filesystem, new profiles and owned directories use owner-only access, and records use owner read/write permissions. Windows ACL and other-platform evidence remains a release gate, not an assumed property of permission flags.

Reimporting identical path/content refreshes recency in the same record. Changed content or a different source path receives a separate record. Existing session context identities and usage history are not changed by import. Source records retain the original absolute path; future connection resolution must use that directory for relative certificate, token-file and explicit relative plugin paths, not the owned-record directory.

Qt Core owns file access, hashes, JSON, profile validation, locking and atomic publication. The installed yaml-cpp parser owns YAML syntax; handwritten YAML parsing is not introduced. Parsed display metadata is derived from owned bytes and is not persisted as a second authority. CLI output omits tokens, private keys and original YAML; parser errors omit payloads. Ambiguous context/cluster/user names produce warnings and broken-reference markers rather than a usable-looking binding.

Listing reports invalid individual records alongside healthy snapshots without changing any file. An unreadable or invalid directory rejects the scan. Import rejects a malformed existing target rather than silently repairing or replacing it. Lock contention is explicit and does not apply an import.

## Scope And Evidence

Native file import and source listing compose with the session CLI. The same store now owns recursive folder import through `podlord-source import-path` and the Qt Quick Import action. `~` and `~/` expand at the source-path boundary; other user-home notation is rejected explicitly. Scans include regular hidden files, do not follow child file/directory symlinks, and exclude the selected private profile subtree. Selecting the private profile itself as a scan root is rejected. Each valid file uses the existing snapshot publication and identity rules; an invalid file does not roll back unrelated successful imports. Empty directories have an explicit empty result. Per-file failures are retained in the CLI report and a virtualized UI details list; partial CLI reports exit nonzero. Imports never authenticate or contact a cluster.

Source watchers, automatic replacement-session creation and default/paste/generated import remain open. Effective connection resolution, confirmed authentication and cluster-request evidence have since advanced independently; their current capabilities and limitations are tracked in [the test map](../spec/k3d-test-map.md), not inferred from import metadata.

Tests run real CLI processes and local filesystem operations, including changed/repeated imports, deleted originals, private permissions, schema and hash failures, isolated corruption, duplicates, plugin non-execution and active-session binding preservation. Results and platform gaps belong in [the test map](../spec/k3d-test-map.md).

References: [Kubernetes kubeconfig v1](https://kubernetes.io/docs/reference/config-api/kubeconfig.v1/), [credential-plugin path semantics](https://kubernetes.io/docs/reference/access-authn-authz/authentication/), [yaml-cpp](https://github.com/jbeder/yaml-cpp/blob/master/docs/Tutorial.md). Distribution and dependency-license evidence remains owned by ADR 0015.

## Migration And Rollback

The native format is not a legacy kubeconfig filename or a legacy-store migration. The preserved legacy runtime remains independently usable with its separate profile. Legacy-to-native import, native-to-legacy recovery, relative-path execution evidence and complete old/new parity must be proved before retirement. Malformed native records remain intact for diagnosis; no fallback reads a mutable original file.

## How are generated K3D sources retained?

The accepted generated-source identity from ADR 0003 also applies to native records: `podlord-generated://k3d/<cluster>`. Only this canonical generated namespace is accepted in owned records; file and pasted-path imports keep their existing path validation. Existing file identities, content hashes and session bindings are unchanged.

Keep the original exported bytes intact. Normalize `0.0.0.0` and `localhost` only in the parsed server URL and effective connection, never by replacing strings in the YAML or credentials. Generated sources are excluded from file refresh; re-export is explicit. They have no original filesystem directory: relative credential/certificate references and explicit relative credential-program paths fail instead of resolving against an accidental working directory. Absolute references remain explicit; confirmed PATH credential commands use the owned snapshot directory and are never executed by import.

The existing asynchronous import owner invokes the selected local k3d executable without a shell. Bound stdout to the existing 16 MiB input budget, discard stderr rather than leaking credential-bearing diagnostics, and bound each local CLI invocation to 30 seconds. This local-process bound does not change Kubernetes request lifetime or turn an integration-test deadline into application cancellation. JSON output, existing name aliases and the reference text-table compatibility path remain supported. Healthy cluster exports survive another cluster's failure. No second source index or session-opening side effect is introduced.
