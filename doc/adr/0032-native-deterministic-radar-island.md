# How does native Radar preserve the deterministic resource island?

Status: accepted for the user-confirmed RTS-style island and focused navigation.

## What owns geometry and camera state?

The full ResourceTable cache supplies cluster, namespace, owner, kind and identity.
The existing filtered proxy supplies resource membership and inspector indices.
RadarIsland derives deterministic geometry using the retained C# cluster centers,
namespace arms, dependency ranks, hash and collision rules. A topology signature
avoids rebuilding it for status/version changes, filters or sorting. Native path
identity seeds the hash; this preserves deterministic topology, not a promise of
pixel-identical placement to C#'s differently encoded resource IDs.

The existing per-session Workspace navigation owns the camera. Replace the old
one-dimensional GridView offset and discrete tile-size APIs, rather than keeping
both implementations. Camera state remains in memory as before; no new profile
schema or persisted resource cache is introduced.

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
