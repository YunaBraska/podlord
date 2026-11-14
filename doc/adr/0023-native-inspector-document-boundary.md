# How does the native inspector cache and redact resource documents?

Status: accepted implementation boundary for the confirmed read-only inspector
and Secret presentation contracts. Protected draft ownership follows
[ADR 0008](0008-protect-yaml-editing-and-apply.md); apply and operational writes
remain uncompleted requirements.

## Context

The initial native inspector retained normalized resource metadata only. A YAML
or values surface cannot reconstruct the real resource from that summary. Full
documents can contain credentials, and formatting them on every general UI
notification would reintroduce the work the rewrite is intended to remove.

## Decision

Keep the complete accepted GET document beside its normalized summary in the
existing detail cache. It has the same five-minute lease, originating session,
resource path and current-UID authority defined by
[ADR 0022](0022-native-resource-metadata-authority.md). There is no second cache,
resource disk persistence, credential export, independent refresh loop or new
dependency. The existing queue owns inspector priority, request spacing, backoff,
authentication suspension and sent-read completion.

Validate ConfigMap/Secret value maps, keys, string types and encoded-data validity
at API ingress. Invalid responses preserve the previous accepted document and
produce a generic diagnostic without the response body. The inspector exposes
its own cached-document timestamp, pending read and failure, rather than implying
that a collection timestamp describes the detail document.

Detail response identity includes the requested API version as well as kind,
name and scope. The shared core-value classifier uses API version and kind;
a custom resource named `Secret` in another API must retain its own schema and
YAML instead of being validated or redacted as a core Secret. Inferred list-row
API identity comes from the accepted discovery/request path, including when
individual list items omit their type metadata.
When a list item supplies an API version, it must match that collection. An
inconsistent item rejects the response without replacing the retained collection.

The visible inspector formats metadata, YAML and value rows only when its actual
snapshot, resource scope or explicit reveal selection changes. Getters, painting,
scrolling, hover and page switches read those snapshots and do not fetch data.
Use the installed YAML emitter, quote strings and keys, and preserve JSON scalar
types. Use the platform's fixed-width font for YAML. Refresh captures and restores
the same resource's reading position; changing the scope resets it.

Secret `data` and `stringData` values remain masked in read-only YAML, including
base64 strings. The last-applied annotation is always masked because it can
duplicate the complete Secret. One explicit reveal affects one field/key in
Values only; it never authorizes whole-document disclosure or YAML copy. A separate copy
action copies that individual value without implicitly revealing it. Ordinary
ConfigMap values are directly visible. Valid UTF-8 text is decoded for value
presentation; binary data is explicitly labelled and copied as base64, without
lossy decoding. Copying YAML copies only its visible form, always masked for a
core Secret. Reveal changes rebuild value rows, not the unchanged YAML snapshot.
The field conventions follow the
[Kubernetes Secret contract](https://kubernetes.io/docs/concepts/configuration/secret/#constraints-on-secret-names-and-data).

Revealed presentation is not a per-session preference. Changing resource or
session, closing the inspector/tab, hiding the window or expiring its document
resets it. Returning to identical cached content must still reapply redaction.
The cache retains the original document independently of presentation masks; no
mask is an apply payload or editing baseline.

Closing the inspector removes only unsent detail requests. A sent request finishes
at its original boundary and can update that session's cache, but cannot populate
the closed or different inspector. The shared owner publishes completion after
clearing the running task so that the UI does not retain a false loading state.

## Scope and verification

This boundary provides real read-only YAML, values, explicit individual Secret
reveal/copy and cache/failure/lifecycle behavior. Protected fresh-read editing
and discard guards are separately owned by ADR 0008. The UI explicitly states
that apply remains unavailable. Diff/confirmation, conflict handling, uncertain
write read-back, deletion, complete Secret write safety and legacy parity remain
unimplemented or unproved. Future writes must use the original editing document,
never its redacted presentation.

The existing [test map](../spec/k3d-test-map.md) owns public-QML scenarios and actual
local Kubernetes evidence. Broader platform accessibility, large-document/cache
memory, UI/frame performance, legacy comparison and release gates remain open.
