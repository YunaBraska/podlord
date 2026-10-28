# How does native Radar preserve the deterministic resource island?

Status: accepted for the user-confirmed RTS-style island and focused navigation.

## What owns geometry and camera state?

The full ResourceTable cache supplies cluster, namespace, owner, kind and identity.
The existing filtered proxy supplies resource membership and inspector indices.
On 2026-10-28 the user approved clearly separated cluster islands with namespace
districts inside each island, replacing the earlier exact-reference topology.
RadarIsland packs nonoverlapping cluster bounds and rounded namespace districts.
Cached district shorelines group namespaces within each cluster, with water
between clusters. The user rejected the artificial connecting land on 2026-10-28:
these strokes do not represent Kubernetes relationships and are removed.
Painting does not rebuild the shoreline or move tiles.
The existing 90-percent new-logic coverage requirement is enforced over the entire
modified synchronization/layout/painting/color/view-selection function family,
including local functions. This is broader than counting added lines and avoids
mixing unrelated existing water/input handlers into the new Map increment. Missing
owners fail the gate rather than silently reducing its scope.
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
start a request, authenticate or reset filters, drafts or camera position.
The new navigation is approved but not yet implemented. Cluster
names appear at overview scale, namespace district
labels appear when zooming in, and resource glyphs/details retain their existing
zoom and interaction rules. Labels use the current theme, viewport buckets, cached
text bounds and collision decluttering instead of a delegate per cached label.

A context overview is separate from an opened context map. The user follow-up on
2026-10-28 supersedes the earlier RAM-only overview: retain the latest visual
preview across restarts, not a persistent Kubernetes resource store. The ordinary
runtime cache and its TTL have a different purpose and remain unchanged here.
Opening the overview must not open sessions, synchronize contexts or authenticate.
Only visible previews need loading; entries without a preview show a question
mark. Previews are removed with their catalog entry, with startup reconciliation
only after a successful authoritative catalog load. No additional per-cluster age
or connection-status panels are introduced. Tile ownership (context versus file)
and retained namespace labels await clarification before dependent persistence
or navigation is implemented.

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
