# How does native Radar preserve the deterministic resource island?

Status: accepted for the user-confirmed RTS-style island and focused navigation.

## What owns geometry and camera state?

The full ResourceTable cache supplies cluster, namespace, owner, kind and identity.
The existing filtered proxy supplies resource membership and inspector indices.
On 2026-10-28 the user approved clearly separated cluster islands with namespace
districts inside each island, replacing the earlier exact-reference topology.
RadarIsland packs nonoverlapping cluster bounds and namespace districts using
the original deterministic Radar arm/depth placement inside each district.
There are no circular placeholder land fills or artificial district connectors.
These would invent geography or relationships absent from Kubernetes. Water
separates clusters. Painting does not rebuild geometry or move tiles.
The existing 90-percent new-logic coverage requirement is enforced over the entire
modified synchronization/layout/painting/color/view-selection function family,
map navigation, visual preview storage and capture, including local functions.
The obsolete menu-only setter is replaced by the actual workspace-tab owner.
This is broader than counting added lines and avoids mixing unrelated existing
water/input handlers into the new Map increment. Missing owners fail the gate.
Cluster-scoped resources occupy a separate district rather than an invented
namespace. Actual Namespace resources remain selectable at their district anchor.
The seven-unit lattice, kind-dependent terrain colors and identity-seeded placement
remain deterministic. A topology signature avoids rebuilding geometry for
status/version changes, filters or sorting. The same resource set published in a
different order produces the same positions. Kubernetes UID remains an identity
input, including when an object is replaced at the same API path.

All discovered listable kinds participate in the same layout. When native
discovery includes kinds absent from C#'s fixed catalog, common resource tiles
may move as the island is redistributed. A frozen reference-only skeleton or
separate layout for additional kinds would contradict the confirmed product
behavior; parity comparisons must use the same resource set.

The existing per-session Workspace navigation owns the camera. Replace the old
one-dimensional GridView offset and discrete tile-size APIs, rather than keeping
both implementations. Camera state remains in memory as before; no new profile
schema or persisted resource cache is introduced.

## How do Table and Map share the existing workspace?

The existing menu-only Table/Map implementation is interim. The user follow-up
requires Map as the first and default primary workspace tab, with backward and
forward navigation. Resources, Events and Ports remain available and restore the
small Radar for the active context; only Map hides it. Opening a context from the
overview reuses the ordinary context-opening flow, with no parallel loader or
authentication path. The context map shares the existing renderer, filtered
source, camera, selection, tooltip and alarm owner. Tab switching itself does not
start a request, authenticate or reset filters, drafts or camera position for an
already loaded session. Restored tabs are metadata-only until the first explicit
resource-facing action, using the same normal loader. Map history reuses existing
session activation and dirty-YAML Stay/Discard, not a second session catalog.
Cluster
names appear at overview scale, namespace district
labels appear when zooming in, and resource glyphs/details retain their existing
zoom and interaction rules. Labels use the current theme, viewport buckets, cached
text bounds and collision decluttering instead of a delegate per cached label.

A context overview is separate from an opened context map. The user follow-up on
2026-10-28 supersedes the earlier RAM-only overview: retain the latest visual
preview across restarts, not a persistent Kubernetes resource store. The ordinary
runtime cache retains resource collections for a configurable positive number of
whole hours, default 24. Sync settings capture its estimated payload size. This
does not claim to measure heap allocation or change other cache lifetimes.
Opening the overview must not open sessions, synchronize contexts or authenticate.
Only visible previews need loading; entries without a preview show a question
mark. Previews are removed with their catalog entry, with startup reconciliation
only after a successful authoritative catalog load. No additional per-cluster age
or connection-status panels are introduced. Only the current imported snapshot
per normalized source origin supplies cards. Current membership is derived from
the existing import-recency ordering before removed contexts are filtered, so
removing a current cohort cannot resurrect historical cards. Older snapshots
remain available to saved sessions. No new persisted current-version index or
source-record format is introduced. Explicit reimport can select an older snapshot
again through ordinary import recency. Partial catalog failure permits readable
versions with a visible older-version warning; whole-catalog failure preserves
the last readable cards with that warning. A complete successful reload clears it.
Each immutable context ID owns one latest private 320 x 200 PNG, at most 512 KiB.
Its caption uses the
existing local alias and referenced cluster name outside the image. Multiple
contexts referencing one cluster remain separate. PNGs contain no text or
resource documents. A profile-owned background writer coalesces pending images,
uses atomic replacement and drains accepted work on shutdown. Failed updates and
cache expiry do not create replacement previews. Context removal serializes
preview deletion after pending saves. Rendering reads cached URLs only, and the
virtualized grid decodes visible images without contacting clusters.
Short landscape viewports put the same preview beside the caption and rename
action rather than clipping the caption or removing the image. This uses one
responsive card layout, not a separate mobile implementation.

## Why use a sparse native surface with visible resource delegates?

One painted surface draws cluster/namespace context markers. Coarse
spatial buckets select visible resources; a small list model publishes only those
identities to the existing QML glyph, tooltip, accessibility and alarm delegates.
Viewport changes do not enumerate the complete resource cache or regenerate its
geometry. Hidden Radar releases delegates and defers geometry work.
Painting is read-only. Alarm evaluation and expiry retain ADR 0025's single owner.

A dense TableView would instantiate delegates for empty water cells, especially
at the retained 55% zoom. A whole-cache Repeater would keep offscreen UI work
alive. A second custom glyph/effect implementation would duplicate behavior.
All three were rejected. Cluster and namespace markers are non-interactive
context derived from observed groups, not synthetic Kubernetes resources.

The C# reference paints solid resource tiles, not filled corridors between them.
Remove native corridor cells and their index rather than retaining invented land.
The retained terrain palette distinguishes shallow network water and deep Event
water. Glyphs appear only at a tile width of at least 9 pixels, with dark fill and
terrain/alert-colored strokes; smaller tiles remain inexpensive solid marks.

The user clarified that only background water is required, not Event waves.
One independent painted water surface follows the C# 18-pixel pattern, drift,
translucent colors and activity/speed interval. Its bounded request timestamps
come from the existing scheduler admission signal, never from a new request.
Event tiles retain their terrain/alert colors without local water effects.
The background has no resource-model binding, resource traversal or per-Event
animation. Event filtering therefore does not cause background repaint work.

Water settings reuse the authoritative ReadSettingsStore (version 5); versions
1 through 4 load without rewriting, with enabled/45% defaults. Saving upgrades
atomically and retains existing conflict detection. Older executables reject
version 5 explicitly; rollback requires retaining the previous profile file.
There is no new settings file or resource persistence.

The water clock stops for hidden/minimized/detached surfaces, reduced motion,
disabled water, zero speed and active dragging. Camera changes defer its next
phase for at least 200 ms. Frozen water retains its current pattern. Painting
does not mutate the cache, fetch, save or rebuild geometry. Alarm motion keeps
its existing independent lifecycle because it communicates a different state.

## What must verification establish?

Real QML input must prove focus-scoped keyboard movement, drag versus click,
mouse-wheel and trackpad pointer-anchored zoom, reset, per-session retention,
stable filtered positions and cache-only behavior. Existing glyph, tooltip,
highlight, animation, reduced-motion and hidden-work checks remain applicable.
The test map owns actual results, real local Kubernetes images and outstanding
visual/performance gaps. This decision is not a release or whole-product parity claim.

Keyboard focus retains a resource path, not a proxy row number. Filtering out
that identity makes Enter unavailable until the user explicitly selects another
resource. Sorting can change its proxy index without changing the identity or
world coordinate. Inspector selection remains owned by Workspace.
