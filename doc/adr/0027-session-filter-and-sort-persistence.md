# Where do context filters and sort order live?

Status: accepted. Context ownership and the positive catalog were confirmed on 2026-11-13.
Execution evidence belongs in [the test map](../spec/k3d-test-map.md).

## Decision

There is no user-facing saved-session catalog. Imported contexts are the durable
objects. Actual tabs reference a context and own their inspector selection,
camera, drafts, terminals, port-forwards and window placement. Context aliases
come from the source catalog, not independently named tabs.

Filters and sorting are shared immediately by every tab and window displaying
the same context. Different contexts remain independent. Resource, event and
connection filters, resource Problems/Activity mode and display limit, and the
six supported table sorts use stable model column IDs. Column layout remains
global per table type under ADR 0010. Named filter presets remain a separate,
profile-wide collection of reusable predicates, not saved sessions.

One versioned record per canonical context ID lives at
`views/context-<context digest>.json` in the private profile. An alias does not
change its identity or filename. Missing records mean empty filters, NONE sorting
and the current default resource limit. Version 7 stores the namespace request scope on the resource record.
Other tables cannot carry this scope.

The application runtime owns one cached context state and one asynchronous
writer. Windows do not coordinate independent copies or competing writers.
Explicit changes update cached presentation immediately. The writer serializes
saves and coalesces unsent changes by context and table. Each save locks the
record, checks the expected table value, merges unrelated tables and atomically
publishes the private file. Conflicting, unsupported, malformed, oversized,
inaccessible and symlinked input fails explicitly. It is never reset silently.

Closing a tab removes its live-tab record, stops only its connections and retains
context preferences. Moving a tab preserves its identity and streams. Closing a
window does not destroy the application-owned writer. Normal shutdown drains
pending work, and save failure retains the existing explicit close decision.
Removing a context deletes its current preference record and prevents a late
writer from recreating it. It does not alter original kubeconfig files.

The runtime also owns an in-memory catalog revision. Successful source imports,
edits and removals advance it. A loader completion from an older revision is
discarded and rereads the local catalog before publishing. This prevents a
different window's delayed read from restoring removed cards or usage. It does
not introduce persisted deletion state, another catalog or a network retry.

## One context authority, no native-format migration

The context overview ranks explicit uses by count, then most recent use. Only
those two values are stored per context in the existing positive catalog. There
is no per-use history, saved-session adoption or additional index. An absent use
record means never used. Original source files and credentials are untouched.
Each window snapshots its card order when opening the overview. Uses elsewhere
do not move visible cards. Newly imported cards may be added and removed cards
disappear without reordering the remaining cards. Ties retain source/context
order. Actual tab placement remains independent of this ranking.

The application runtime serializes and coalesces pending increments off the UI
thread. Atomic publication uses the existing source lock and checks positive
membership. Removal also prunes usage, so a pending save cannot restore a
context. Shutdown drains owned writes. A failed write preserves existing bytes
and reports the error without blocking context access.

The private `kubeconfigs/catalog.json` is the sole membership authority. It
contains source snapshot IDs and their available context IDs. Source payloads
remain immutable data files, not a directory to rediscover. Unlisted files must
not reintroduce a context. Empty source entries retain current-version ordering,
not removed context identities. Tab records are references, not membership or
authorization. A retained readable overview can still open a tab during a source
read failure, but cannot grant new credentials or bypass source validation. An explicit import may register those contexts
again. It does not restore deleted tabs or connections.

There is no removed-context list. Removal holds the existing source, preference
and tab locks, validates the confirmed tab inventory, and atomically removes
membership before cleaning up dependent state. A cleanup failure does not grant
access again. A late preference save checks positive membership under the
preference lock. Credentials and source YAML are resolved through that catalog.

Native pre-release data is not migrated. Existing source copies and credentials
remain on disk, but contexts must be explicitly imported into the new catalog. Current actual tabs live in
`tabs.json`, containing only IDs and context references. Old `sessions.json`
and UUID preference files are neither used by workspace restoration nor deleted.
There is no backup/adoption loop or winner selection between old C++ sessions.
Current context settings accept their current format only. Migration from the
C# product remains separately scoped. No startup operation connects or
authenticates merely to construct this local catalog.

## Compatibility and verification

Current context records describe all six actual table schemas.
Resource display limits cap only the sorted table projection. Radar, counts,
pickers and alarms keep their complete authoritative sets.

Explicit named-filter import still accepts the bounded reference array or native
preset record, preserves source bytes and protects the empty default. Import
rejects conflicting names rather than replacing existing filters. Selecting a
preset cannot change namespace request scope, fetch resources or restore streams.

Public migration tests exercise winner selection, backup contents, repeat use,
namespace scope, locks, malformed input, close/reopen and optimistic conflicts.
A real HTTP-backed two-window regression verifies immediate shared filtering and
sorting. UI, restart, terminal, forwarding, package and performance evidence must
be reviewed separately. These checks alone are not release acceptance.

## What happens to cached resources when a tab is closed?

Closing a tab still stops its terminals and port forwards. It does not extend the
existing collection-cache TTL or turn the tab into a saved session. A later tab
for the same context may take over a closed, quiescent client's existing cache.
The context ID, server, credential identity and namespace access scope must match.
The transfer moves the existing state rather than copying a resource inventory.
Already-sent requests retain their original owner and are never moved or aborted.
A non-quiescent state is therefore not eligible for transfer. Expiration still
runs before the transferred snapshot is published under the new tab ID. Inspector
selection, camera and drafts are not restored from the closed tab.

Public resource-cache and workspace regressions cover reuse, identity isolation
and expiry. The UI quick-open and footer regressions additionally require no new
API request for a fresh retained cache.
