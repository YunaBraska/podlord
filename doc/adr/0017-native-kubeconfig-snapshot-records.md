# How Does The Native Runtime Retain Imported Kubeconfigs Independently Of Their Original Files?

Status: implementation of the accepted snapshot identity and binding requirements in [ADR 0004](0004-content-addressed-kubeconfig-snapshots.md), within the isolated native profile established by [ADR 0016](0016-native-session-persistence-boundary.md).

## Decision

Each native import is an app-owned, versioned JSON record under the selected profile's `kubeconfigs` directory. Its filename combines SHA-256 hashes of the normalized source origin and original content. The record owns source identity, original bytes and import recency. No second source index is persisted.

Original bytes are stored as canonical Base64, retaining UTF-8 BOMs, unknown fields and credential-plugin configuration without invoking plugins. Base64 is not encryption. On the verified macOS filesystem, new profiles and owned directories use owner-only access, and records use owner read/write permissions. Windows ACL and other-platform evidence remains a release gate, not an assumed property of permission flags.

Reimporting identical path/content refreshes recency in the same record. Changed content or a different source path receives a separate record. Existing session context identities and usage history are not changed by import. Source records retain the original absolute path; future connection resolution must use that directory for relative certificate, token-file and explicit relative plugin paths, not the owned-record directory.

Qt Core owns file access, hashes, JSON, profile validation, locking and atomic publication. The installed yaml-cpp parser owns YAML syntax; handwritten YAML parsing is not introduced. Parsed display metadata is derived from owned bytes and is not persisted as a second authority. CLI output omits tokens, private keys and original YAML; parser errors omit payloads. Ambiguous context/cluster/user names produce warnings and broken-reference markers rather than a usable-looking binding.

Listing reports invalid individual records alongside healthy snapshots without changing any file. An unreadable or invalid directory rejects the scan. Import rejects a malformed existing target rather than silently repairing or replacing it. Lock contention is explicit and does not apply an import.

## Scope And Evidence

Native file import and source listing compose with the session CLI. The same store now owns recursive folder import through `podlord-source import-path` and the Qt Quick Import action. `~` and `~/` expand at the source-path boundary; other user-home notation is rejected explicitly. Scans include regular hidden files, do not follow child file/directory symlinks, and exclude the selected private profile subtree. Selecting the private profile itself as a scan root is rejected. Each valid file uses the existing snapshot publication and identity rules; an invalid file does not roll back unrelated successful imports. Empty directories have an explicit empty result. Per-file failures are retained in the CLI report and a virtualized UI details list; partial CLI reports exit nonzero. Imports never authenticate or contact a cluster.

Opening a management cluster that exposes Cluster API `Cluster` objects also reads each observed `<cluster-name>-kubeconfig` Secret through the normal request scheduler. This is a separate cluster-driven import path. It requires Secret read access and uses a 15-minute per-session cooldown for each observed workload endpoint. A missing Secret leaves manually imported contexts usable. The Secret must have the Cluster API type, `data.value`, one self-contained context and an HTTPS server matching the observed control-plane endpoint. External credential files and executable login providers are rejected. Secret values stay out of resource rows, diagnostics, AI requests and logs.

Accepted managed kubeconfigs use the same owner-only snapshot store and content hash. Their `managed://capi/...` origin is derived from the management source path, context name and Cluster UID, so credential rotation updates the same source. A unique existing context at the same workload endpoint is rebound to the new snapshot, retaining its display name and open-session binding. Ambiguous existing endpoints stop automatic replacement. Managed origins are not reread as local files. No additional credential store or persisted index is introduced.

A successful complete Cluster API list retires managed origins whose Cluster UID and endpoint disappeared. A recreated Cluster at the same endpoint keeps the context until its new Secret can be validated and imported. After a namespace-scoped fallback, only a UID removed from a previously successful list of that same scope can be retired. Failed or incomplete lists never authorize retirement. Retirement closes tabs owned by the managed context, removes its private snapshots and restores any replaced manual context and its tabs. Manually imported kubeconfigs are never removed by Cluster API discovery.

Effective connection resolution, confirmed authentication and cluster-request evidence are tracked in [the test map](../spec/k3d-test-map.md), not inferred from import metadata.

Tests run real CLI processes and local filesystem operations, including changed/repeated imports, deleted originals, private permissions, schema and hash failures, isolated corruption, duplicates, plugin non-execution and active-session binding preservation. Results and platform gaps belong in [the test map](../spec/k3d-test-map.md).

References: [Kubernetes kubeconfig v1](https://kubernetes.io/docs/reference/config-api/kubeconfig.v1/), [Cluster API kubeconfig Secret](https://cluster-api.sigs.k8s.io/developer/core/controllers/cluster.html), [credential-plugin path semantics](https://kubernetes.io/docs/reference/access-authn-authz/authentication/), [yaml-cpp](https://github.com/jbeder/yaml-cpp/blob/master/docs/Tutorial.md). Distribution and dependency-license evidence remains owned by ADR 0015.

## Migration And Rollback

The native format is independent of the kubeconfig filename. Malformed records remain intact for diagnosis; no fallback reads a mutable original file.

## How are historical virtual sources retained without a CLI integration?

The native product imports standard kubeconfigs. It does not discover local test clusters, look up a K3d executable or invoke a generator. Test-cluster generation has no production path.

Previously stored virtual origins remain data under the existing `podlord-generated://<origin>/<name>` namespace. Validate both bounded identity components without dispatching on a generator name. Preserve record bytes, hashes, context identifiers, aliases and session bindings. These records are not watched or refreshed as filesystem paths. Keeping their decoder is data compatibility, not continued CLI support.

For those historical virtual records only, retain their parsed local-endpoint normalization and lack of a filesystem base. Relative credential/certificate files and explicit relative credential programs still fail. Ordinary file imports keep their declared endpoint unchanged and resolve relative paths against their original directory. Reading either format never runs authentication. No new source index, resource cache or migration rewrite is introduced.
