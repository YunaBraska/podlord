# 0006 Cache-Preserved Source Switching And Localization

## Status

Accepted

## Context

Switching kubeconfig sources should not feel blank or blocked when Podlord already has cached data for that source. At the same time, stale cache must not be treated as fresh live state forever.

Podlord also needs first-class internationalisation support without changing Kubernetes object names, user-provided labels, or cluster data.

## Decision

Keep list cache displayable for up to 24 hours per session/source. Data older than the normal fresh TTL is rendered with stale freshness, while data older than the display TTL is hidden. On source switch, the view model restores any available cache immediately, shows loading feedback for cold sources, and queues a background refresh. If a refresh completes after the user has switched away, its result is not rendered over the current source.

Store the selected UI language in app settings using a `system` default. Add a localizer catalog with 20 selectable UI languages and English fallback. Translate application chrome, settings, filters, source feedback, and empty/loading states first. Kubernetes resource names and cluster-provided strings remain unchanged.

## Consequences

- Source switching can be instant when cache exists.
- Cold sources show explicit loading state instead of silent waiting.
- Old but useful cache remains visible for operator context, marked stale by existing freshness metadata.
- Localization can expand key-by-key without changing cluster data or persistence shape again.
- Table headers and deeper inspector strings still need follow-up localization passes.

## How Does The Native Runtime Retain The Reference Catalog?

The native compiled catalog is exported through the retained C# localizer's public
entrypoint. The real inventory is twenty translated languages plus English and
the system selection. English fallback is exported once; language-specific
differences are merged into immutable presentation maps. Native startup and
language changes do not load .NET, fetch cluster data, change Kubernetes values,
or run a translation timer. The reference CI regenerates and compares the shipped
catalog, so its source cannot silently diverge.

The existing private settings owner stores the selected canonical code. Reading
versions 1 through 6 supplies `system` without rewriting; the next explicit save
atomically writes version 7 with unchanged read, appearance, water and restoration
settings. Invalid language data and concurrent writers retain the old document.
Rollback uses a preserved profile or a compatible binary, not deletion of the
new field. Navigation, sidebar filter modes, settings section names and resource
empty/loading chrome are the first migrated surfaces. Other inspector/help/action
strings remain explicit localization work, not an implied whole-product pass.
