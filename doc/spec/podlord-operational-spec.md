# Podlord Operational Spec

## Purpose

Podlord is a desktop Kubernetes operations console for people who need to understand cluster state quickly without trusting shell context, stale views, or namespace-first navigation.

It turns Kubernetes from scattered YAML and terminal state into one cache-backed, multi-session control surface.

## Problems Solved

- Wrong-context risk: every view, tab, radar, action, and port forward is bound to an explicit source/session.
- Slow navigation: resources are shown across the selected scope first, then filtered down.
- Stale UI state: Kubernetes data is cached, refreshed in the background, and marked by sync state.
- Tool dependency: core resource loading, metrics, YAML, secrets, and port-forwarding use Kubernetes APIs instead of requiring `kubectl`.
- Operational noise: filters, alerts, radar colors, sounds, and animations surface important changes without alerting on every normal update.

## Core Capabilities

- Import kubeconfigs from files, folders, pasted YAML, default locations, and generated k3d sources.
- Store app-owned kubeconfig snapshots with content hashes to avoid duplicates.
- Detect changed kubeconfig files and create new snapshots while preserving existing session bindings.
- Open sessions in tabs or detached windows while sharing the same cache.
- Keep each session open only once across all tabs and windows.
- List Kubernetes resources in a flat, sortable, filterable table.
- Filter by kind, namespace, status, name, image, node, owner, age, restarts, CPU, memory, storage, problems, and activity.
- Inspect resources with overview data, YAML, events, links, logs, values, and actions.
- Reveal ConfigMap and Secret key/value data with secret values hidden by default.
- Edit and apply YAML through Kubernetes API paths.
- Start native Kubernetes port-forwards for supported running Pods and Services.
- Show live API request diagnostics, audit rows, cache telemetry, and process memory diagnostics.
- Render a deterministic radar map of resources and relationships.
- Apply rule-based alerts to radar and resource rows.
- Play local bundled alert sounds without opening a browser.

## Operational Model

Podlord is cache-first.

The UI reads local snapshots. Background sync fills and refreshes the cache through a rate-limited request queue. User-focused actions can request fresher data, but still respect throttling and backoff.

Each source/session has independent request telemetry and selection state. Switching sessions should be instant when cache exists, and visible when data is still loading.

## Specification Interview Progress

Session lifecycle, naming, usage ordering, port-forward ownership, pod logs, and most inspector/YAML behavior have confirmed requirements below. Implementation conformance remains unreviewed.

The remaining interview uses `Question x/y` numbering, initially estimated at 12 topic blocks and expanded to 14 on 2026-10-02 after the requested C++ rewrite introduced platform and framework/distribution decisions. This is not a retrospective count of earlier questions. Group related decisions; do not turn every acceptance case into another interview question. If an answer adds or removes a necessary block, explain the changed total. Proposed behavior remains unconfirmed until explicitly accepted.

| Question | Remaining topic | Status |
| --- | --- | --- |
| 1/14 | YAML apply failures, uncertain outcomes, and retries | Confirmed; INS-023 through INS-028 |
| 2/14 | Secrets and sensitive-data presentation/copying | Confirmed; SEC-001 through SEC-008 |
| 3/14 | Supported resource actions and destructive-action safeguards | Delete safeguards confirmed; ACT-001 through ACT-010. Supported-action inventory remains for scope reconciliation. |
| 4/14 | Shared table behavior: sorting, columns, colors, copying, and overflow | Confirmed; TBL-001 through TBL-012 and INS-001 through INS-003 |
| 5/14 | Filters, radar scope, and state isolation when switching tabs | Confirmed; FLT-001 through FLT-010 |
| 6/14 | CPU, memory, and storage presentation, including missing metrics | Confirmed; MET-001 through MET-011 |
| 7/14 | Background synchronization, cache freshness, and request priority | Confirmed; SYN-001 through SYN-013 |
| 8/14 | Connection, authorization, rate-limit, and recovery feedback | Confirmed; ERR-001 through ERR-014, including user-confirmed authentication retry |
| 9/14 | Startup, workspace restoration, and settings persistence | Confirmed; STR-001 through STR-010, SYN-001, and ERR-013 through ERR-014 |
| 10/14 | Keyboard operation, focus, and accessible feedback | Confirmed; ACC-001 through ACC-009 |
| 11/14 | Measurable responsiveness and resource-use acceptance criteria | Confirmed and tightened by question 13; PER-001 through PER-013. Infrastructure latency does not authorize request cancellation. |
| 12/14 | Required device/platform coverage for the C++ rewrite | Confirmed; RWT-004 through RWT-008. Qt accepted; browser application excluded. |
| 13/14 | UI/framework and distribution constraints; tightened cached interaction budget | Confirmed; Qt Quick with thin QML presentation, RWT-006, and PER-001 through PER-013. Module/distribution evidence remains required. |
| 14/14 | Final scope/gap reconciliation and the rewrite/test review plan | Confirmed; RWT-009 through RWT-015 require independently runnable old/new comparison and evidenced parity before retirement. |

Questions 1 through 3 were confirmed on 2026-10-01; questions 4 through 14 were confirmed on 2026-10-02. This interview round is complete. Their owning requirements are referenced in the table above. Question 13 confirms Qt Quick with thin QML presentation and the tightened performance contract. Question 14 confirms the rewrite sequence and adds continuous old/new comparability as a retirement gate. Concrete module/distribution obligations, exact tested OS/architecture support, supported-action inventory, and outstanding behavioral details remain explicit reconciliation/evidence work, not assumed passes. Implementation sequencing is owned by [the roadmap](../roadmap.md); comparison scenarios and evidence are tracked in [the existing test map](k3d-test-map.md).

## Confirmed Source And Session Requirements

These requirements were confirmed on 2026-09-30. Confirmation establishes required behavior; implementation conformance has not yet been reviewed. Snapshot rationale is owned by [ADR 0004](../adr/0004-content-addressed-kubeconfig-snapshots.md).

| ID | Required observable behavior |
|---|---|
| SES-001 | When an imported kubeconfig file changes, Podlord MUST preserve the previously imported snapshot and import the changed content as a new snapshot. |
| SES-002 | An existing session MUST remain bound to its original snapshot until the user explicitly chooses a replacement session. A source-file change MUST NOT silently change its API endpoint or authentication configuration. |
| SES-003 | Re-importing unchanged content from the same source path MUST NOT create duplicate snapshots or sessions. |
| SES-004 | Session selection MUST prioritize frequently used sessions so obsolete, rarely used sessions move out of the prominent part of the list. This ordering MUST NOT delete sessions or their snapshots. |
| SES-005 | When changed source configuration produces a replacement session, the replacement MUST inherit its predecessor's base display name with a numeric suffix, whether that base name was assigned automatically or by the user. Numbering follows SES-007, SES-017, and SES-018. |
| SES-006 | Creating a replacement session MUST preserve the names of all existing sessions. |
| SES-007 | Successive replacements MUST use numeric suffixes starting at 2: `Production`, `Production 2`, `Production 3`, or `dev`, `dev 2`, `dev 3`. The suffix MUST NOT accumulate into names such as `Production 2 2`. Deletion and name collisions follow SES-017 and SES-018. |
| SES-008 | Explicitly opening a session or switching to it MUST count as usage. Background synchronization MUST NOT count as usage. |
| SES-009 | Session selection MUST sort by usage count within the preceding 30 days, highest count first. Older usage MUST NOT contribute to that count. |
| SES-010 | Sessions with equal usage counts in the 30-day window MUST be ordered by their most recent usage, most recent first. |
| SES-011 | A replacement session created from changed configuration MUST start with its own usage history and MUST NOT inherit usage from its predecessor. Creating the replacement MUST preserve the predecessor's history. Explicitly opening or switching to the replacement counts under SES-008. |
| SES-012 | Activating an already active session MUST NOT increment its usage count or update its last-use timestamp. |
| SES-013 | Sessions with no recorded usage MUST appear after sessions with recorded usage. Among never-used sessions, newer sessions MUST appear first. |
| SES-014 | Sessions with equal usage count and last-use timestamp, or equal creation timestamps among never-used sessions, MUST use session ID ascending as the final stable ordering key. |
| SES-015 | Usage ordering MUST apply to the session-selection list. Changes in usage rank MUST NOT reorder already open session tabs. |
| SES-016 | Switching sessions or synchronizing in the background MUST preserve the order of already open session tabs. |
| SES-017 | Within the same replacement-session sequence, the next generated suffix MUST be one greater than the highest suffix among remaining sessions, starting at 2 when no numbered replacement remains. Gaps below the highest remaining suffix MUST NOT be filled. A deleted suffix MAY be reused when no higher-numbered successor remains, subject to SES-018. |
| SES-018 | When the next generated replacement name is already occupied by an existing session, Podlord MUST skip it and advance to the next available number without renaming the existing session. |
| SES-019 | When the user manually renames a numbered replacement, the assigned name MUST become its new base display name and begin a separate naming sequence for future replacements. Renaming `Production 2` to `Payments` MUST cause the next replacement to be named `Payments 2`, subject to SES-017 and SES-018. Existing sessions MUST retain their names. |
| SES-020 | Every port-forward MUST belong to its owning session tab. Closing that tab MUST stop all of its port-forwards and release their local listening ports. |
| SES-021 | Closing one session tab MUST NOT stop port-forwards belonging to another open session tab. |
| SES-022 | Closing a session tab MUST preserve its cached data subject to the existing cache TTL. Closing the tab MUST NOT extend or reset that TTL. |
| SES-023 | Moving a session tab to a detached window MUST preserve the same session and its running port-forwards. Moving the tab MUST NOT stop or recreate those forwards. |
| SES-024 | Closing a detached window that owns a session MUST stop that session's port-forwards and release their local listening ports. Port-forwards in other open tabs or windows MUST remain unaffected. |
| SES-025 | Switching between open session tabs MUST preserve their running port-forwards. A tab switch MUST NOT stop or recreate those forwards. |
| SES-026 | While a log view is not visible, Podlord MUST pause polling for that view. Switching away from its session tab MUST pause its log polling. |
| SES-027 | When returning to a visible log view, Podlord MUST show available cached logs immediately and resume their refresh unless the user has paused that view. Cache availability remains subject to the existing TTL rules. |

### What is currently proven for native session-owned port forwards?

The native Pod/Service forwarding increment follows SES-020, SES-021 and
SES-025 through real QML controls, the existing request owner and actual local
TCP traffic. Pod and named-Service forwarding, stopping and port reuse also
pass against an owned local Kubernetes cluster. [ADR 0030](../adr/0030-native-port-forward-transport-and-session-ownership.md)
owns transport/lifecycle decisions; [the test map](k3d-test-map.md#what-proves-native-pod-and-service-port-forwarding)
owns execution evidence and remaining gaps. Native detached windows now share
the same transport owner and preserve active shells and forward endpoints.
The owned real Kubernetes window-transfer lane verifies SES-023/024 with two
independent sessions, live TCP traffic and a retained remote shell variable.
[ADR 0044](../adr/0044-shared-native-window-ownership.md) owns window placement;
[the window test map](k3d-test-map.md#what-proves-native-window-ownership-and-transfer)
records public UI, close-failure and shared-preference coverage. Complete upgrade
error classification, installed-app task-table parity and physical multiwindow
presentation remain open. These scoped results do not establish full migration
or release readiness.

### How Must The Native Ports Table Behave?

Ports uses the shared virtualized table and column editor: seven model-owned
columns for local endpoint, name, kind, namespace, requested port, resolved port
and status. Headers cycle typed ASC/DESC/NONE sorting; visibility, order, widths
and pinning persist per table type. Whole-row click or Enter opens the existing
inspector. Context-menu and keyboard copy use the visible cell value; truncated
values use the existing plain-text popup. Kind, namespace and status use the
same theme/identity color owner as Resources, never a second color algorithm.

Search and sorting remain independent per open session and cannot stop its
forwards. Column layouts are global per table type. Endpoint copy, explicit
HTTP/HTTPS opening and stopping remain available in the toolbar and context
menu; stale or foreign-session tokens cannot operate another session's forward.
Filtering, sorting, copying, hovering and column editing make no Kubernetes
requests. Explicit inspection retains the accepted cache-first detail contract.
Table snapshots identify each row by its forward token, not the resource path.
Loading older valid layouts is read-only; the next explicit save upgrades the
layout document to version 4 without discarding Resources/Events preferences.

### Acceptance And Evidence

| Requirement | Public scenario | Expected result | Evidence status |
|---|---|---|---|
| SES-001, SES-002 | Open a session, change its source kubeconfig endpoint, and refresh the source. | A new snapshot becomes available; the open session continues to address its original endpoint. | Planned check; implementation review pending. |
| SES-002 | Explicitly open the session associated with the replacement snapshot. | Operations in that session use the replacement configuration. | Planned check; implementation review pending. |
| SES-003 | Import the same file twice without changing its content. | The second import creates no duplicate snapshot or session. | Planned check; implementation review pending. |
| SES-004, SES-008, SES-009 | Repeatedly open or switch to one session while leaving another unused, then open session selection. | The frequently used session is prioritized; the unused session remains accessible. | Planned check; implementation review pending. |
| SES-008 | Synchronize an inactive session in the background without opening or switching to it. | Background synchronization does not improve its usage rank. | Planned check; implementation review pending. |
| SES-009 | Compare a session with many uses older than 30 days and none within the window against a session used within the window. | The recently used session ranks above the historically frequent session. | Planned check; implementation review pending. |
| SES-010 | Use two sessions equally often within the window, with one used more recently. | The more recently used session ranks first. | Planned check; implementation review pending. |
| SES-011 | Create `Production 2` from a frequently used `Production` session without opening the replacement. | The replacement has no usage; the predecessor retains its history and can initially rank higher. | Planned check; implementation review pending. |
| SES-008, SES-011 | Explicitly open the replacement for the first time. | Its own usage count increases without changing the predecessor's history. | Planned check; implementation review pending. |
| SES-012 | Repeatedly activate the already active session, then open session selection. | Its usage count and last-use timestamp remain unchanged; those activations do not improve its rank. | Planned check; implementation review pending. |
| SES-013 | Compare previously used sessions and two never-used sessions with different creation timestamps. | Previously used sessions appear first; the newer never-used session precedes the older one. | Planned check; implementation review pending. |
| SES-014 | Reopen session selection when all preceding ordering keys are equal. | The tied sessions retain a deterministic order by session ID ascending. | Planned check; implementation review pending. |
| SES-015, SES-016 | Open multiple session tabs, switch between them until usage ranks change, and synchronize in the background. | Session selection follows the usage rules; already open tabs retain their order. | Planned check; implementation review pending. |
| SES-017 | Retain replacements numbered 2, 4, 5, and 6, then import another changed configuration in the same sequence. | The new replacement uses suffix 7; the gap at 3 remains. | Planned check; implementation review pending. |
| SES-017 | Retain only replacements numbered 2 and 4, then import another changed configuration in the same sequence. | The new replacement uses suffix 5; the gap at 3 remains. | Planned check; implementation review pending. |
| SES-017 | Create replacements through `Production 3`, delete `Production 3`, then import another changed configuration in the same sequence. | The new replacement is `Production 3`; the deleted highest suffix is reused because no higher-numbered successor remains. | Planned check; implementation review pending. |
| SES-017 | Delete all numbered replacements while retaining the original session, then import another changed configuration. | Numbering restarts at 2, subject to occupied-name handling in SES-018. | Planned check; implementation review pending. |
| SES-018 | Reach `Production 3` while another existing session is named `Production 4`, then import another changed configuration. | The replacement is `Production 5`; the existing `Production 4` keeps its name. | Planned check; implementation review pending. |
| SES-019 | Rename `Production 2` to `Payments`, change the source configuration, and refresh the source. | The replacement is `Payments 2`; `Payments` and earlier `Production` sessions retain their names. | Planned check; implementation review pending. |
| SES-020 | Start multiple port-forwards in a session tab, then close that tab. | All of its forwards stop; their local ports no longer accept forwarded connections and are available for reuse. | Planned check; implementation review pending. |
| SES-020, SES-021 | Start a forward in each of two session tabs, then close only one tab. | The closed tab's forward stops; the other tab's forward continues to serve connections. | Planned check; implementation review pending. |
| SES-022 | Warm a session cache, close its tab, and reopen it before cache expiry. | The cached data remains available according to the existing freshness and display TTL rules. | Planned check; implementation review pending. |
| SES-022 | Close and reopen a session tab after its existing cache TTL expires. | Reopening does not revive expired cache data or renew its lifetime merely because the tab was closed. | Planned check; implementation review pending. |
| SES-023 | Start a port-forward in a session tab, move that tab to a detached window, and use the same local endpoint. | The forward remains running with the same local endpoint; moving the tab does not stop or recreate it. | Passing native `window.forward` Basic/Fusion and owned real Kubernetes `window-host-test real_transfer`; physical installed-app presentation remains open. |
| SES-021, SES-024 | Run forwards in a detached session window and another open session, then close the detached window. | Only the closed window's session forwards stop and release their local ports; the other session's forwards continue. | Passing native `window.close_isolation` Basic/Fusion and owned real Kubernetes `real_transfer`, including released-port bind and other-session HTTP traffic. |
| SES-025 | Start forwards in two session tabs and switch between the tabs without closing either. | Both forwards remain running at their existing local endpoints. | Planned check; implementation review pending. |
| SES-026 | Open a polling log view, then switch to another session tab or hide the log view. | No further polling requests are initiated for the hidden view while it remains hidden. | Planned check; implementation review pending. |
| SES-027 | Return to a log view with available cached logs while the next refresh is delayed. | Cached logs appear immediately, before the refresh returns; polling resumes while the view is visible. | Planned check; implementation review pending. |
| SES-005, SES-006 | Rename a session to `Production`, change its source configuration, and refresh the source. | The replacement is named `Production 2`; the original remains `Production`. | Planned check; implementation review pending. |
| SES-005, SES-006, SES-007 | Keep an automatically assigned name `dev`, change its source configuration, and refresh the source. | The replacement is named `dev 2`; the original remains `dev`. A further changed import produces `dev 3` while both predecessors remain. | Planned check; implementation review pending. |
| SES-006, SES-007 | Change the configuration again after creating `Production 2` and refresh the source. | The next replacement is `Production 3`; the earlier names remain unchanged. | Planned check; implementation review pending. |
| SES-003, SES-007 | Refresh the unchanged configuration after creating `Production 2`. | No replacement or additional numeric suffix is created. | Planned check; implementation review pending. |

### Open Decisions

Remaining specification decisions are tracked in the Specification Interview Progress section above.

## Confirmed Pod Log Requirements

These requirements are confirmed product behavior. Implementation conformance has not yet been reviewed.

| ID | Required observable behavior |
|---|---|
| LOG-001 | For a Pod with one container, opening logs MUST load that container's logs directly. |
| LOG-002 | For a Pod with multiple containers and no retained selection, the container selector MUST default to `all` and show logs from all of that Pod's containers. The selector MUST also allow choosing one individual container. |
| LOG-003 | Podlord MUST retain the selected container or `all` separately for each Pod and session. Opening another Pod or session MUST NOT overwrite that selection. |
| LOG-004 | Each container log request MUST explicitly identify the actual container being requested. `all` is a selector option for the set of containers, not a literal container name sent to the API. |
| LOG-005 | With `all` selected, Podlord MUST present the containers' log entries in one combined view ordered chronologically by their log timestamps. |
| LOG-006 | Each entry in the combined `all` view MUST visibly identify its originating container. |
| LOG-007 | When some containers fail to return logs with `all` selected, Podlord MUST continue showing and refreshing logs from the containers that remain available. A partial failure MUST NOT replace the entire log view with an error. |
| LOG-008 | A container log failure in the combined `all` view MUST be visible with the name of the affected container. |
| LOG-009 | While the log view is visible and not paused by the user, automatic refresh MUST normally run every 3 seconds. Automatic refresh cycles MUST NOT start more frequently than once every 3 seconds; an ongoing refresh or API backoff may lengthen the interval. |
| LOG-010 | Each refresh with `all` selected MUST request logs separately for the actual containers in that selection. |
| LOG-011 | A log view MUST NOT start another automatic refresh cycle while its previous cycle is still running. |
| LOG-012 | Automatic log refresh MUST respect API backoff, extending the refresh interval while backoff applies. |
| LOG-013 | Opening logs or changing the selected container MUST immediately display available cached logs for the selected Pod, session, and container selection. |
| LOG-014 | Opening logs or changing the selected container MUST request fresh logs with foreground priority without waiting for the next automatic 3-second refresh tick. |
| LOG-015 | Foreground log refresh MUST continue to respect request limits and API backoff. Foreground priority MUST NOT bypass those constraints. |
| LOG-016 | Pausing the log view MUST stop automatic log requests and keep the displayed logs unchanged by background updates. |
| LOG-017 | Resuming a visible paused log view MUST immediately request fresh logs with foreground priority, subject to LOG-015, then resume the automatic 3-second cadence. |
| LOG-018 | While the user is at the end of the log, the view MUST automatically follow newly arriving entries. |
| LOG-019 | When the user scrolls up to read earlier log entries, refresh MUST preserve the reading position while the entry being read remains retained and MUST NOT force the view back to the end. Scrolling up MUST NOT pause log requests. If size-limit eviction removes that entry, LOG-028 through LOG-030 apply. |
| LOG-020 | The log view MUST provide a visible `Live follow` control that returns to the end and resumes following new entries. |
| LOG-021 | Podlord MUST provide a user-configurable retained-log size limit in Settings, expressed in MB, with a default of 5 MB. |
| LOG-022 | The configured size limit MUST apply separately to each Pod log view. With `all` selected, all containers MUST share that view's size budget. |
| LOG-023 | When a Pod log view exceeds its configured size budget, Podlord MUST remove its oldest log entries until the retained history fits within that budget. |
| LOG-024 | When log entries are removed because of the size limit, the log view MUST visibly indicate that its history has been limited. |
| LOG-025 | A change to the configured log-size limit MUST take effect immediately for existing Pod log views. Lowering the limit MUST immediately trim any existing history that exceeds the new budget under LOG-023. |
| LOG-026 | The configured log-size limit MUST persist across application restarts. |
| LOG-027 | Raising the log-size limit MUST immediately increase the budget available for further log entries in existing Pod log views. |
| LOG-028 | If size-limit eviction removes the entry currently being read, the log view MUST position the user at the oldest remaining entry. |
| LOG-029 | When size-limit eviction removes the entry currently being read, the log view MUST visibly explain that the section being read was removed by the limit. |
| LOG-030 | Losing the current reading position through size-limit eviction MUST NOT turn automatic live following back on. |
| LOG-031 | The configurable log-size limit MUST accept only positive whole numbers of MB. Zero MUST NOT mean unlimited retention; fractional values MUST NOT be silently rounded. |
| LOG-032 | Zero, negative, empty, fractional, or otherwise invalid log-size input MUST produce an understandable validation error and MUST leave the previously valid setting effective and unchanged. |
| LOG-033 | When a change of container, Pod, or session selection or hiding the log view makes queued log requests obsolete, Podlord MUST remove those requests if they have not yet been sent. |
| LOG-034 | A selection change or hiding the log view MUST NOT cancel a log request already sent to the API. That request MUST be allowed to complete. |
| LOG-035 | Responses from already sent log requests MUST still be processed for their originating Pod, container, and session, including cache updates and failure handling. Existing cache TTL and log-size limits continue to apply. |
| LOG-036 | A late response MUST NOT change the displayed logs or errors of a different selection, nor update a hidden log view. Processing that response MUST NOT resume polling for the hidden view. |

### Acceptance And Evidence

| Requirement | Public scenario | Expected result | Evidence status |
|---|---|---|---|
| LOG-001, LOG-004 | Open logs for a Pod with one container. | Its logs load directly; the request explicitly names that container. | Planned check; implementation review pending. |
| LOG-002, LOG-004 | Open logs for a Pod with multiple containers and no previous selection. | `all` is selected; logs are requested for each actual container without requesting a container literally named `all`. | Planned check; implementation review pending. |
| LOG-002 | Choose one container from `all`. | The log view shows only the chosen container's logs. | Planned check; implementation review pending. |
| LOG-003 | Select one container, inspect another Pod or session, then return to the original Pod and session. | The original selection is restored. | Planned check; implementation review pending. |
| LOG-003 | Select `all`, inspect another Pod or session, then return. | `all` remains selected for the original Pod and session. | Planned check; implementation review pending. |
| LOG-005, LOG-006 | Return interleaved timestamped entries from multiple containers while `all` is selected. | One combined view shows the entries in chronological order; each entry visibly identifies its originating container. | Planned check; implementation review pending. |
| LOG-007, LOG-008 | Select `all` for a three-container Pod; two containers return logs and the third fails. | The available logs remain visible and continue refreshing; the failure identifies the third container without replacing the whole view. | Planned check; implementation review pending. |
| SES-026, LOG-009, LOG-010 | Observe automatic refresh for a visible multi-container log view with `all` selected, then hide the view. | Refresh cycles occur normally every 3 seconds and request each container separately; hiding the view pauses polling. | Planned check; implementation review pending. |
| LOG-011 | Keep one refresh cycle in flight longer than 3 seconds. | No overlapping automatic refresh cycle starts while the first remains in flight. | Planned check; implementation review pending. |
| LOG-012 | Make API backoff extend beyond the normal 3-second interval. | Automatic refresh waits for the applicable backoff rather than continuing at the normal cadence. | Planned check; implementation review pending. |
| LOG-013, LOG-014 | Open logs or change the selected container between automatic refresh ticks while the matching cache is available and the fresh response is delayed. | Matching cached logs appear immediately; a foreground refresh is requested without waiting for the next tick. | Planned check; implementation review pending. |
| LOG-014, LOG-015 | Open logs while background work is queued and the request gate is available. | Foreground log requests take priority over queued background work. | Planned check; implementation review pending. |
| LOG-015 | Open logs or change the selected container while a request limit or API backoff prevents dispatch. | Cached logs remain available under their TTL; the foreground request respects the active constraint instead of bypassing it. | Planned check; implementation review pending. |
| LOG-016 | Pause a visible log view, allow background data to change, and wait across automatic refresh ticks. | No new automatic log requests start; background updates do not change the displayed logs. | Planned check; implementation review pending. |
| SES-027, LOG-016 | Pause a log view, switch to another session tab, and return to the paused view. | Returning does not resume automatic log requests or apply background changes to the paused display. | Planned check; implementation review pending. |
| LOG-017 | Resume a visible paused log view between automatic refresh ticks. | A fresh foreground request is initiated immediately, subject to request limits and backoff; automatic refresh subsequently resumes at its normal cadence. | Planned check; implementation review pending. |
| LOG-018 | Remain at the end of a refreshing log view while new entries arrive. | The view follows the new entries automatically. | Planned check; implementation review pending. |
| LOG-019 | Scroll up in a refreshing log view and allow more entries to arrive. | The reading position remains stable while log requests continue. | Planned check; implementation review pending. |
| LOG-020 | Scroll up, then activate `Live follow`. | The view returns to the end and follows subsequent entries. | Planned check; implementation review pending. |
| LOG-021 | Open Settings without having changed the retained-log size setting. | The setting displays a default of 5 MB and allows the user to change it. | Planned check; implementation review pending. |
| LOG-022 | Open log views for two Pods with `all` selected and multiple containers producing entries. | Each Pod view has its own size budget; entries from its containers count toward one shared budget. | Planned check; implementation review pending. |
| LOG-023, LOG-024 | Continue receiving log entries until a Pod view exceeds the configured size budget. | The oldest entries are removed, retained history fits within the budget, and a visible indication explains that the history was limited. | Planned check; implementation review pending. |
| LOG-025 | Lower the size limit below the retained history size in an existing Pod log view. | Its oldest entries are trimmed immediately to fit the new budget, with the visible limit indication. | Planned check; implementation review pending. |
| LOG-026 | Change the configured size limit and restart Podlord. | Settings and Pod log views use the saved limit rather than resetting to the default. | Planned check; implementation review pending. |
| LOG-027 | Raise the size limit while a Pod log view is open, then receive more entries. | The view immediately uses the larger budget for further entries. | Planned check; implementation review pending. |
| LOG-028, LOG-029, LOG-030 | Scroll up to read an earlier entry, then receive enough new entries or lower the size limit so the entry being read is removed. | The view positions the user at the oldest remaining entry, explains why the section disappeared, and keeps automatic live following off. | Planned check; implementation review pending. |
| LOG-031 | Set the retained-log size limit to a valid positive whole number. | Settings accept and apply the exact value without rounding. | Planned check; implementation review pending. |
| LOG-032 | Enter zero, a negative value, an empty value, a fraction, or nonnumeric text after a valid setting. | Each invalid input shows an understandable error; the prior valid setting remains effective and unchanged. | Planned check; implementation review pending. |
| LOG-033 | Queue a log request behind other work, then change the selected container, Pod, or session or hide the log view before dispatch. | The obsolete queued request is removed and never sent to the API. | Planned check; implementation review pending. |
| LOG-034, LOG-035, LOG-036 | Send a log request, delay its response, and switch to a different selection before the response arrives. | The original request completes without selection-triggered cancellation; its response is processed for its original scope, and the current selection's logs and errors remain unchanged. | Planned check; implementation review pending. |
| LOG-034, LOG-035, LOG-036 | Send a log request, hide its view, then return a success or failure. | The response is processed for its originating scope without updating the hidden view or restarting its polling. | Planned check; implementation review pending. |
| LOG-035 | Complete an earlier request while its log view is hidden, then reopen the original selection within its cache TTL. | Successfully cached entries from that response are available under the existing log-size limit. | Planned check; implementation review pending. |

## Confirmed Inspector Requirements

These requirements are confirmed product behavior. Implementation conformance has not yet been reviewed.

| ID | Required observable behavior |
|---|---|
| INS-001 | Clicking a resource row MUST open that resource in the inspector. |
| INS-002 | The resource-row context menu MUST provide an `Open in inspector` action that opens that resource. |
| INS-003 | Activating a cell action such as copying a value MUST NOT additionally open the inspector. |
| INS-004 | When opening a resource, the inspector MUST display available cached data immediately and request fresh details with foreground priority. Entering the YAML view after its detail cache has expired MUST request fresh details with the same priority when no editing draft exists. Re-selecting the already active YAML view MUST NOT enqueue another read. |
| INS-005 | When no cache is available, the inspector MUST immediately display the resource name and a loading state while fresh details are requested. |
| INS-006 | If a fresh-detail request fails, the inspector MUST retain available cached data and visibly indicate both the failure and the data's freshness. |
| INS-007 | When fresh data replaces cached data for the same inspected resource, the inspector MUST preserve the user's scroll position. |
| INS-008 | Cached YAML MUST be viewable before fresh YAML arrives, but MUST remain read-only. YAML editing MUST become available only after a fresh YAML request for the currently inspected resource completes successfully. |
| INS-009 | A failed fresh YAML request MUST leave YAML editing unavailable while keeping available cached YAML viewable. |
| INS-010 | After the user begins editing freshly loaded YAML, subsequent refresh results MUST NOT overwrite the user's draft. |
| INS-011 | When a newer server version is detected during YAML editing, the inspector MUST visibly indicate that a newer version exists while preserving the user's draft. |
| INS-012 | When changing the inspected resource or closing the inspector, session tab, or window would discard unapplied YAML changes, Podlord MUST first ask whether to discard the changes or stay. |
| INS-013 | Without explicit confirmation to discard, Podlord MUST preserve the draft and MUST NOT complete the action that would discard it. |
| INS-014 | After explicit confirmation to discard, Podlord MUST perform the originally requested resource change or close action without applying the discarded draft to the server. |
| INS-015 | Leaving or closing an inspector MUST NOT automatically apply YAML changes. |
| INS-016 | If the server resource changed after the YAML editing baseline was loaded, applying the draft MUST report a conflict instead of blindly overwriting the newer server version. |
| INS-017 | An apply conflict MUST preserve the user's YAML draft. |
| INS-018 | After an apply conflict, Podlord MUST allow the user to compare the draft with the current server version and consciously reconcile the changes. |
| INS-019 | Before applying edited YAML, Podlord MUST present a change preview containing a diff. |
| INS-020 | The apply preview MUST identify the target cluster, namespace, and resource name. |
| INS-021 | Podlord MUST send the YAML change to Kubernetes only after explicit confirmation in the apply preview. |
| INS-022 | Cancelling the apply preview MUST preserve the draft and MUST NOT send the YAML change to Kubernetes. |
| INS-023 | If applying YAML fails or its outcome is uncertain, Podlord MUST preserve the user's draft. |
| INS-024 | After a timeout or response loss that leaves a dispatched YAML change's outcome unknown, Podlord MUST visibly identify the outcome as uncertain rather than reporting a definite failure or success. |
| INS-025 | After an uncertain YAML apply outcome, Podlord MUST request the current resource from the originating Kubernetes target to inspect its observed state. |
| INS-026 | An uncertain YAML apply outcome MUST NOT trigger automatic retransmission of the write. |
| INS-027 | If a fresh read-back shows the requested changes present, Podlord MUST report that the desired state is observed without claiming that the original request executed exactly once or sending another write. |
| INS-028 | If read-back fails or does not show the requested changes, Podlord MUST retain the uncertainty about the original write's outcome rather than treating that observation as proof that the write failed. |

The overview MUST expose available cluster, readiness, restart, and owner metadata alongside resource identity, status, node, and image. Readiness MUST preserve its count and denominator: a Pod with known desired containers but no reported ready containers shows `0/N`, not a missing value. Full, partial, and absent readiness remain distinguishable; a zero desired replica count is neutral rather than a fabricated healthy measurement. Missing restart observations MUST NOT be replaced with zero. Custom resources MUST NOT acquire Pod/workload readiness merely because their kind name resembles a built-in resource.

Foreground requests remain subject to the request limits and API backoff described in the operational model and ADR 0002. Opening YAML MUST NOT overwrite or automatically refresh an editing draft. Authentication rejection still requires the suspension and explicit recovery action defined in ERR-004 and ERR-005; changing inspector views does not authorize a login or retry.

Any renewed apply after an uncertain outcome remains subject to the existing preview, explicit confirmation, and conflict protection in INS-016 through INS-021. Read-back does not authorize replacing the editing baseline or overwriting concurrent changes.

The rationale for the YAML editing safeguards is recorded in [ADR 0008](../adr/0008-protect-yaml-editing-and-apply.md). The requirements above remain the owning behavioral contract.

### Acceptance And Evidence

| Requirement | Public scenario | Expected result | Evidence status |
|---|---|---|---|
| INS-001 | Click a resource row. | The inspector opens the resource represented by that row. | Planned check; implementation review pending. |
| INS-002 | Right-click a resource row and choose `Open in inspector`, even when a different row was selected previously. | The inspector opens the resource whose context menu was invoked. | Planned check; implementation review pending. |
| INS-003 | Activate the copy-value action in a resource cell. | The value is copied without additionally opening or changing the inspector. | Planned check; implementation review pending. |
| INS-004 | Open a resource with cached detail while delaying the fresh response. | Cached detail appears before the response; fresh details are requested with foreground priority. | Planned check; implementation review pending. |
| INS-004, INS-008, INS-010 | Let the detail cache expire, then enter YAML; separately repeat the scenario while an editing draft exists. | Without a draft, a foreground read reloads YAML and editing remains unavailable until it succeeds. With a draft, switching views preserves its contents and does not initiate another read. | Native public UI regressions and real local Kubernetes desktop evidence: see the unlocked comparison section in [the test map](k3d-test-map.md). |
| INS-005 | Open a resource without cached detail while delaying the fresh response. | Its name and loading state appear before the response. | Planned check; implementation review pending. |
| INS-006 | Open a resource with cached detail and fail the fresh request. | Cached detail remains visible with a failure indication and freshness information. | Planned check; implementation review pending. |
| INS-007 | Open cached detail, scroll within it, and then deliver fresh data for the same resource. | The fresh data becomes visible without resetting the user's scroll position. | Planned check; implementation review pending. |
| INS-008 | Open cached YAML while delaying the fresh YAML response and attempt to start editing. | Cached YAML is visible but cannot be edited; successful fresh YAML loading enables editing. | Planned check; implementation review pending. |
| INS-009 | Open cached YAML and fail the fresh YAML request. | Cached YAML remains viewable and editing remains unavailable. | Planned check; implementation review pending. |
| INS-010, INS-011 | Begin editing freshly loaded YAML, then deliver a refresh containing a newer server version. | The draft remains unchanged and the inspector indicates that a newer server version exists. | Planned check; implementation review pending. |
| INS-012 | Edit YAML without applying it, then attempt a resource change or close the inspector, session tab, or window. | A discard-or-stay prompt appears before any draft would be lost. | Planned check; implementation review pending. |
| INS-013 | Choose to stay or dismiss the discard prompt. | The draft and current view remain; the action that would discard the draft is not completed. | Planned check; implementation review pending. |
| INS-014, INS-015 | Confirm discarding an unapplied draft after requesting a resource change or close action. | The requested action completes without applying the discarded draft to the server. | Planned check; implementation review pending. |
| INS-016, INS-017, INS-018 | Load fresh YAML, edit it, change the same resource on the server, and then attempt to apply the draft. | Apply reports a conflict without overwriting the newer server version; the draft remains available for comparison and conscious reconciliation. | Planned check; implementation review pending. |
| INS-019, INS-020, INS-021 | Request to apply edited YAML without yet confirming the preview. | A diff and the target cluster, namespace, and resource name are visible; no YAML change has been sent. | Planned check; implementation review pending. |
| INS-021 | Explicitly confirm the apply preview. | The previewed change is sent to its identified Kubernetes target. | Planned check; implementation review pending. |
| INS-022 | Cancel the apply preview. | The draft remains available and no YAML change is sent. | Planned check; implementation review pending. |
| INS-023 | Submit an edited draft and receive a definite validation or authorization rejection. | The failure is visible and the draft remains available. | Planned check; implementation review pending. |
| INS-023, INS-024, INS-025, INS-026 | Dispatch a confirmed apply and lose its response while delaying read-back. | The draft remains available, the outcome is visibly uncertain, a read is requested for the original target, and no automatic second write is sent. | Planned check; implementation review pending. |
| INS-027 | Apply the requested changes on the server, lose the write response, and return a fresh read showing those changes. | Podlord reports the observed desired state without claiming exactly-once execution or sending another write. | Planned check; implementation review pending. |
| INS-028 | Lose an apply response, then fail the read-back or return a resource without the requested changes. | The original write remains explicitly uncertain; the draft is retained and no automatic retransmission occurs. | Planned check; implementation review pending. |
| INS-016, INS-019, INS-020, INS-021, INS-026 | After an uncertain apply, observe concurrent server changes and explicitly request another attempt. | A new preview and confirmation are required; read-back does not silently rebase the draft or bypass conflict protection. | Planned check; implementation review pending. |

## Confirmed Secret Presentation Requirements

These requirements were confirmed on 2026-10-01. Implementation conformance has not yet been reviewed. Presentation safeguards do not grant Kubernetes access beyond the session's permissions. Decision rationale is owned by [ADR 0009](../adr/0009-explicit-secret-reveal-and-copy.md).

| ID | Required observable behavior |
|---|---|
| SEC-001 | Podlord MUST hide Secret values by default, including their representation in Secret YAML and change diffs. Base64-encoded values MUST be treated as sensitive values, not as redaction. |
| SEC-002 | Revealing a Secret value MUST require an explicit user action for that individual value. Revealing one value MUST NOT reveal other values. |
| SEC-003 | Copying a Secret value MUST require an explicit user copy action for that individual value. Revealing a value MUST NOT automatically copy it. |
| SEC-004 | Changing the inspected resource or session, or closing the inspector, MUST reset revealed Secret values to hidden presentation. |
| SEC-005 | Refreshing Secret data MUST NOT reveal a previously hidden value without an explicit reveal action under SEC-002. |
| SEC-006 | Podlord-generated diagnostic and audit output MUST NOT contain Secret values, whether decoded or base64-encoded, including values carried in request bodies, responses, or errors. |
| SEC-007 | Ordinary ConfigMap values MUST remain directly viewable without Secret reveal actions. |
| SEC-008 | Presentation redaction MUST NOT replace actual Secret values in an apply payload. Editing and applying other fields MUST preserve untouched Secret values rather than submitting masking placeholders or erasing those values. |

### Acceptance And Evidence

| Requirement | Public scenario | Expected result | Evidence status |
|---|---|---|---|
| SEC-001 | Open a Secret's values, YAML, and change preview without revealing values. | Neither decoded nor base64-encoded Secret values are exposed in those surfaces. | Planned check; implementation review pending. |
| SEC-002 | Explicitly reveal one of several Secret values. | Only the requested value becomes visible. | Planned check; implementation review pending. |
| SEC-003, INS-003 | Reveal a value, then separately invoke its copy action. | Reveal does not change the clipboard; the copy action copies only the requested value without opening another inspector. | Planned check; implementation review pending. |
| SEC-004 | Reveal a value, then change resource or session or close and reopen the inspector. | The subsequent Secret presentation starts with its values hidden. | Planned check; implementation review pending. |
| SEC-005 | Keep values hidden and deliver fresh Secret data. | The updated values remain hidden. | Planned check; implementation review pending. |
| SEC-006 | Read, reveal, copy, or apply Secret values and inspect Podlord diagnostics and audit output, including operation failures. | Neither decoded nor encoded values appear in app-generated diagnostics or audit records. | Planned check; implementation review pending. |
| SEC-007 | Open an ordinary ConfigMap. | Its values are directly visible without Secret-specific reveal actions. | Planned check; implementation review pending. |
| SEC-001, SEC-008, INS-008 | Load fresh Secret YAML, edit an unrelated field while Secret values remain hidden, and confirm apply. | The preview keeps values hidden; the server retains untouched Secret values, with no masking placeholders submitted. | Planned check; implementation review pending. |

## Confirmed Resource Deletion Requirements

These safeguards were confirmed on 2026-10-01. They apply to supported resource deletion operations and do not introduce additional resource actions. Implementation conformance has not yet been reviewed.

| ID | Required observable behavior |
|---|---|
| ACT-001 | Before sending a resource deletion request, Podlord MUST require explicit confirmation identifying the target cluster, namespace, resource type, and resource name. Cluster-scoped targets MUST be identified as cluster-scoped rather than assigned an invented namespace. |
| ACT-002 | For deletion of multiple selected resources, the confirmation MUST show the number of targets and identify the affected resources and their scopes. |
| ACT-003 | Cancelling a deletion confirmation MUST NOT send a deletion request. |
| ACT-004 | Confirming deletion MUST authorize only the targets displayed in that confirmation. Subsequent selection or session changes MUST NOT retarget the authorized operation. |
| ACT-005 | Podlord MUST NOT automatically escalate a deletion to force-delete. |
| ACT-006 | A deletion failure MUST be visibly reported rather than presented as a successful deletion. |
| ACT-007 | A timeout or response loss leaving a dispatched deletion's outcome unknown MUST be presented as uncertain, not as definite success or failure. |
| ACT-008 | After an uncertain deletion outcome, Podlord MUST read the target's current server state before considering another deletion attempt. |
| ACT-009 | An uncertain deletion outcome MUST NOT trigger automatic retransmission of the deletion request. A renewed attempt MUST require confirmation under ACT-001 and ACT-002. |
| ACT-010 | Deleting a single resource MUST be confirmable without requiring the user to type its name. |

Read-back establishes observed state, not proof that the original request executed exactly once. Absence, continued presence, or read-back failure must not be used to claim knowledge of the original request's execution. Read-back does not authorize deleting a different resource created under the same name. The rationale is recorded in [ADR 0008](../adr/0008-protect-yaml-editing-and-apply.md).

### Acceptance And Evidence

| Requirement | Public scenario | Expected result | Evidence status |
|---|---|---|---|
| ACT-001, ACT-010 | Request deletion of one supported namespaced resource. | The confirmation identifies cluster, namespace, type, and name; no request is sent before confirmation, and no name typing is required. | Planned check; implementation review pending. |
| ACT-001 | Request deletion of a supported cluster-scoped resource. | Confirmation identifies the cluster, type, name, and cluster-scoped status without inventing a namespace. | Planned check; implementation review pending. |
| ACT-002 | Request deletion of multiple supported resources. | Confirmation shows the target count and identifies each affected resource and scope. | Planned check; implementation review pending. |
| ACT-003 | Cancel the single-resource or multiple-resource confirmation. | No deletion request is sent. | Planned check; implementation review pending. |
| ACT-004 | Display a deletion confirmation, then change selection or session before dispatch. | Confirmation cannot cause a request for a target other than those displayed. | Planned check; implementation review pending. |
| ACT-005, ACT-006 | Reject or delay ordinary deletion so that automatic force deletion might otherwise be considered. | No automatic force-delete request is sent; an actual rejection is reported as failure. | Planned check; implementation review pending. |
| ACT-007, ACT-008, ACT-009 | Dispatch a confirmed deletion and lose its response. | The outcome is visibly uncertain; Podlord requests current state for the original target without automatically sending another deletion. | Planned check; implementation review pending. |
| ACT-007, ACT-008 | After losing a deletion response, return absence, continued presence, or a read failure. | Podlord distinguishes observed state from the unknown execution outcome and does not assert exactly-once execution. | Planned check; implementation review pending. |
| ACT-001, ACT-002, ACT-009 | Explicitly request another deletion after an uncertain outcome. | A renewed confirmation is required before any further deletion request. | Planned check; implementation review pending. |

## Confirmed Shared Table Requirements

These requirements were confirmed on 2026-10-02 and apply to all data tables. Implementation conformance has not yet been reviewed. Resource-row inspector interactions remain owned by INS-001 through INS-003; non-resource tables do not acquire inspector navigation. Decision rationale is owned by [ADR 0010](../adr/0010-consistent-table-interaction-and-layout.md).

| ID | Required observable behavior |
|---|---|
| TBL-001 | Activating a sortable column MUST cycle through ascending, descending, and no user sort (`ASC`, `DESC`, `NONE`). |
| TBL-002 | Numeric values, quantities such as resource sizes, and temporal values MUST be sorted according to their underlying meaning rather than their formatted display strings. |
| TBL-003 | Selecting `NONE` MUST restore the table's stable default ordering. |
| TBL-004 | Users MUST be able to show and hide table columns. |
| TBL-005 | Users MUST be able to reorder table columns. |
| TBL-006 | Users MUST be able to pin columns so they remain visible while scrolling horizontally. |
| TBL-007 | Column layout, including visibility, ordering, pinning, and widths, MUST be saved per table type and restored across app restarts. |
| TBL-008 | The same image, cluster, namespace, status, resource type, or node MUST have consistent identifying colors wherever that category is presented. |
| TBL-009 | Colored identities and statuses MUST retain textual identification; color MUST NOT be their only identifying information. |
| TBL-010 | Users MUST be able to copy cell values. |
| TBL-011 | Truncated cell content MUST be available in full through both hover and keyboard interaction without triggering a network request. |
| TBL-012 | Data refresh MUST preserve selection and the user's reading position when the corresponding resource or record remains in the displayed table. |

The default order for each table and any genuine non-sortable columns remain to be identified in the later scope and implementation review. Confirmation of the common interaction does not claim that every existing table already conforms.

### Which Resource Values Must The Native Table Preserve?

The resource data-column inventory is Name, Kind, Namespace, Status, Node, Image,
Cluster, CPU, Memory, Storage, Age, Ready, Restarts and Owner. Issue provides the
native cache's textual problem explanation in addition to Status. This records the
existing C# data-column baseline under RWT-009 through RWT-015; it does not close
the separate Port Forward action or advanced-filter migration.

CPU and byte quantities use canonical measured values for sorting, not their
display units. Missing measurements remain unavailable rather than being replaced
by a request, limit or capacity. Zero measurements remain visible as zero; partial
and stale measurements retain their qualifiers. Age sorts by elapsed time rather
than its compact label. Missing values sort after known values in either direction;
equal values retain the stable resource-path order. NONE restores that path order.

Workload images retain the full image identity, including registry and tag. Pod
fields require core v1 Pod identity; Deployment, ReplicaSet, StatefulSet and
DaemonSet templates require apps/v1 identity; Job and CronJob templates require
batch/v1 identity. A custom resource with the same kind name must not inherit
these Kubernetes-specific interpretations. Ready is available for Pods and the
three replica-based workloads; the latter use readyReplicas and desired replicas,
including the API's default of one and an explicit zero. Non-Pod restarts are
unavailable, not an invented zero. Event Owner identifies involvedObject/regarding
when both kind and name exist. Core Node and apps/v1 Deployment/ReplicaSet status
derive from their actual conditions and replica counts, not an Observed placeholder.

New columns must not discard an existing native six-column layout. Reading that
valid older layout preserves order, visibility, pinning and widths, appends new
columns, and does not rewrite the saved file. An explicit successful save upgrades
the file. Malformed or unsupported layouts fail without replacing stored bytes.
Numeric values, ages and empty cells must not acquire arbitrary identity colors.

Native-table evidence belongs to the [existing test map](k3d-test-map.md). Component
and offscreen UI checks do not establish parity for every table, theme or device.

### Acceptance And Evidence

| Requirement | Public scenario | Expected result | Evidence status |
|---|---|---|---|
| TBL-001, TBL-003 | Activate a sortable header three times from `NONE`. | The table changes to ascending, descending, and then its stable default ordering. | Planned check; implementation review pending. |
| TBL-002 | Sort columns containing numeric, size, and temporal values whose display-string order differs from their semantic order. | Ascending and descending results follow the underlying values, not formatted text. | Planned check; implementation review pending. |
| TBL-003 | Select `NONE`, refresh unchanged records, and switch away and back. | The same default relative order is retained. | Planned check; implementation review pending. |
| TBL-004, TBL-005, TBL-006 | Hide and restore a column, reorder columns, pin a column, and scroll horizontally. | The requested layout changes are visible and the pinned column remains visible during horizontal scrolling. | Planned check; implementation review pending. |
| TBL-007 | Change column visibility, order, pinning, and widths, then reopen the same table type and restart the app. | The saved layout is restored for that table type rather than lost or applied to unrelated table types. | Planned check; implementation review pending. |
| TBL-008, TBL-009 | Display matching image, cluster, namespace, status, type, and node identities in different applicable table surfaces. | Corresponding identities use consistent colors and remain identifiable by text. | Planned check; implementation review pending. |
| TBL-010, INS-003 | Copy a resource table cell value. | The value is copied without also opening or changing the inspector. | Planned check; implementation review pending. |
| TBL-011 | Hover over truncated content, then access the same content using only the keyboard while observing API traffic. | The full content is available through either interaction; neither triggers a network request. | Planned check; implementation review pending. |
| TBL-012 | Select a record, scroll within a table, and deliver refreshed records while the selected and reading-position records remain displayed. | Selection and reading position are retained rather than reset to the first row. | Planned check; implementation review pending. |

## Confirmed Filter And Session Presentation Requirements

These requirements were confirmed on 2026-10-02. Implementation conformance has not yet been reviewed. Per-table-type column layout remains owned by TBL-007; filter and sort state below is session-specific. Decision rationale is owned by [ADR 0011](../adr/0011-session-scoped-filtered-resource-presentation.md).

| ID | Required observable behavior |
|---|---|
| FLT-001 | Filters MUST be retained per session and restored when returning to that session. |
| FLT-002 | Sort state MUST be retained per session and table type and restored when returning to that session and table type. |
| FLT-003 | The resource table, radar, and corresponding match counts MUST represent the same filtered resource set. The radar and counts MUST NOT be limited to the currently visible or rendered table rows. |
| FLT-004 | Changing filters MUST evaluate available cached data without initiating an API request because of the filter change. |
| FLT-005 | Filter changes MUST update the visible table, radar, and match counts immediately from available cached data rather than waiting for background synchronization. |
| FLT-006 | Filters MUST restrict presentation without restricting background synchronization of resources in the session's configured scope. |
| FLT-007 | Switching sessions MUST NOT present the previous session's resources or filters as belonging to the newly active session. |
| FLT-008 | Responses arriving after a session switch MUST remain associated with their originating session and MUST NOT populate another session's visible resources or filter state. |
| FLT-009 | Switching session tabs MUST NOT replay notifications for already known alarms merely because a tab became active. |
| FLT-010 | When no resources match, the resource table and radar MUST show a clear empty state and provide a way to reset the filters. |
| FLT-011 | Within each field or global search, expression alternatives MUST be combined with OR. Different field expressions and global search MUST all match the resource. |
| FLT-012 | A field value picker MUST read the complete current session cache. Its open option snapshot MUST stay stable until an explicit reload, preserve selected values absent from a refreshed cache, and close on a session change. |
| FLT-013 | CPU, memory and storage quantity filters MUST compare actual cached measurements only. Missing measurements MUST NOT match a numeric comparison or be substituted with zero, requests, limits or capacity. References remain separate from measured usage. |
| FLT-014 | Wide-window sidebar fields MUST open a nonmodal, viewport-bounded flyout at the selected field, leaving workspace navigation operable. Escape, outside presses, session changes and an anchor leaving or being reused in the list MUST close it. The complete field picker and narrow-window selection MUST remain centered and operable. Checkbox state, borders and keyboard focus MUST remain visible in the selected theme. |
| FLT-015 | The reference row Limit MUST remain available, default to 256 and accept 1 through 5,000 rows. It MUST cap table presentation after filtering and sorting, not discovery, synchronization, the complete filtered match count, Radar, field-picker values or alarm evaluation. Invalid/nonpositive textual input uses the reference default; positive values above the maximum clamp to 5,000. Changing the Limit MUST NOT initiate a Kubernetes request or replay alerts. |
| FLT-016 | The row Limit MUST follow its session across tab/window changes and restart, be retained by saved presets and legacy preset import, and return to 256 when resource filters are reset or the protected default preset is loaded. Earlier native view/preset records MUST read as 256 without an implicit write. Invalid current records MUST be retained and rejected explicitly. |

Filter and sort retention across app restarts is governed by STR-003 and STR-004,
with the native storage boundary recorded in [ADR 0027](../adr/0027-session-filter-and-sort-persistence.md).
Quantitative responsiveness is governed by the confirmed performance requirements.
Alarm evaluation uses the complete session cache under ALT-001; presentation
filters must not hide alarms. FLT-009 establishes non-replay on tab switches.

### Which Existing Search Expressions Must Remain Available?

Under the retained filter capability LEG-012 through LEG-014, resource and Event
searches accept case-insensitive substring terms, space-separated alternatives,
quoted exact values, `=value` exact text, `~prefix`, `suffix~`, `/regular expression/`
and integer comparisons (`>`, `<`, `>=`, `<=`, `=>`, `=<`, `=`). Search alternatives
match any applicable cached displayed value; they do not require every term to
match the same row. Quoted values can contain spaces and escaped quotes. Regex
escapes remain intact, including character classes; escaped delimiter slashes
are supported. Empty or whitespace-only searches clear the restriction.

Malformed expressions are visibly reported without dispatching a request, losing
the expression, or silently presenting an unfiltered result. Regex evaluation
uses the existing native alert match/depth budgets and reports exhaustion rather
than blocking the UI indefinitely. Invalid-search presentation remains scoped to
the resource or Event search that produced it. Resource search membership is
shared by the table, radar and match count; Event search does not overwrite the
resource search. Persisted search strings use the same existing session view
record. The initial global-search increment kept version 1; the field-filter
increment extends that same record to version 2 under
[ADR 0031](../adr/0031-cached-field-filters-and-measurement-semantics.md).
The implemented text-field picker covers Name, Kind, Namespace, Status, Node,
Image, Cluster, Owner, Issue and Ready. Ready matches its displayed fraction as
text, not a silently inferred percentage. Restarts uses the shared integer/text
grammar: alternatives, including numeric comparisons, use OR. Native currently
treats missing restart observations as unavailable; the legacy integer default
is zero. That compatibility difference needs an explicit decision before
Restarts is accepted as equivalent. FLT-013 applies to CPU, Memory and Storage,
not an inferred change to other counters. These quantities use actual measurements
under FLT-013; quantity ranges use AND and exact numeric alternatives use OR.
Display-text filters can explicitly match unavailable/stale/incomplete labels
without inventing a measurement. Problems/Activity and native saved presets now
have the scoped contracts below. Age, UID and explicit legacy preset import have
public-boundary coverage. The row Limit now retains reference display semantics
under FLT-015/016; installed-process migration and complete paired presentation
remain separate gates. Implementation and public-boundary evidence
are tracked independently in the [test map](k3d-test-map.md).

### How Must Radar Present Arriving Resources And New Problems?

Radar MUST be a deterministic RTS-style resource island, not a rectangular list
of resources. The retained C# cluster-center, namespace-arm, dependency-rank,
stable-hash and collision-placement rules define the topology. Identical cached
identities and topology MUST produce identical world positions independent of
table sorting, filters, health changes and resource versions. Incoming accepted
cache collections progressively extend the map. Cluster/namespace decorations
are cache-derived context, not invented Kubernetes resources or alarm matches.

While Radar is focused, arrow keys and WASD pan in both axes; plus/minus zoom,
zero resets, and Home/End focus the first/last filtered resource. Enter inspects
the focused resource. These keys MUST NOT control Radar while a filter or another
input is focused. Left-button dragging pans without opening an inspector; a
resource click does inspect. Mouse-wheel and trackpad scrolling zoom around the
pointer without a modifier; scrolling MUST NOT pan. Zoom retains the C# range
0.55 through 3.4. Each open session retains its own two-dimensional camera during
view/tab switches. Filtering MUST NOT move the camera or rearrange surviving
resources. Keyboard focus MUST retain resource identity, not a proxy row number;
if filtering hides it, Enter MUST NOT inspect its replacement row. The user must
explicitly select another resource. Empty membership stays empty; context decoration cannot become a
selectable fake resource. No idle screensaver or unrelated decorative motion is introduced.

Radar MUST render accepted cache fragments while synchronization continues; it
MUST NOT wait for every collection or cluster. Its session health bar and counts
MUST grow from those same accepted fragments. Loading remains explicit without
inventing a completion percentage while discovery is incomplete. Rendering,
hover, filtering and automatic focus MUST NOT request Kubernetes data.

The three retained built-in alarm rules remain authoritative. A recent-change
highlight is green (`#7DFFC3`), a problem classified as a warning is yellow
(`#FFE866`), and a severe failure is red (`#FF5C5C`). Restart outliers use the
existing session population threshold, not another Radar threshold. Health
severity is classified alongside canonical cache problem state; the UI consumes
it rather than recomputing Kubernetes health. Custom alarm colors, disabled rules,
finite effect expiry and reduced motion retain their existing contracts.
Resource names and warning/error labels MUST expose meaning without color alone.

When a focus-enabled rule observes a newly matching or changed problem, Radar
MUST focus that newly affected filtered resource, not an unchanged older match.
Within a batch, the most recently observed change wins; simultaneous changes
prefer severe failures, then deterministic resource-path order. An unchanged
refresh MUST NOT replay focus. A filtered-out resource MUST NOT be selected or
reintroduced, although its alarm still evaluates under ALT-001. Focus MUST NOT
open the inspector or change the active session.

The native release MUST NOT include the idle Radar screensaver. This explicit
user decision on 2026-10-05 supersedes preservation of that legacy effect in
LEG-026. It does not remove resource highlights or authorize dropping the spatial
map, pan, zoom, reset or other retained Radar interactions.

Background water remains required; Event-local waves MUST NOT be drawn. A populated
Radar retains the reference's fixed deep-blue base `#061621` in every theme and
variant, including when water motion is disabled or its speed is zero. The theme's
glass color is not the water base. An empty Radar does not animate idle water.
Healthy terrain colors MUST match the
reference: stone `#6B7378`, forest `#2E5941`, grass `#4E6A43`, dirt `#665A3F`,
sand `#7D7048`, shallow network water `#286473`, deep Event water `#1B4357`.
Solid resource tiles form the island; invented filled corridors are not part of
the reference. Unknown kinds use grass. Retained alert colors override terrain,
without an additional health classifier. Tiny tiles remain solid; at least 9
pixels of tile width enables a dark-filled, terrain/alert-stroked kind glyph.

Appearance settings MUST expose water enablement and a whole-number speed from
0 through 100, default enabled/45. Zero disables waves. The C# 18-pixel water
pattern and activity-dependent drift are retained. Events retain ordinary
terrain/alert tiles without additional water effects. The background MUST NOT
traverse resources or trigger API requests.
One water clock MUST serve the visible viewport, stop while hidden, minimized,
detached, disabled, dragging or in reduced motion, and defer movement during
camera interaction. Reduced motion keeps a static water pattern. Water work MUST
not rebuild resource geometry or keep per-Event timers alive.

### Which Radar Presentation Has Executable Evidence?

The native radar retains the resource glyph catalog from the existing C# shared
glyph control: 24 known Kubernetes kinds and a diamond for unknown or empty kinds.
Related kinds use their existing shared shapes. Geometry is immutable and reused;
painting reads the current kind and color without network, storage or refresh
timers. Accessible resource names, keyboard inspection, alert effects, selection
and the shared filtered resource model remain authoritative.

Home/End navigation must continue reaching the first/last resource after repeated
navigation through a population larger than the viewport. Positioning an indexed
resource resolves its cached world coordinate rather than estimating a list
scroll offset. The hidden native radar releases visible resource delegates and
reattaches the same authoritative filtered cache on opening. The initial visible
population and reopening at the retained position need rendered-resource evidence;
a matching count alone does not prove either behavior. Hidden movement and hover
state must not overwrite the visible session position or expose a tooltip.
Glyph coverage and filtered membership alone do not establish spatial, input,
visual or performance parity. The current scoped evidence and remaining gaps
are recorded in the test map; they do not establish whole-product release readiness.

### Acceptance And Evidence

| Requirement | Public scenario | Expected result | Evidence status |
|---|---|---|---|
| FLT-001, FLT-002 | Set different filters and sort states in two sessions, including different table types, then switch between them. | Each session restores its own filters and each session/table-type pair restores its own sort state. | Planned check; implementation review pending. |
| FLT-003 | Filter a cached resource set containing more matches than the table viewport can display, then scroll the table. | Table membership, radar membership, and match counts agree; scrolling does not change radar membership or counts. | Planned check; implementation review pending. |
| FLT-004, FLT-005 | Change filters while fresh API responses are delayed and observe outgoing requests. | All applicable visible surfaces reflect the matching cached set without waiting for those responses or issuing a filter-triggered API request. | Planned check; implementation review pending. |
| FLT-006 | Filter out a resource, update it through normal background synchronization, then remove the filter. | The resource's synchronized data is available despite having been hidden by the filter. | Planned check; implementation review pending. |
| FLT-007 | Switch from a populated session to another with no available cached data while its response is delayed. | No resources or filters from the previous session are presented as the new session's data. | Planned check; implementation review pending. |
| FLT-008 | Dispatch a refresh for one session, switch to another, then deliver the original response. | The response remains scoped to its originating session and does not change the active session's resources or filters. | Planned check; implementation review pending. |
| FLT-009 | Receive an alarm notification, switch away from its session, and return without a new alarm event. | Returning does not replay the known notification. | Planned check; implementation review pending. |
| FLT-010 | Apply filters with no matches, then activate the reset control. | Both table and radar show a clear empty state before reset and show the unfiltered available resource set after reset. | Planned check; implementation review pending. |

## Confirmed Resource Metric Presentation Requirements

These requirements were confirmed on 2026-10-02. Implementation conformance has not yet been reviewed. They distinguish observed usage from configuration, capacity, and recommendations; they do not introduce a metrics provider or a recommendation engine. Decision rationale is owned by [ADR 0012](../adr/0012-distinguish-metric-usage-and-reference-values.md).

| ID | Required observable behavior |
|---|---|
| MET-001 | Resource presentations MUST distinguish observed usage, configured requests, and configured limits rather than presenting them as interchangeable values. |
| MET-002 | Displayed values MUST identify their units, and applicable resource visualizations MUST include labeled reference markers for available requests and limits. |
| MET-003 | A displayed percentage MUST identify its reference quantity, such as request, limit, or capacity. |
| MET-004 | Values above 100 percent of the stated reference MUST remain observable rather than silently clipped to 100 percent. |
| MET-005 | Absent configured requests or limits MUST be identified as not set. |
| MET-006 | Missing observed metrics MUST be identified as unavailable rather than replaced with zero. |
| MET-007 | Retained stale metrics MUST be visibly identifiable as stale and show their measurement timestamp or age. |
| MET-008 | Aggregated metrics MUST be identified as incomplete when constituent container measurements are missing. Missing measurements MUST NOT be silently treated as observed zero usage. |
| MET-009 | Storage presentations MUST distinguish requested size, provisioned capacity, and actual measured usage. Capacity MUST NOT be presented as measured usage. |
| MET-010 | A recommendation marker MUST be shown only when a substantiated recommendation is available; it MUST NOT be generated from an invented estimate. |
| MET-011 | A displayed recommendation MUST identify its source and remain distinguishable from observed usage and configured request or limit values. |

Applicable metric sources, reference quantities, aggregation membership, freshness thresholds, and any existing recommendation sources remain to be inventoried in the later implementation and scope review. A missing storage-usage source means usage is unavailable, not that an additional provider must be installed. These requirements do not authorize paint-time or hover-triggered fetching.

### Acceptance And Evidence

| Requirement | Public scenario | Expected result | Evidence status |
|---|---|---|---|
| MET-001, MET-002 | Display resource usage with configured requests and limits that differ from the observed value. | Usage, request, and limit are separately labeled with units; applicable visualizations show labeled reference markers. | Planned check; implementation review pending. |
| MET-003, MET-004 | Display observed usage exceeding a configured request while remaining below a different limit. | Percentages identify their denominator; the value exceeding 100 percent of the request remains observable. | Planned check; implementation review pending. |
| MET-005 | Display a resource without a configured request or limit. | Each absent setting is identified as not set rather than represented as a measured zero. | Planned check; implementation review pending. |
| MET-006 | Withhold an observed metric or make its source unavailable, then separately return a valid zero measurement. | Missing data is marked unavailable; a genuine observed zero remains distinguishable from missing data. | Planned check; implementation review pending. |
| MET-007 | Retain an older measurement while fresh retrieval is delayed or fails. | The retained value is visibly stale and its measurement timestamp or age remains available. | Planned check; implementation review pending. |
| MET-008 | Display an aggregate whose contributing containers include both available and missing measurements. | The aggregate is marked incomplete and missing measurements are not presented as observed zero usage. | Planned check; implementation review pending. |
| MET-009 | Display storage with requested size and provisioned capacity but no usage measurement. | Requested size and capacity are separately identified; actual usage is unavailable rather than inferred from either value. | Planned check; implementation review pending. |
| MET-009 | Display storage with a real usage measurement different from its requested size and capacity. | All available quantities retain their distinct meaning and labels. | Planned check; implementation review pending. |
| MET-010, MET-011 | Display resource metrics with no substantiated recommendation, then with an available sourced recommendation. | No invented recommendation marker appears in the first case; the second shows an explicitly sourced recommendation distinct from usage and configured values. | Planned check; implementation review pending. |

## Confirmed Cache And Request Scheduling Requirements

These requirements were confirmed on 2026-10-02. Implementation conformance has not yet been reviewed. Existing inspector and log requirements remain authoritative for their specific interactions. Request-steering rationale is owned by [ADR 0002](../adr/0002-cache-first-rate-limited-kubernetes-requests.md).

| ID | Required observable behavior |
|---|---|
| SYN-001 | Views MUST display available cached data immediately without waiting for a fresh response, authentication completion, or other clusters. This applies throughout app use, not only startup. Cache availability remains subject to the existing retention and TTL rules. |
| SYN-002 | Fresh data retrieval MUST run asynchronously without blocking presentation of available cached data. |
| SYN-003 | Views MUST make loading state and the age of displayed data discernible. |
| SYN-004 | Inspector and explicit manual refresh requests MUST take priority over queued background work while respecting applicable request limits and backoff. This priority MUST NOT require interrupting already dispatched work. |
| SYN-005 | Concurrent equivalent read requests for the same originating session, target context, and request parameters MUST share a request rather than dispatch duplicate parallel reads. Sharing MUST NOT cross session or authorization boundaries. |
| SYN-006 | Obsolete read or refresh requests that have not yet been dispatched MUST be removed without sending them. |
| SYN-007 | A request already dispatched MUST NOT be cancelled merely because its originating view became hidden or its selection changed. Its result MUST be processed in its original context without updating an unrelated view or restarting hidden refresh work. |
| SYN-008 | Hover, scrolling, and layout MUST NOT trigger API requests. |
| SYN-009 | Hidden surfaces MUST NOT run their own refresh loops. |
| SYN-010 | Session synchronization MUST have central ownership rather than be independently driven by each visible or hidden surface. Log-view visibility and pause behavior remain governed by SES-026, SES-027, and LOG-016 through LOG-017. |
| SYN-011 | Settings MUST present a stable snapshot refreshed through explicit user action or bounded TTL refresh, rather than continuously rebuilding large lists. |
| SYN-012 | Discovery metadata MUST have a longer cache freshness TTL than live measurements. |
| SYN-013 | A refresh failure MUST preserve available cached presentation and identify affected data as stale rather than clear the view. |

Read coalescing does not authorize coalescing writes. Cancellation rules above concern changes in view relevance, not transport failure handling or application shutdown. TTL values, discovery membership, sync cadence, and the existing request-limit configuration remain for later scope reconciliation; no numeric defaults are invented here. SYN-013 does not extend cache retention or override its existing TTL rules.

### Acceptance And Evidence

| Requirement | Public scenario | Expected result | Evidence status |
|---|---|---|---|
| SYN-001, SYN-002, SYN-003 | Open a view with available cache while delaying its fresh response. | Cached content appears before the response, interaction remains available, and loading state and data age are discernible. | Planned check; implementation review pending. |
| SYN-001, ERR-014 | Open cached content while login is active or another cluster remains unavailable. | The matching cache appears immediately and remains usable; the existing status indicator reflects active authentication without blocking presentation. | Planned check; implementation review pending. |
| SYN-004 | Queue background work, then open the inspector or request manual refresh while the request gate is available. | Foreground work is dispatched before waiting background work without cancelling an already dispatched request. | Planned check; implementation review pending. |
| SYN-004 | Request a foreground refresh during an active request limit or backoff period. | The foreground request respects that constraint rather than bypassing it. | Planned check; implementation review pending. |
| SYN-005 | Concurrently request the same read through two public consumers in the same session and delay the response. | One API request serves both consumers with the originating scope preserved. | Planned check; implementation review pending. |
| SYN-005 | Request different parameters or the same apparent resource through distinct sessions or authorization contexts. | Non-equivalent reads are not incorrectly merged across their boundaries. | Planned check; implementation review pending. |
| SYN-006 | Queue a refresh, then make all consumers of that refresh obsolete before dispatch. | The obsolete request is removed and never sent. A read still needed by another coalesced consumer is not treated as wholly obsolete. | Planned check; implementation review pending. |
| SYN-007 | Dispatch a read, hide its view or change selection, then deliver a success or failure. | The request finishes without relevance-triggered cancellation; its outcome stays in its original context without changing unrelated presentation or restarting hidden work. | Planned check; implementation review pending. |
| SYN-008 | Hover, scroll, and resize a cached view while observing outgoing API requests. | Those interactions do not cause API calls. | Planned check; implementation review pending. |
| SYN-009, SYN-010 | Hide surfaces while central session synchronization continues. | Hidden surfaces do not issue their own periodic refresh requests; central session sync retains ownership, and hidden log polling remains paused. | Planned check; implementation review pending. |
| SYN-011 | Open Settings, leave its snapshot unchanged while background data changes, and then explicitly refresh or reach the applicable refresh TTL. | Large lists are not continuously rebuilt; an authorized bounded refresh updates the snapshot. | Planned check; implementation review pending. |
| SYN-012 | Observe discovery metadata and live measurements across their configured freshness intervals. | Discovery metadata has a longer freshness TTL than live measurements. | Planned check; implementation review pending. |
| SYN-013 | Fail a fresh request while the view's cached content remains available. | The content remains visible with an indication that affected data is stale. | Planned check; implementation review pending. |

## Confirmed Error And Recovery Requirements

These requirements were confirmed on 2026-10-02. Implementation conformance has not yet been reviewed. Authentication failures require user-confirmed recovery because another credential attempt may launch an interactive browser login. Scheduling rationale is owned by [ADR 0002](../adr/0002-cache-first-rate-limited-kubernetes-requests.md).

| ID | Required observable behavior |
|---|---|
| ERR-001 | Podlord MUST distinguish connection failures, authentication failures, authorization failures, validation failures, and rate limits in understandable user feedback. |
| ERR-002 | Error feedback MUST identify the affected session and operation without exposing credentials or Secret values. |
| ERR-003 | Transient connection failures during reads or synchronization MAY be retried automatically only under a bounded backoff policy and applicable rate limits. Authentication, authorization, and validation failures MUST NOT be treated as transient connection failures. |
| ERR-004 | After an authentication failure, including an expired token, Podlord MUST suspend automatic authentication attempts for the affected session and credential context. Timers, queued work, tab activation, inspector opening, and ordinary refresh MUST NOT bypass that suspension. |
| ERR-005 | Retrying authentication after a known authentication failure MUST require explicit user confirmation identifying the affected session and stating that the attempt may open an interactive login. |
| ERR-006 | One user-confirmed authentication retry MUST NOT authorize an automatic sequence of further authentication attempts after another failure or cancelled login. Further attempts MUST require renewed confirmation. |
| ERR-007 | Authorization and validation failures MUST NOT cause endless automatic retries. Functions unaffected by the failure MUST remain usable. |
| ERR-008 | Repeated equivalent failures MUST be represented by persistent status rather than generate a new toast for every refresh attempt. |
| ERR-009 | An explicit retry control MUST be available and MUST respect request limits and backoff. For authentication failures it MUST use the confirmation required by ERR-005 rather than immediately re-executing authentication. |
| ERR-010 | Successful recovery MUST update data without losing selection or reading position where the corresponding record remains displayed. |
| ERR-011 | Failure status MUST be cleared only after an actual successful check of the affected operation or authentication context, not merely because a retry was requested or a login browser opened. |
| ERR-012 | Automatic recovery of reads or synchronization MUST NOT automatically replay writes. YAML apply and deletion retain their existing confirmation and uncertain-outcome contracts. |
| ERR-013 | When a session needs authentication and its authentication mechanism supports renewed login, Podlord MUST provide a clearly visible, session-specific reauthentication button. Activating it MUST require explicit confirmation before starting the authentication attempt, including browser-based login. |
| ERR-014 | While authentication is active, the existing health/energy/loading indicator MUST visibly reflect the authentication state and available progress. Login activity MUST NOT block cached presentation or imply successful recovery before the successful check required by ERR-011. |

The authentication suspension is specific to the affected credential context: unrelated sessions must not be blocked. Duplicate queued or shared consumers do not each gain authority to re-execute authentication. Initial interactive authentication during workspace restoration is additionally governed by STR-005. The indicator reports known authentication state or available progress, not invented completion percentages. Concrete backoff bounds and failure-classification evidence remain for later scope reconciliation.

### Acceptance And Evidence

| Requirement | Public scenario | Expected result | Evidence status |
|---|---|---|---|
| ERR-001, ERR-002 | Produce connection, authentication, authorization, validation, and rate-limit failures through public operations. | Feedback distinguishes each failure and identifies its session and operation without exposing credentials or Secret values. | Planned check; implementation review pending. |
| ERR-003 | Temporarily fail a read with a connection error, then restore service. | Recovery uses the bounded backoff policy and applicable request limits rather than immediate repeated calls. | Planned check; implementation review pending. |
| ERR-004 | Fail authentication for an expired token, then allow sync ticks, queued reads, tab activation, inspector opening, and ordinary refresh to occur. | No automatic credential retry or additional login-browser invocation occurs for the suspended context; available cached presentation remains usable. | Planned check; implementation review pending. |
| ERR-004, ERR-007 | Fail authentication in one session while another session has working independent credentials. | Only the affected credential context is suspended; the independent session remains usable. | Planned check; implementation review pending. |
| ERR-005, ERR-009 | Request authentication retry, then cancel or dismiss its confirmation. | The prompt identifies the session and possible interactive login; no authentication attempt starts without confirmation. | Planned check; implementation review pending. |
| ERR-005, ERR-006 | Confirm one authentication retry and fail or cancel the resulting login while background work remains queued. | The confirmed attempt may invoke login, but no further attempt or browser launch occurs without renewed confirmation. | Planned check; implementation review pending. |
| ERR-005, ERR-006 | Multiple consumers request recovery for the same suspended credential context and one retry is confirmed. | Consumers do not independently launch duplicate authentication attempts under that single confirmation. | Planned check; implementation review pending. |
| ERR-007 | Reject an operation for authorization or validation while unrelated operations remain available. | The rejected operation does not endlessly retry; unrelated functions remain usable. | Planned check; implementation review pending. |
| ERR-008 | Return the same failure across multiple permitted refresh attempts. | A persistent error status remains visible without a toast for each occurrence. | Planned check; implementation review pending. |
| ERR-009 | Activate retry during a rate-limit or backoff period. | The explicit request respects the active constraint. | Planned check; implementation review pending. |
| ERR-010, ERR-011 | Restore a failed service or complete a user-confirmed login, then successfully check the affected operation. | Data refresh preserves applicable selection and reading position; the failure clears only after that successful check. | Planned check; implementation review pending. |
| ERR-011 | Start a retry or open a login browser without completing a successful check. | The affected failure is not falsely presented as resolved. | Planned check; implementation review pending. |
| ERR-012 | Recover read connectivity after an earlier uncertain apply or deletion. | Recovery does not resend the earlier write. | Planned check; implementation review pending. |
| ERR-013 | Expire credentials for a session supporting browser-based renewed login and view its status. | A clearly visible reauthentication button is available for that session; it leads to confirmation rather than immediately opening the browser. | Planned check; implementation review pending. |
| ERR-014, ERR-011 | Start a confirmed login, leave it awaiting browser interaction, then complete or fail authentication. | The existing health/energy/loading indicator shows the known authentication state or available progress; it does not invent completion percentages or declare recovery merely because the browser opened. | Planned check; implementation review pending. |

## Confirmed Startup And Workspace Restoration Requirements

These requirements were confirmed on 2026-10-02. Implementation conformance has not yet been reviewed. Immediate cache presentation at startup and throughout app use remains owned by SYN-001, not a startup-only exception. Authentication controls and feedback remain owned by ERR-004 through ERR-006 and ERR-013 through ERR-014.

| ID | Required observable behavior |
|---|---|
| STR-001 | Workspace restoration MUST default to enabled. When enabled, restart MUST restore saved sessions, open session tabs, their order, and the active session. Users MUST be able to disable restoration in Settings for the next launch. Disabling MUST NOT close current tabs or forwards, delete saved sessions or their history/configuration, or discard saved filters and sorts. On the next launch, saved sessions remain available but no session tab is automatically opened. Explicitly reopening a saved session MUST remain possible. |
| STR-002 | On restart, Podlord MUST restore the saved window layout. |
| STR-003 | Session-specific filters MUST persist across restarts and be restored for their owning session. |
| STR-004 | Session/table-type-specific sort state MUST persist across restarts and be restored for its owning context. |
| STR-005 | Workspace restoration MUST NOT automatically initiate interactive authentication. If login is needed and supported, it MUST be available through the visible, confirmation-gated action in ERR-013. |
| STR-006 | Restarting Podlord MUST NOT automatically recreate port-forwards. |
| STR-007 | Restarting or restoring the workspace MUST NOT automatically apply YAML changes. |
| STR-008 | Settings MUST persist across app restarts. |
| STR-009 | Settings input MUST be validated before adoption. Invalid input MUST leave the previously valid effective value unchanged. |
| STR-010 | Saved sessions that are no longer reachable MUST remain visible with understandable status rather than be silently deleted. |

Column layout persistence remains owned by TBL-007. These requirements do not introduce disk persistence for resource caches or unapplied YAML drafts; whether usable cached content survives a process restart must be established from the existing cache lifecycle during the later review. Saved window-layout details and behavior when displays change remain for final scope reconciliation. Restoration is not authorization to replay operational side effects.

For STR-003 and STR-004, Resources and Events retain independent session-owned
filters and stable-column sort IDs, including NONE. The native persistence boundary
is described in [ADR 0027](../adr/0027-session-filter-and-sort-persistence.md).
Unavailable or invalid saved preferences MUST remain visibly failed rather than be
silently overwritten. Cached interaction remains usable; reloading saved views is
an explicit action that replaces local filters and sorts. Closing waits for pending
saves; on failure the user may stay or explicitly close without saving. Neither
restoration nor preference saving authorizes a Kubernetes write or login.

### Acceptance And Evidence

| Requirement | Public scenario | Expected result | Evidence status |
|---|---|---|---|
| STR-001, STR-002 | Save a workspace with multiple session tabs, a non-default order and active session, and a changed window layout, then restart with restoration enabled. | The saved sessions, open tabs, order, active session, and window layout are restored. | Native session restoration tests exist; complete window-layout and installed-app restart comparison remain open. |
| STR-001, STR-008, STR-009 | Disable restoration, continue using current tabs, restart, explicitly reopen a retained session, then enable restoration and restart again. Also submit malformed or conflicting saved settings. | Disabling applies only to the next launch; identities/history/configuration and view preferences are retained. Explicit reopen remains possible. Reenabled restoration resumes normal open-tab restoration. Invalid/conflicting settings are not silently overwritten. | `native.workspace_restore.*` covers retained catalog data, next-launch placement, explicit reopen, reenabling, malformed input, locks/conflicts, repeat use, actual checkbox, keyboard and narrow window. Saved view retention and full installed-app restart remain separate checks. |
| STR-003, STR-004 | Set different filters and table sort states in different sessions, restart, and return to each context. | Each context restores its own saved filters and sort state. | Planned check; implementation review pending. |
| SYN-001 | Restore a workspace while one cluster is slow or unavailable and another context has usable cached content. | Available cached content appears without waiting for all clusters; absent cache is not fabricated. | Planned check; implementation review pending. |
| STR-005, ERR-013, ERR-014 | Restore a session requiring browser-based login, then explicitly confirm its visible reauthentication action. | Startup does not automatically launch login; the confirmed action may start it, with authentication state visible in the existing indicator. | Planned check; implementation review pending. |
| STR-006 | Run a port-forward, close the app, and restart its saved workspace. | No local listening port is reopened automatically by restoration. | Planned check; implementation review pending. |
| STR-007 | Leave an unapplied YAML change and restore a workspace after restart. | Restoration sends no apply request. | Planned check; implementation review pending. |
| STR-008 | Change a valid setting, restart, and reopen Settings. | The saved value remains effective and visible. | Planned check; implementation review pending. |
| STR-009 | Submit invalid input for a setting that already has a valid effective value. | The invalid value is not adopted and the previous valid setting remains unchanged. | Planned check; implementation review pending. |
| STR-010 | Restore a saved session whose endpoint is no longer reachable. | The session remains visible with understandable failure status, without blocking usable contexts. | Planned check; implementation review pending. |

## Confirmed Keyboard And Accessibility Requirements

These requirements were confirmed on 2026-10-02. Implementation conformance has not yet been reviewed. They apply across the desktop UI, not just tables. Decision rationale is owned by [ADR 0013](../adr/0013-keyboard-and-accessible-desktop-interaction.md).

| ID | Required observable behavior |
|---|---|
| ACC-001 | Core operations MUST be reachable and executable using only the keyboard, including table interactions, inspector access, copying, and authentication actions. |
| ACC-002 | Keyboard focus MUST be clearly visible. |
| ACC-003 | Keyboard navigation MUST follow a comprehensible order through the current surface's controls. |
| ACC-004 | Opening a dialog MUST move focus into it; closing the dialog MUST return focus to its initiating control where that control still exists. |
| ACC-005 | `Escape` MUST cancel dismissible interactions without applying changes or silently discarding an unapplied YAML draft. Existing draft-discard protection remains governed by INS-012 through INS-015. |
| ACC-006 | Buttons, status indicators, and errors MUST expose understandable accessible names or descriptions to screen readers. |
| ACC-007 | Information MUST remain understandable without distinguishing colors. Textual identity requirements in TBL-009 remain authoritative for colored table content. |
| ACC-008 | Reduced-motion operation MUST disable non-essential animations without suppressing the information they communicate. |
| ACC-009 | With enlarged text or a small window, content and actions MUST remain reachable rather than be irretrievably clipped or hidden. |

Exact platform/assistive-technology coverage, enlarged-text settings, and minimum supported window dimensions remain to be established during scope reconciliation and the later public-UI review. These confirmed behaviors do not constitute a legal-compliance or platform-certification claim.

### Acceptance And Evidence

| Requirement | Public scenario | Expected result | Evidence status |
|---|---|---|---|
| ACC-001, ACC-002, ACC-003 | Use only the keyboard to navigate a resource table, open the inspector, copy a value, and reach the session authentication action. | Each operation is reachable and executable with visible focus and comprehensible navigation order; authentication still requires confirmation. | Planned check; implementation review pending. |
| ACC-004 | Open and close an apply, deletion, or authentication confirmation while its initiating control remains present. | Focus enters the dialog and returns to the initiating control when it closes. | Planned check; implementation review pending. |
| ACC-005, INS-012, INS-013 | Use `Escape` to dismiss a preview or a discard-or-stay prompt while an unapplied YAML draft exists. | No change is applied, the draft is preserved, and a dismissal does not silently complete an action that would discard it. | Planned check; implementation review pending. |
| ACC-006 | Inspect buttons, the health/energy/loading indicator, authentication state, and errors using a supported screen reader. | Their purpose and state are understandable through accessible names or descriptions without requiring visual inspection. | Planned check; implementation review pending. |
| ACC-007, TBL-009 | Inspect statuses and identities without relying on color distinctions. | Their meaning remains available through text or other non-color identification. | Planned check; implementation review pending. |
| ACC-008 | Enable reduced motion and trigger normally animated status or alert presentation. | Non-essential animation is disabled while the relevant status or alert remains understandable. | Planned check; implementation review pending. |
| ACC-009 | Increase text size or reduce the window size and operate tables, Settings, the inspector, and confirmations. | Content and required actions remain reachable through the supported layout and navigation rather than inaccessible clipping. | Planned check; implementation review pending. |

## Confirmed Performance Acceptance Requirements

These targets were confirmed and tightened on 2026-10-02. The original 200 ms inspector, 2-second startup, and 500 MB RSS limits are superseded by the requirements below. They are acceptance requirements, not recorded benchmark results. Decision rationale is owned by [ADR 0014](../adr/0014-separate-ui-performance-budgets-from-infrastructure-latency.md).

| ID | Required observable behavior |
|---|---|
| PER-001 | Cached filtering and sorting MUST produce a visible response within 100 ms at the 95th percentile across repeated runs. Cached tab-switch timing is owned by PER-010. |
| PER-002 | Opening an inspector with available cached content MUST make that content visible within 50 ms at the 95th percentile across repeated runs. |
| PER-003 | App startup MUST become interactive within 1 second in the agreed reference profile, without waiting for cluster connections or login. |
| PER-004 | Scrolling and interactions MUST NOT exhibit reproducible UI stalls longer than 100 ms. |
| PER-005 | Idle CPU use, with no synchronization in progress, MUST average no more than 2 percent of one CPU core over a 60-second measurement window. |
| PER-006 | Process resident memory (RSS) MUST remain at or below 250 MB in the agreed performance profile. |
| PER-007 | Repeated opening and closing after warm-up MUST NOT cause continuing process-memory growth. |
| PER-008 | UI performance acceptance MUST distinguish local cached presentation from infrastructure response latency. Slow Kubernetes or custom-resource responses MUST NOT be counted as cached-rendering latency or used to prevent immediate cached presentation. |
| PER-009 | Exceeding a UI performance target MUST NOT cause an infrastructure request to be cancelled or impose that target as a request deadline. Dispatched-request handling remains governed by SYN-007. |
| PER-010 | Cached tab switching MUST produce a visible response within 50 ms at the 95th percentile across repeated runs. |
| PER-011 | Local UI processing per frame MUST take no more than 8 ms at the 95th percentile in the agreed reference profile. This is distinct from action-to-visible-content latency and external waiting time. |
| PER-012 | A release download MUST be no larger than 50 MB per platform/architecture target, including required runtime Qt libraries and other runtime dependencies but excluding separately distributed debug symbols. |
| PER-013 | The installed release footprint MUST be no larger than 100 MB per platform/architecture target, including required runtime Qt libraries and other runtime dependencies but excluding separately distributed debug symbols. |

The original 200 ms target measured the whole cached-inspector opening interaction, not only painting. Its replacement is the confirmed 50 ms p95 action-to-visible-content target. Local UI frame work has the separate 8 ms p95 budget in PER-011 and must not be mislabeled as whole-interaction latency. The language change itself is not performance evidence.

### Measurement Profile And Boundaries

Use a release build on the user's Mac with three sessions containing 5,000 cached resources in total and one visible log view using the configured default 5 MB retention limit. Record hardware, operating system, app version, actual resource composition, and cache state with benchmark evidence; absent measurements must not be replaced with invented figures. The profile describes the target workload, not an assertion that it has already been provisioned.

Measure interaction response from public user action to visible response, not dispatch of a task or a private helper returning. Use repeated runs and the 95th percentile for PER-001, PER-002, and PER-010. Measure PER-011 during real public UI interactions with frame profiling; it is the local processing budget, not the time spent waiting for the next display refresh. Startup is measured to the usable UI, not full cluster synchronization. CPU percentages are normalized to one core; RSS is process resident memory, not only managed allocations. Account for UI/background work separately from external API latency rather than silently omitting local contention.

Other device groups require explicitly recorded performance profiles rather than an unsupported claim that the Mac measurements prove equivalent performance everywhere. Package-size limits apply per target and include the dependencies actually needed on that target; moving a required library to a separately downloaded prerequisite must not hide its footprint. Record artifact compression, measurement units, and installed-footprint method with size evidence.

Warm-up duration, repeat count, percentile calculation, the operational definition of a reproducible stall, and memory-growth tolerance must be recorded in the repeatable benchmark protocol before claiming a pass. PER-007 concerns continuing growth after warm-up, not a claim that every allocation immediately reduces RSS on close. The CPU idle check excludes active sync but must not hide unintended refresh activity by labeling a busy app idle.

Infrastructure calls may legitimately take longer, including discovery or retrieval of custom resources. These UI budgets neither introduce transport timeouts nor remove independently justified transport failure handling. Authentication suspension and uncertain write-outcome safeguards remain unchanged.

### Acceptance And Evidence

| Requirement | Public scenario | Expected result | Evidence status |
|---|---|---|---|
| PER-001 | Repeatedly change filters and sort states in the agreed profile. | Public-action-to-visible-response p95 is at most 100 ms for each interaction class. | 2026-10-09 Cocoa/Metal Release run: filter 43.16 ms, sort 33.02 ms. Scoped pass; see the current [measurement ledger](k3d-test-map.md#2026-10-09-native-catalog-refresh-and-release-measurements). |
| PER-002 | Repeatedly open cached inspector content in the agreed profile. | Public-action-to-visible-content p95 is at most 50 ms. | Same run: 34.22 ms. Scoped pass, not all-device clearance. |
| PER-003 | Start the release app while cluster connections or login are delayed. | The app is usable within 1 second without waiting for infrastructure readiness. | Planned benchmark; not measured. |
| PER-004 | Scroll and operate the public UI repeatedly in the agreed profile. | No UI stall longer than 100 ms is reproducible under the recorded protocol. | Planned benchmark; not measured. |
| PER-005 | Observe the app for 60 seconds after settling, without active synchronization. | Mean process CPU is no more than 2 percent of one core. | Same run: 60.001 seconds, 1.62 percent of one core, foreground retained. Scoped pass. Earlier interrupted windows are not accepted idle evidence. |
| PER-006, PER-007 | Warm the agreed workload and repeatedly open and close views while recording process RSS. | RSS remains within 250 MB and does not continue growing under the recorded repetition protocol. | Same run: peak RSS 275,038,208 bytes, failing PER-006. Warm growth 3,293,184 bytes stays below the recorded 5 MB tolerance; broader leak/lifecycle evidence remains separate. |
| PER-008, PER-009, SYN-001 | Delay a dispatched Kubernetes custom-resource response beyond the UI budgets while usable cached content exists, then eventually return the response. | Cached presentation remains responsive; the request is not cancelled because of the UI budget and its eventual response is processed in its original context. | Planned check; implementation review pending. |
| PER-010 | Repeatedly switch between cached sessions in the agreed profile. | Public-action-to-visible-response p95 is at most 50 ms. | Same run: 49.82 ms. Scoped pass with little margin; repeated-run and other-device stability remain to be established. |
| PER-011 | Profile frames while scrolling, switching views, and operating the actual public UI in the agreed profile. | Local UI processing per frame p95 is at most 8 ms, with the profiling method and external/display waiting distinguished. | Planned benchmark; not measured. |
| PER-012, PER-013 | Package and install a release for each claimed platform/architecture target, counting required runtime dependencies. | Download is at most 50 MB and installed footprint is at most 100 MB; separately distributed debug symbols are excluded transparently. | 2026-10-09 macOS arm64 private package: ZIP 32,459,039 bytes; installed regular-file logical sum 85,883,451 bytes. Both size targets pass for this artifact only. |

## Confirmed C++ Rewrite Direction

This direction was requested on 2026-10-02. It changes the intended implementation language, not the already confirmed product behavior. Rationale and unresolved architecture choices are owned by [ADR 0015](../adr/0015-cpp-rewrite-for-performance-and-device-coverage.md).

| ID | Required direction |
|---|---|
| RWT-001 | After completing the specification interview, Podlord MUST be rewritten in C++. This direction does not authorize starting implementation before the remaining architecture and scope decisions are settled. |
| RWT-002 | The rewrite MUST aim for broad device coverage with minimal platform-specific wrappers. Platform families and Qt are confirmed in RWT-004 through RWT-006; exact OS/architecture combinations and the Qt UI approach remain to be established. |
| RWT-003 | The rewrite MUST retain the confirmed product contracts and establish actual public-boundary correctness and performance evidence rather than assume that changing language proves conformance. |
| RWT-004 | The target platform families MUST include macOS, Windows, Linux, Android, and iOS, covering desktop computers, phones, and tablets with the broadest practically supportable device coverage. Exact OS-version and CPU-architecture support MUST be documented and evidenced before release claims. |
| RWT-005 | Podlord MUST be a locally installed application, not a browser application. Its application UI MUST NOT depend on embedded browser rendering, Qt WebEngine, Qt WebView, or Chromium. Opening an external browser for explicitly confirmed authentication remains permitted under ERR-005 and ERR-013. |
| RWT-006 | The replacement application MUST use C++ and Qt Quick with a thin QML presentation layer. Data models, cache, Kubernetes integration, and business logic MUST be owned by C++, not duplicated as a second application implementation in QML. |
| RWT-007 | The distributed replacement MUST NOT require a C#/.NET runtime or a permanent .NET companion service to implement core behavior. |
| RWT-008 | Release packages MUST include only the runtime modules, plugins, and resources justified by supported features. Package size MUST be measured per target rather than inferred from the implementation language. |
| RWT-009 | Before replacement changes can invalidate the reference, Podlord MUST preserve an identifiable, reproducibly buildable old application baseline and its runnable release artifact with recorded version and artifact identity. That reference MUST remain available for comparison after retirement from the production release. |
| RWT-010 | During the rewrite, the old and new applications MUST be independently buildable and launchable on a common supported comparison platform without one installation replacing the other. |
| RWT-011 | Old/new comparison MUST isolate each application's local settings, caches, logs, workspace state, and listening ports so neither corrupts or silently migrates the other's data. Migration tests MUST use explicitly designated copies. |
| RWT-012 | The existing test map MUST account for every inventoried shipped function with its public entrypoint, expected contract, old/new build identities, applicable scenario/test, outcome, and remaining gap. A function without an assigned disposition MUST block claims of complete parity. |
| RWT-013 | Comparison MUST drive equivalent public scenarios from equivalent preconditions. Reads MAY use the same test cluster; mutations MUST use isolated test-owned resources or a restored equivalent initial state so the first application does not invalidate the second application's scenario. |
| RWT-014 | Differences MUST be evaluated against the confirmed specification, not blindly copied from old behavior. Known old bugs and explicitly approved changes or removals MUST have recorded dispositions; normalization MUST NOT hide meaningful differences in scope, authorization, errors, side effects, or retained state. |
| RWT-015 | The old production implementation MUST NOT be removed until required-function parity, migration, rollback, cleanup, and applicable correctness/performance/release evidence are established. Preserving an isolated test reference MUST NOT retain the old runtime as a dependency of the new production release. |
| RWT-016 | Visual equivalence is part of functional equivalence. Every retained workspace, inspector page and Settings section MUST remain reachable and usable with every shipped theme's dark/light variants. Theme selector and palette tests alone MUST NOT establish this requirement. Review populated old/new screenshots, keyboard operation, readable overflow and responsive layout; exact Avalonia pixels are not required. |
| RWT-017 | Every discovered comparison discrepancy MUST be mapped to its owning requirement or inventoried capability, with implementation and verification tracked separately. Missing menus or behavior MUST remain release blockers unless removal is explicitly approved. A partial feature package MUST NOT be described as a release-ready replacement. |

The current .NET/Avalonia application remains the implemented reference until replacement work is authorized and completed. Its existing technology ADRs describe that implementation, not mandatory dependencies of the future C++ application. C++/Qt and the platform families above are accepted; specific Qt modules, a project-license change, and universal mobile feature equivalence are not inferred. Platform capability differences, authentication integration, deployment obligations, and migration still require evidence. No browser version or browser backend is in scope.

Direct old/new UI comparison applies on platforms where the old application actually runs; it does not pretend an old mobile release exists. Other new targets must exercise the same applicable public behavior against the owning specification and explicit platform capability matrix. A preserved artifact alone is not a reproducible baseline: record required toolchains, dependencies, and build/run commands before relying on it.

Test cleanup is mandatory on success, failure, interruption, and partial setup. Stop and remove task-owned ephemeral containers and other temporary resources without pruning unrelated resources; retain at most the latest task-owned test image where needed. Existing coverage gates remain 95 percent lines and 90 percent branches; percentage gates do not replace behavioral parity evidence.

### What Must Be Closed Before Native Release?

RWT-016/017 record the visual/functional completion contract requested on
2026-10-03. The populated desktop comparison exposed omissions below; none is
an approved scope reduction. The capability inventory owns legacy routes; the
[test map](k3d-test-map.md#what-did-the-populated-desktop-comparison-on-2026-10-03-establish)
owns captures, build identities, executed outcomes and remaining verification.

| Required completion | Owning contract/capability | Acceptance boundary |
| --- | --- | --- |
| Tables: semantic sorting, saved hide/reorder/pin/width controls, full-value access and stable refresh | TBL-001 through TBL-012; LEG-015 | Operate each applicable table, restart its layout, refresh while reading and copy without opening the inspector. |
| Filters, presets, searches and per-session state | FLT-001 through FLT-010, STR-003/004; LEG-012 through LEG-014 | Exercise retained filter operators on populated cache; switch/restart sessions without leaking filters or selection. |
| Radar selection, pointer/keyboard navigation, pan/zoom/reset and filtered membership | FLT-003 through FLT-010, ACC; LEG-025/026 | Table/radar/counts agree beyond viewport size, including no matches; navigate cached identities. Retained effects require reduced-motion and hidden-work checks. |
| Structured inspector, readable YAML/values/logs, Events, Links and history | INS, SEC, LOG, ACC; LEG-017/020/021/022 | Real long content, nonempty relations and Events; preserve reading position, fresh-only editing and Secret masking. |
| Events workspace and event-to-resource navigation | INS, TBL, FLT; LEG-016 | Show real Events and navigate actual targets without guessing a replacement resource by name. |
| Confirmed operational actions and live session-owned port-forwards | ACT, SES-020 through SES-025, ERR; LEG-019/023 | Actual writes/traffic, failures/uncertain outcomes, tab/window close and independent port release. Empty Ports screenshots are insufficient. |
| CPU/memory/storage usage and reference visualization | MET-001 through MET-011; LEG-024 | Real source values and labeled markers; missing/stale/incomplete usage and recommendations are not invented. |
| Alerts, sounds and mute | ALT-001/002 below, FLT-009, ACC-008; LEG-027 through LEG-030 | CRUD, matcher evaluation, real local playback, no tab-switch replay, reduced motion and hidden-work ownership. |
| Appearance, synchronization, Alerts, Diagnostics, Graphics, Privacy, Sources, Workspace and About Settings | STR-008/009, SYN-011, RWT-016; LEG-031 through LEG-035 | Operate each retained section with populated data in every theme; validation, failed saves and restart preserve valid values. |
| Source/session management, import channels, windows and restoration | SES, STR; LEG-001 through LEG-010/032 | Public workflows, changed snapshots, independent windows, lifecycle and restart; no silent interactive login. |
| Localization, command palette, shortcuts, update checking and links | ACC, STR, ERR; LEG-033 through LEG-037 | Retained routes/options, language fallback, safe commands, failure states and deliberate external actions. |
| Cache/request lifecycle, performance, migration/rollback, packaging and device coverage | SYN, ERR, PER, RWT-004 through RWT-015 | Public-boundary measurements, enforced coverage gates, clean-device install/run and OS/architecture/license/signing evidence per claimed target. |

All 19 themes, dark/light and subtle/medium/arcade choices remain required;
they are not replaced by a new visual direction. Kubernetes operation must not
be disguised as a `kubectl` wrapper. Test tools are not shipped prerequisites.
Mobile support is not established by a macOS build or toolkit support. Graph
is the approved complete removal, not a native workspace or retained dead code.

### Which Resources Do Alert Rules Evaluate?

Confirmed on 2026-10-03 in supplementary question 1/3:

- **ALT-001:** Alert rules MUST evaluate all available resources in their owning
  session's configured synchronization scope, independently of presentation
  filters. Applying or clearing a table/radar filter MUST NOT suppress an alarm.
- **ALT-002:** Filtered radar/table presentation remains governed by FLT-003.
  Whole-session alert evaluation MUST NOT insert nonmatching resources into that
  presentation, leak alarms across sessions or replay known alarms on tab switch.

Acceptance: create matching healthy/problem resources in one session, filter
the problem resource out, and change its state through normal synchronization.
Its rule still evaluates and reports an alarm; table/radar/counts retain only
filter matches. Switching sessions does not replay the existing notification.
Rule operators, defaults, sound attribution and remaining alert semantics must
still be reconciled with LEG-027 through LEG-030 before their implementation.

### Acceptance And Evidence

| Requirement | Public scenario | Expected result | Evidence status |
|---|---|---|---|
| RWT-003, RWT-004 | Build, install, and exercise the agreed public workflows on each claimed OS/architecture target. | Release claims match tested platform support and confirmed behavior; unverified targets are not presented as supported. | Planned check; implementation not started. |
| RWT-005, RWT-007 | Install and run a replacement release artifact on a clean supported device. | The local UI works without embedded browser rendering, a .NET runtime, or a .NET companion service. Explicitly confirmed external authentication remains available where supported. | Planned check; implementation not started. |
| RWT-008, PER-012, PER-013 | Inspect and measure release artifacts per target. | Only necessary runtime dependencies are included; actual download and installed sizes satisfy the confirmed per-target limits. | Planned packaging check; not measured. |
| RWT-009, RWT-010, RWT-011 | Build and launch the recorded old baseline and new application on the comparison host, then change their local settings independently. | Both remain runnable and their local state and artifacts stay isolated. Repeat after production retirement using the preserved reference. | Planned check; comparison runners not yet implemented. |
| RWT-012 | Reconcile the shipped-function inventory against requirements and the old/new test ledger. | Every function has an explicit disposition and public evidence or a visible gap; unmapped functions block complete-parity claims. | Planned inventory; not yet established. |
| RWT-013 | Run equivalent read and mutation scenarios through each application's public boundary. | Reads observe equivalent cluster scope; mutations use isolated or reset preconditions and do not contaminate the other application's result. | Planned comparison checks; not executed. |
| RWT-014 | Compare a known old bug, an approved removal, and a regression in a required behavior. | The bug is not preserved as intended behavior, the approved removal is explicit, and the required-behavior regression remains a blocking discrepancy. | Planned comparison checks; not executed. |
| RWT-015 | Evaluate retirement while parity, migration, or release evidence is missing, then after required evidence is complete. | Missing evidence blocks removal; completed replacement can exclude the old runtime while retaining a separately usable test reference. | Planned retirement gate; not evaluated. |

## Safety Model

### Inspector History Migration Evidence

The native inspector now provides session-local Back/Forward controls, native
keyboard bindings, a 32-visit bound, consecutive deduplication and the C#
reference's retained forward branch. Navigation uses current collection-cache
membership independently of display filters; old detail-only entries are not
treated as current resources. The existing YAML discard decision binds the
exact target, and fresh reads remain subject to the confirmed request/auth rules.
See [ADR 0013](../adr/0013-keyboard-and-accessible-desktop-interaction.md#how-does-inspector-history-preserve-its-target)
and the inspector-history entry in the test map. This records a scoped migration
increment, not complete Inspector, accessibility or release parity.

- Never silently acts on a different session than the visible one.
- Never depends on the user's current shell kube context.
- Does not mutate original kubeconfig files during import.
- Stores kubeconfig copies as app-managed snapshots.
- Applies the confirmed Secret presentation requirements above.
- Uses the confirmed deletion safeguards above for supported resource deletion operations; other destructive actions still require explicit user actions.
- Keeps diagnostics useful without dumping sensitive kubeconfig content.

## Audit Trail: What Happens And Why

| Action | What Podlord Does | Why |
|---|---|---|
| Import kubeconfig | Validates, snapshots, hashes, deduplicates, creates sessions | Stable app-owned sources without changing user files |
| Source file changes | Creates a new snapshot for changed content and preserves existing session bindings | Keep changed configuration available without silently retargeting active work |
| Select session | Restores cached rows, filters, radar state, and open tab state | Fast context switching without losing work |
| Refresh data | Queues Kubernetes API calls with pacing, TTL, and backoff | Avoid API spam while keeping data current |
| Display resources | Reads from cache and applies local filters immediately | UI stays responsive during sync |
| Open resource | Shows cached detail first, then requests fresh detail if needed | Immediate feedback with better accuracy shortly after |
| Open logs | Fetches logs only for log-capable resources and active log views | Avoid hidden background work |
| Port forward | Uses native Kubernetes port-forward stream handling | Cross-platform behavior without `kubectl` dependency |
| Trigger alert | Matches resource state, applies color/animation/sound/zoom rules | Make important changes visible without manual scanning |
| Switch tabs | Preserves alert state per session and avoids replaying old alerts | Context switches should not create false activity |
| Show diagnostics | Records request status, timing, queue state, cache size, and memory | Explain what the app is doing when users need proof |

## Non-Goals

- Not a namespace tree browser.
- Not a shell wrapper.
- Not a Grafana clone.
- Not a Helm or GitOps manager yet.
- Not a multi-user control plane.

## Product Principle

Podlord should make the operator feel:

- I know which cluster I am touching.
- I know what changed.
- I know what is unhealthy.
- I know what data is cached or loading.
- I can act faster without becoming more dangerous.

### Local YAML bounds and user-entered Secret values

- **INS-029:** Settings provides a positive whole-number MiB limit for local YAML documents, expanded JSON and prepared patches, default **3 MiB**. Oversized input fails explicitly before dispatch. This limit does not authorize cancellation of requests already sent.
- **SEC-009:** Secret values explicitly entered by the user may be visible in the local YAML draft. Secret values in YAML loaded from Kubernetes, all comparisons and all diff previews remain masked. Fresh YAML shown after successful Apply is masked again. Unchanged masked values retain their original bytes; a new placeholder without an original value is rejected. Explicit individual reveal/copy in the values surface remains governed by the existing Secret rules.

Secret `stringData` follows the [Kubernetes API contract](https://kubernetes.io/docs/reference/kubernetes-api/core/secret-v1/): UTF-8 values override matching `data` keys and are written in canonical base64 form. Read-back verifies the canonical values without requiring the write-only `stringData` field to be returned.

### How Are Event API Aliases Presented?

- **RWT-018**: Core `v1/Event` and `events.k8s.io/v1/Event` representations with the same Event UID, namespace and name represent one resource in the session presentation. Prefer the modern representation when both are cached; retain the core representation when it is the only available one. Preserve each API path in the authoritative cache for explicit detail access. Different Event UIDs remain distinct, even when they concern the same resource. Tables, radar, related Events and session-wide alarm evaluation consume the same canonical resource projection.

- **RWT-019**: Passive synchronization must not move workspace navigation, filters or resource content when the loading indicator appears or disappears. Keep the indicator's layout allocation stable and disable indeterminate animation when there is no work. A loading state is not permission to change the user's click target.

Event normalization reads `series.lastObservedTime`, then the API's last-timestamp compatibility field, then `eventTime`, then its first-timestamp compatibility field. Empty or null fields do not suppress a later available timestamp. This covers modern Event representations produced from older Core Events; see the [Kubernetes Event API](https://kubernetes.io/docs/reference/kubernetes-api/events/event-v1/).

## Which table and release requirements have executable evidence?

As of 2026-10-03, TBL-004 through TBL-007 have scoped native implementation and
public-entrypoint evidence for the Resources and Events tables: show/hide, pin,
order, width, reset, independent persisted layouts, atomic asynchronous Save and
explicit conflict/validation/lock errors. This is partial fulfillment of the
all-table requirements, not completion of those requirements across the product.
The persisted schema uses each real table model's stable header IDs. At least one
column remains visible; a pinned column must also be visible. Invalid or newer
stored documents are reported and preserved rather than silently overwritten.

The [test map](k3d-test-map.md#which-native-table-layout-and-package-checks-passed-on-2026-10-03)
records the exact public scenarios, failures, remaining gaps, complete-suite run,
coverage gate and packaged-binary screenshot identities. All 19 themes have
scoped dark/light screenshots for Resources, Events and both column dialogs;
that evidence does not waive all-view, all-intensity, accessibility, function or
platform requirements. CLI catalog timings are not UI-rendering measurements.

The release definition is unchanged. A passing test count does not satisfy the
90% branch gate, package size, full capability parity, mobile authentication,
platform/device verification, licensing or distribution/signing requirements.
The earlier Homebrew-based macOS package exceeded the 100,000,000-byte limit.
The 2026-10-06 portable startup-import candidate measures 80,742,424 installed
regular-file bytes and 31,322,650 ZIP bytes; its identity and scoped evidence are
recorded in the test map. All nineteen themes remain present. This size result
does not close signing, runtime-license/source delivery, clean-device or mobile
distribution gates. ALT-001/ALT-002 still require alarm evaluation over the entire
session cache independently of display filters; table/theme evidence alone does
not establish that behavior.

Source-import success information is available when Sources is expanded or an
empty workspace needs onboarding; it does not permanently occupy an active
collapsed workspace. Per-file import failures remain visible with their details
action even when Sources is collapsed. A direct invalid-path failure remains a
direct error, not a fabricated per-file import report. This is scoped evidence for
the disclosure behavior in ADR 0026, not completion of all Sources settings.

### Which Native Radar Animation And Hover Lifecycles Are Established?

Radar presentation uses the existing filtered session snapshot. Its kind glyph,
selection, status color and alarm membership are unchanged by presentation
suspension. A delegate outside the viewport must stop
its local blink, pulse and sweep work; returning it to the viewport resumes an
applicable effect. Static outline and no-animation modes must not schedule
continuing frames. Sweep runs only while its own effect is visible, not merely
while the containing resource item exists. Reduced-motion handling remains in
the same presentation owner; background alarm evaluation still covers the whole
session under ALT-001.

The single shared Radar tooltip now reuses the inspector/dashboard metric gauge.
It displays cached CPU, memory and storage presentation, observed units,
request/limit/capacity markers when available, denominator labels, incomplete or
stale annotations, and measurement source/time/window. Long usage labels wrap
within the available width; the bar occupies its own flexible row instead of
competing with labels. Tooltip gauges exist only while the tooltip is shown.
Hover, focus, scrolling and painting do not request measurements. An absent
storage usage value remains unavailable. Configured references are not presented
as recommendations; MET-010 and MET-011 still require a substantiated source for
any recommendation marker.

Public regression and real local Kubernetes evidence, including its boundary and
remaining parity/performance gaps, are owned by the existing
[test map](k3d-test-map.md). This establishes the described presentation increment,
not spatial Radar parity, all-theme comparison or release readiness.

### Which Boundary Establishes Automatic Log Cadence?

LOG-009 measures automatic cycle admission at the local request scheduler, using
its existing monotonic clock and unchanged 3,000 ms guard. Public `requestStarted`
metadata exposes originating session, canonical API path and that timestamp, not
query parameters, authorization or payload. This event does not fetch, persist or
retry anything. Server ingress may have different transport/event-loop delays;
it is not the scheduler clock. The public log scenario still verifies real HTTP
requests and visible retained contents. This is diagnostic-boundary clarification,
not authorization for faster polling or a claim of a complete request-audit UI.
Rationale is owned by [ADR 0029](../adr/0029-measure-request-cadence-at-local-admission.md).

An unavailable quantity must not inherit a timestamp/window from another observed
quantity on the same resource. A genuine observed zero retains its own measurement
metadata. This is owned by the common metric formatter, so Radar, Inspector and
Dashboard use the same distinction between configured references and observed
measurements. The gauge's configuration-source caption describes references;
measurement-provider parity remains separate from that caption.

### Which Native Resource Deletion Boundary Is Established?

Native single-resource deletion implements ACT-001, ACT-003 through ACT-010 at
the inspector. The confirmation freezes cluster, session, namespace or explicit
cluster scope, API kind/name and UID. Cancel is initially focused; no name typing
is required. Discovery must advertise `delete`, and a listed UID is required.
Server RBAC remains authoritative. An active local YAML draft disables deletion.

A confirmed ordinary DELETE carries a UID precondition. It neither force-deletes
nor changes grace periods, propagation defaults or finalizers. Selection changes
cancel unsent confirmation; navigation cancels a queued deletion. A dispatched
deletion and its read-back finish in their originating scope, including after
inspector or session-tab closure. No request is automatically replayed.

Acknowledgement, observed absence, continued presence, termination, replacement
UID and unavailable observation are separate results. Lost or malformed replies
and server failures remain explicitly uncertain even if absence is later observed.
A read failure blocks another deletion until a successful current-target read;
changing inspected resources cannot erase this guard. Retrying is an explicit
new confirmation, not a retry button that reuses old authorization.

Confirmed absence removes older retained generations at the exact originating
path, using the observation's monotonic request-admission time. It must not
resurrect a still-older same-name row or erase newer observations or other paths.
Render and hover paths remain cache-only; deletion and read-back are explicit
operational actions. The existing request queue owns priority, rate control, TLS,
manual authentication recovery and cleanup.

Multi-resource selection/deletion (ACT-002) remains an open native capability.
This boundary is not a claim of complete action, inspector or release parity.
Evidence and outstanding gates remain in the existing Kubernetes test map.

## How do native source dialogs preserve file identity?

File/folder dialog results cross the source-import boundary as file URLs, without
presentation-side prefix stripping or percent decoding. The source owner converts
them once to local filesystem paths. The resulting snapshot identity, import
report, authentication behavior and ownership rules are identical to importing
that path directly. Invalid URL metadata must fail explicitly instead of silently
selecting another file. Focused macOS CLI and visible-UI evidence is recorded in
[the test map](k3d-test-map.md#does-source-import-accept-paths-and-file-urls-at-one-public-boundary);
OS-native chooser interaction and actual additional-platform proof remain open.

## What is required of the shared native application shell?

Confirmed direction: match the existing application layout first, then complete
functions in individually evidenced increments. Layout alone does not close a
capability or release gate. [ADR 0033](../adr/0033-native-reference-shell-and-cache-summary.md)
records presentation ownership and non-duplicated cache summary semantics.

| ID | Observable requirement |
| --- | --- |
| SHL-001 | Primary navigation exposes Resources, Events, Ports and Settings. Alerts belong to Settings; usage details open from header cards. No separate Radar workspace or dead navigation route remains. |
| SHL-002 | Wide windows retain one right-hand Radar/filter sidebar while changing the central workspace. Narrow windows expose that same surface through an accessible drawer, without duplicating camera or filter ownership. |
| SHL-003 | CPU, Memory, Storage, Pods and Nodes summarize the filtered cached snapshot. Missing measurements remain unavailable; capacity is only a reference. Node/Pod and claim/volume quantities are not double-counted. Staleness and partial coverage are disclosed. |
| SHL-004 | Source/session controls, explicit authentication, table controls and the resource inspector retain their real producers and existing safeguards. Navigation, hover, scroll and painting do not perform Kubernetes requests. |
| SHL-005 | Missing functions are explicitly disabled and explained, not simulated. Settings navigation remains reachable when the inspector narrows the central pane. |
| SHL-006 | Ports lists only forwards owned by the active session. Search and copying are cache-only. Inspect targets the owned resource; Stop releases the selected forward. Unknown/inactive tokens cannot overwrite the clipboard. Closing the owner still stops its forwards. |
| SHL-007 | The inspector MUST dock below the primary workspace, preserving its available column width and the right-hand Radar/filter sidebar. Its height MUST be adjustable through a native split handle. Closing it MUST restore the workspace height without resetting cached filters or requesting resources. Narrow windows MUST retain reachable inspector actions through wrapping controls. Moving the dock MUST NOT change authentication, Secret masking, fresh-only YAML editing, write confirmation or port-forward ownership. |
| SHL-008 | Resource, Event and Port search fields are revealed on demand. Closing search or pressing Escape retains the cached query and its results; the search toggle indicates a retained active query. Queries remain independent across workspaces and sessions. |
| SHL-009 | Routine synchronization remains automatic. Explicit refresh and commands belong in the workspace menu, not a permanent text-button toolbar. Filter/camera reset and table tools use vector icons with accessible names, visible focus and tooltips. Authentication and real error recovery remain explicit and reachable. |
| SHL-010 | The shell footer is one fixed-height line; long names/statuses elide rather than wrapping or increasing height. At 320/390 logical pixels navigation and primary icon actions have at least 44-by-44 touch targets. The same Radar stays above a scrollable filter surface in the narrow drawer. Phone-sized Qt rendering does not establish an iOS/Android release. |
| SHL-011 | The tab strip exposes a quick file-open action and an anchored dropdown for usage-ranked saved sessions and imported contexts. File acceptance depends on kubeconfig content, not a suffix. Invalid, missing and unreadable files do not import or change the selected session, cache, filters or notices. Valid import never implicitly activates a context, executes a credential command or opens authentication. Profile/write failures remain visible. Session/context selection retains the existing draft-discard and authentication safeguards. |
| SHL-012 | Compact landscape windows (at least 600 logical pixels wide and less than 600 high) use the same navigation actions in a left-hand scrollable icon rail, with 44-pixel targets. The existing Radar remains docked at the upper right; filters start collapsed and can be expanded without clearing active filters. Rotation and disclosure preserve session, query, cache and Radar pose without issuing requests. Portrait retains the drawer; no orientation lock or second UI is introduced. |
| SHL-013 | The single-line footer matches the reference categories: visible/cached resource counts, admitted API requests in the preceding minute for the selected session, and age of its latest accepted collection snapshot (seconds/minutes/hours, or never). Enqueued requests and rejected local operations are not counted. Failed reads do not create a fresh snapshot timestamp. Errors remain visible separately, with full accessible text and a tooltip. Footer rendering does not schedule transport. |

The current implementation does not establish complete mobile layouts, complete
Settings or all-theme visual equivalence. Existing broad portability and release
gates remain unchanged. Matching every pixel is not a reason to reintroduce
incorrect unknown-as-zero or duplicated cluster usage calculations.

### How Does Loading Affect Health, Activity And Alerts?

The initial session sync establishes a silent baseline. Loading does not produce
new-resource effects, sounds or automatic alarm focus. Once loading finishes,
existing problems are shown with their normal severity, without announcing the
initial population. Subsequent additions and changes may trigger the enabled
rules. Partial cached resources remain usable while collections arrive.

| ID | Requirement |
| --- | --- |
| LOAD-001 | Collection loading MUST suppress alert effects, notification sounds and automatic alarm focus. The first completed sync MUST establish a silent baseline rather than classify the initial population as new. |
| LOAD-002 | The health strip MUST fill from bottom to top only during a session's initial collection load, monotonically until admitted collection work and its discovery/pagination follow-ups have completed. After that baseline it MUST retain full height through passive sync, explicit refresh, cached reopen and authentication retry; only cached health proportions adjust. An uninitialized session or its incomplete/failed authentication MUST NOT claim completion. Authentication/loading status remains separately visible; retained full height is not proof that a later login succeeded. |
| LOAD-003 | Health segments MUST be ordered critical red at the top, warning yellow below it, and healthy green at the bottom. Their proportions use the complete session cache, independent of presentation filters. Unknown health before resources arrive MUST be neutral, not fabricated green health. |
| LOAD-004 | Independent reads MAY overlap within four active slots. Admission MUST retain the global minimum start spacing, configured hard limit and Retry-After. Writes and port-forward handshakes remain exclusive; dispatched work is never interrupted to prioritize another request. |
| FLT-014 | Problems and Activity MUST be mutually exclusive and combine with search and field expressions. Switching modes and resetting MUST operate entirely on the session cache and update table, radar and counts together. |
| FLT-015 | Activity MUST follow the reference statuses and recent change/creation semantics (15 minutes). Events use five minutes for normal/success activity and thirty minutes for warnings; future, Observed and Historical events are not activity. |
| FLT-016 | Named resource filter presets MUST be shared within the profile, while applying a preset affects only the active session. Save/overwrite, load, rename, delete and reload MUST be reachable controls. The empty default cannot be changed, renamed or deleted. |
| FLT-017 | Presets and per-session mode state MUST survive restart. Atomic private persistence MUST detect conflicting writers and retain malformed/newer documents instead of replacing them with defaults. |

Native presets capture search, implemented field expressions and Problems/Activity,
not table sort or radar camera. Session views version 5 preserve older supported
documents on read and upgrade only when a view actually changes. Saved-filter
import is not a complete C# profile import.

### How Must Saved-Filter Migration Behave?

- FLT-018: Import MUST be explicit and accept a regular local JSON file in the native or C# saved-filter format, within the existing 64 KiB bound. It MUST NOT access an ambient reference profile or modify the selected source.
- FLT-019: Import MUST preserve search, Problems/Activity and supported field expressions. Mutually conflicting modes, malformed fields, unsupported fields and case-insensitive name collisions MUST fail without partial replacement. Repeating the same named state MUST be idempotent; differing state MUST NOT silently overwrite an existing name.
- FLT-020: Import MUST use the existing asynchronous preset owner and private atomic persistence. Failure MUST preserve existing presets and report an error without exposing document contents. Import and applying the resulting filter MUST NOT initiate Kubernetes requests.

C# ApplyPreset clears Id; import does the same rather than inventing a UID
predicate. Its row-limit is a display cap, not a filter; the native virtualized
view retains all matching cached resources. This bounded compatibility behavior
and rollback are owned by ADR 0031.

### How Must The Values Inspector Apply The Shared Table Contract?

- VAL-TBL-001: Values MUST provide Key, Encoding, Value, Copy and Reveal columns. Key, Encoding and displayed Value MUST cycle ASC/DESC/NONE. Copy and Reveal are action columns, not sortable controls.
- VAL-TBL-002: KEY and VALUE MUST remain direct copy actions; encoded values MUST also offer RAW and DEC. Invalid decoding MUST leave the clipboard unchanged and show an explicit error. Secret access and reveal rules remain authoritative for every representation.
- VAL-TBL-003: Sorting, table Find and overflow presentation MUST read the masked displayed projection. Concealed Secret values MUST NOT become searchable or appear in tooltips. Layout and sort persistence MUST NOT contain resource values.
- VAL-TBL-004: Values MUST reuse hide, reorder, pin, width, keyboard copy and full-value access under TBL-004 through TBL-012. Refresh in the same inspector scope MUST preserve reading position. Changing scope MUST dismiss stale menus and overflow presentation.

## How Must Filtered Radar Terrain And Native Settings Match The Reference?

- RAD-FLT-01: A view filter preserves the complete cached island. Nonmatching resources remain dim grey at the same world coordinates; removed resources disappear. Only matching resources have delegates, tooltips, selection, inspector actions, alert animation and automatic focus.
- RAD-FLT-02: Filtering does not change pan, zoom or deterministic layout and does not cause transport. Dim terrain is drawn from cached, viewport-bucketed geometry rather than allocating delegates for every excluded resource.
- UI-TBL-01: New resource layouts follow Status, Kind, Name, Namespace, Cluster, CPU, Memory, Storage, Age, Ready, Restarts, Node, Image and Owner. Issue remains an optional hidden column. Reference-sized columns, flat glyph headers, compact rows and deterministic identity text replace uniform-width columns and color strips. Saved layouts retain their ordering, widths, visibility and pins. Reset uses the same authoritative defaults as initial load.
- UI-SET-01: Settings retain the reference section order: Alerts, Appearance, Diagnostics, Graphics, Privacy, Sources, Sync, Workspace, About. Existing native appearance, water, reduced-motion, synchronization, retention, YAML-limit, source-import and column-layout capabilities are directly accessible. Narrow windows stack setting labels above controls.
- UI-SET-02: Diagnostics capture an explicit stable snapshot on opening or Refresh. There are no Settings refresh timers. Request audit derives live queued/running entries from the scheduler and retains at most 200 completed request metadata records in memory. It is session-scoped and excludes credentials, headers, response bodies and query parameters.
- UI-SET-03: Appearance, Graphics, Sync and Privacy edit their existing settings directly. There are no duplicate Appearance or synchronization dialogs, routes or hidden selector states. Theme, variant, water, reduced motion, request policy, retained logs and YAML limits keep the existing atomic persistence and validation owner. Leaving unchanged settings does not save; failures retain the accepted configuration and show an error. Native slider keyboard tests use supported arrow-key interaction.
- FLT-PROB-01: StatefulSet readiness deficits, failed Jobs and terminating Pods must appear in Problems; ready StatefulSets, successful Jobs and transient starting Pods do not. Filtering remains cache-only. Raw counts from different synchronization times and resource catalogs are not parity evidence.

Implementation boundary: these requirements do not declare complete Settings capability parity. Native localization selection, source snapshot rename/removal, reference diagnostic table tools and About update/support links still require implementation and evidence. Optional workspace restoration now has a real setting and public startup/UI checks; multiwindow placement ownership and full installed-app restart comparison remain open. The reference uses a fixed resource-kind catalog, while native discovery also lists additional Kubernetes kinds; their total counts are therefore not required to be identical.

UI-TBL-02: Status text follows reference semantics with the selected theme's healthy, warning, critical or unknown color. Event types in both the Events workspace and the resource inspector follow the same semantic mapping: Normal is healthy, Warning is warning, unrecognized nonempty types are unknown, and missing types use ordinary text. Kind, namespace, cluster, node and image retain deterministic identity colors. Status and Event types must not be assigned an identity-hash color.

## What Makes The Reference Radar Geometry Deterministically Equal?

- RAD-DET-01: Given the same immutable resource projection and session identity, native world coordinates and terrain colors MUST match the actual C# reference. Topology inputs are cluster, namespace scope, kind, name, owner and Kubernetes UID. Resource jitter and collision ordering use the reference identity `session:kind:namespace-or-cluster:name:uid-or--`, not an API path. Independently created sessions have different identities and are not a valid exact-equality comparison.
- RAD-DET-02: Cluster and namespace decorations MUST use the reference virtual identities, ordinal grouping/order, seven-unit lattice, kind rings and stable collision tie ordering. An unlisted kind uses reference ring four but grass terrain color; ring and color fallbacks are distinct requirements.
- RAD-DET-03: Actual Namespace resources MUST remain selectable in native. They reuse an existing namespace anchor when one exists; otherwise they occupy free terrain only after canonical placement completes. This addition MUST NOT displace the reference resources or decorations for the same input projection.
- RAD-DET-04: Input enumeration, table sorting, presentation filters and camera changes MUST NOT change world coordinates. Filtering retains noninteractive grey excluded terrain. Geometry, projection and painting read public cache models and MUST NOT initiate transport.
- RAD-DET-05: Exact comparisons MUST use one recorded cache projection from the real reference public pipeline, identical viewport/pan/zoom and stable alert conditions. Comparing two changing API snapshots, different session identities or aggregate counts is insufficient. Isolated renderer captures MUST identify their boundary and MUST NOT be presented as full installed-window, water, animation, every-theme or release-parity evidence.

## How Does The Native Alert Editor Match The Reference Layout?

- UI-ALT-01: Settings section selectors use flat labels and a persistent active underline. Selecting an already active section does not clear its indication. The Alerts section has a compact, scrollable rule table above its action toolbar and editor, rather than a separate left-hand rule-button pane. The first available rule is selected on initial opening.
- UI-ALT-02: The rule table presents activation, active match count, name, matcher summary, action summary and attributed sound. Active counts derive from the existing whole-session alarm evaluation. Sortable columns cycle ascending, descending and stable catalog order; equal values retain catalog order and sorting preserves the selected rule identity. Keyboard Up/Down/Home/End select rows. Long values use plain-text hover previews and explicit local copy.
- UI-ALT-03: Built-in definitions remain locked except for activation; duplication creates an editable definition. Criteria are AND within a group and groups are OR. Users can add criteria to a specific group and remove criteria or complete groups. The last criterion in a group and the last group cannot be removed; saved groups remain contiguous and nonempty.
- UI-ALT-04: Existing asynchronous validation, optimistic persistence and private-store bounds remain authoritative. Failed saves and failed activation retain the saved catalog and visible draft. The local clipboard bridge rejects input exceeding 65536 characters and preserves the previous clipboard on rejection. No read request, extra timer or second alarm evaluator is introduced. Hidden Alerts presentation does not rebuild its derived table; it updates on becoming visible.
- UI-ALT-05: Sound selection for built-ins remains locked, but an existing nonempty sound can be previewed. Mute disables preview. The existing engine owns playback and its failures.

- UI-ALT-06: Radar zoom preview evaluates the current editor draft against the existing session cache, without saving or playing sound. It focuses the first visible matching resource, otherwise the first visible resource, with a minimum zoom of 100 percent. A disabled rule can be previewed without enabling it. No session, initial loading, invalid criteria, bounded-regex failure or an empty visible projection reports an inline error. Results from a changed session or cache cannot move the current camera. Explicit preview reveals the Radar sidebar, including its narrow-screen drawer, without navigating away from the draft.

This increment aligns the Alerts arrangement and reachable editing controls. It does not certify all Settings capabilities, populated C#/native framebuffer equality or release readiness. Native default descriptions explain the three retained rules; recent changes explicitly describe the agreed green highlight rather than the reference's outdated cyan description. Matcher summaries use the selected language's existing field labels rather than persistence identifiers; language changes update presentation without changing evaluation or saved criteria. The editor groups color and hold mode, then animation and hold mode, in two columns on desktop and one column in narrow panes. Sound-source and zoom-preview controls have executable focused evidence; installed visual and assistive-technology evidence remain separate. The existing real-cache alarm tests continue to govern behavior independently of this layout.

## How Does The Resource Inspector Preserve The Reference Presentation?

- UI-INSP-01: Previous and Next, the elided Kind/name title, refresh, review-deletion and close actions share one stable header. Actions use the established glyph control with accessible names, visible keyboard focus and touch-sized narrow targets. Delete uses the reference Trash glyph, not the Close glyph. Port forwarding is offered only for supported targets. Existing confirmation and request-priority owners remain unchanged.
- UI-INSP-02: Inspector tabs retain native TabBar selection and keyboard behavior, compact theme-aware backgrounds and a persistent active indication. Narrow tabs remain horizontally reachable and use at least 44 logical-pixel targets. Optional Values, Logs and Terminal tabs retain their existing availability rules.
- UI-INSP-03: Overview presents a Field/Value/Metric header and bordered metadata rows. It retains virtualized delegates, selectable plain-text values, readiness and metric gauges, and cache-first refresh with preserved scroll position. This presentation change must not add transport, timers or a second overview model.
- UI-INSP-04: YAML copy and terminal expansion are contextual icon actions. Fresh-only YAML editing, Secret masking, draft/discard handling, deletion read-back and terminal/session ownership remain governed by their existing specifications. Captioned controls do not display duplicate caption tooltips; icon-only controls retain explanatory tooltips.

## How Does Cached Diagnostic Guidance Lead To Investigation?

Known status evidence exposes read-only Events and YAML shortcuts in its
expanded diagnostic card. Pod cards additionally expose Pod logs; non-Pod cards
do not. These actions use the existing Inspector navigation and log owner.
Events and already accepted YAML open from cache without a request. Explicit
log opening may start the existing container-scoped log cadence; leaving Logs
stops its visible polling. Returning to Overview retains expanded guidance.
No diagnostic action applies a patch, changes limits or restarts a workload.

Official documentation opens only on explicit action. A failed external browser
handoff stays visible in the card, preserves guidance and allows explicit retry;
there is no automatic browser retry or Kubernetes request. Hover and rendering
remain cache-only. These investigation shortcuts do not establish embedded
terminal or mutating quick-fix support.

## How Does Inspector Value Copying Preserve Masking?

Inspector value copying preserves the existing individual-copy authorization:
the primary action copies the preferred representation; its native menu offers
the key, exact stored value and decoded text. Secret values remain masked unless
separately revealed. Empty text is copyable. Non-text binary values reject the
decoded action with a visible explanation and leave the clipboard unchanged;
the raw action remains available. Unknown keys/representations and a closed
Inspector cannot overwrite the clipboard. These actions read only the active
cached document and neither refresh nor save it. The complete values-table
toolbox and installed visual parity remain separate acceptance gates.

### Welche Import- und Session-Aktionen sind im nativen Client ergänzt?

Ergänzung 2026-10-05, akzeptierter Migrationsumfang:

- Kubeconfig-Paste verwendet denselben Parser und denselben geschützten, inhaltsadressierten Store wie Dateiimporte. Der Benutzer benennt einen Origin-Pfad; relative Zertifikats-, Token- und Plugin-Pfade beziehen sich darauf. Der Origin wird nicht als Datei geschrieben.
- Paste-Entwürfe werden bei erfolgreicher Speicherung und bei Abbruch aus dem Dialog gelöscht. Speicher- und Validierungsfehler bleiben inline sichtbar und erhalten den bearbeitbaren Entwurf. Import führt weder Anmeldung noch Clusterverbindung aus.
- Ein expliziter Home-Import liest ausschließlich `~/.kube/config`. Es gibt weiterhin keinen automatischen Import beim Start.
- „Refresh source files“ liest die bekannten Origin-Dateien erneut. Unveränderte Inhalte werden dedupliziert, Änderungen erzeugen weitere unveränderliche Snapshots. Nicht mehr lesbare Dateien werden einzeln gemeldet; vorhandene Quellen und Sessions bleiben erhalten. Das bloße Neuladen der Quellenliste ist davon getrennt.
- Quelldokumente sind vor Parsing und Speicherung auf 16 MiB UTF-8 begrenzt; gespeicherte JSON-Umschläge auf 32 MiB. Diese lokale Importgrenze ist unabhängig vom einstellbaren 3-MiB-Default für Ressourcen-YAML und Patches.
- Der Session-Manager zeigt gespeicherte Sessions aus dem Katalog, ohne sie zu aktivieren. Kontext- oder Namespace-Änderungen erzeugen geschlossene Konfigurations-Snapshots derselben Serie. Unveränderte oder bereits vorhandene Konfigurationen erzeugen kein weiteres Duplikat.
- Unabhängige Kopien übernehmen die gespeicherte Originalkonfiguration, nicht ungespeicherte Eingaben des Managers. Sie erhalten eine neue Serienidentität und keine Nutzungshistorie. Ein leerer Kopienname nutzt die bestehende Unnamed-Nummerierung.
- Speichern und Kopieren lassen aktive Session, YAML-Entwurf und Port-Forwards unverändert. Öffnen erfolgt ausdrücklich über die gespeicherten Sessions. Ungültige Namespaces, fehlende Kontexte/Sessions und gesperrte Stores scheitern ohne Teiländerungen.
- Die noch fehlenden C#-Session-Felder Sicherheitsstufe, Farbe und Icon sowie Source-Umbenennung/-Entfernung bleiben offene Migrationspunkte. Die Ergänzung behauptet dafür keine Parität.

### Wie werden Anzeige-Aliase importierter Kontexte bearbeitet?

Ein expliziter Rename-Dialog bearbeitet den Anzeigenamen eines einzelnen bereits importierten Kontexts. Leere Eingabe stellt den kanonischen Namen wieder her. Abbruch verändert nichts; Validierungs-, Lock- und Schreibfehler erhalten den Entwurf und zeigen einen Fehler. Der bestehende private Quellenstore speichert den Alias am unveränderlichen Snapshot. Ein erneuter Import identischer Inhalte behält ihn; ein neuer Inhalt erzeugt einen unabhängigen Snapshot ohne übernommene Aliase.

Kontextidentität, Original-YAML, Credentials, Sessionnamen, Session-Konfigurationen und Aktivierung bleiben unverändert. Insbesondere benennt ein Quellen-Alias keine bereits unabhängig benannte oder automatisch nummerierte Session um. Anzeige-Aliase sind auf 512 Zeichen ohne Steuerzeichen begrenzt. Entfernen und Quellen-Filterzuordnung sind separate offene Migrationspunkte; die Löschwirkung auf gebundene Sessions benötigt eine bestätigte Entscheidung.

### Wie helfen bekannte Fehlerhinweise im Inspector?

- Der Overview zeigt eine standardmäßig eingeklappte Diagnosegruppe, ausschließlich abgeleitet aus dem bereits akzeptierten Detail-Cache. Aufklappen, Hover und Textauswahl lösen keine Requests aus. Frische und Lesefehler bleiben über den bestehenden Inspector-Status sichtbar.
- Erkannt werden Image-Pull-/Container-Erstellungsfehler, Restart-Backoff, aktuelles und ausdrücklich historisches OOMKilled, Evicted/Unschedulable, Node-Readiness/-Pressure, Pending/Lost-Volume-Claims und überschrittene Deployment-Fortschrittsfristen. Die Regeln prüfen die tatsächliche API-Version und Kind; gleichnamige Custom Resources werden nicht nach Core-Semantik interpretiert.
- Ein Hinweis enthält den beobachteten Statuscode, den Container/Objekt-Scope, eine Erklärung, nächste lesende Prüfschritte und einen ausdrücklich anklickbaren Link zur offiziellen Kubernetes-Dokumentation. Freie Statusmeldungen, Secret-Inhalte und eine behauptete Root Cause werden nicht übernommen.
- Historische OOM-Terminierungen bleiben gelbe historische Hinweise, auch wenn der Container inzwischen läuft. Fehlende oder unbekannte Statusdaten liefern keinen Gesundheitsbeweis.
- Es werden höchstens acht Hinweise angezeigt. Eingeklappte oder ausgeblendete Diagnoseinhalte instanziieren keine Detail-Controls und besitzen weder Timer noch Transport.
- Diagnose sendet keine Writes. Geführte mutierende Quick Fixes bleiben ein gesonderter, noch offener Implementierungspunkt und müssen vor dem Senden den vorhandenen frischen YAML-/Diff-/Bestätigungsweg durchlaufen.

## Native Age/UID And Explicit External Links (2026-10-06)

- Age and UID are cache-only field filters. Age numeric comparisons use cached creation timestamps, not rounded display labels; bare values mean seconds. Whole-number ms/s/m/h/d/w units, reference aliases and compound durations are accepted. Ranges require all comparisons; numeric exact alternatives require one match. Quoted display text, prefix/suffix and bounded regex remain available. Invalid numeric expressions report errors; missing, malformed or future timestamps never match numeric terms. No filter starts a Kubernetes request or a new refresh timer.
- UID is available to global search, field selection, copying, sorting and the existing column toolbox. It is hidden by default. Layout-v2 migration retains all fifteen prior columns and their user settings, appends hidden UID, and does not rewrite storage until an explicit save. Schema-v3 saving retains the existing conflict/locking behavior.
- Opening a port-forward endpoint is an explicit user action. Only a Listening task owned by the active session can be opened. The URL is constructed from the locally assigned port and literal IPv4 loopback host; callers cannot supply an arbitrary URL. HTTP and HTTPS are separate choices. The action does not authenticate or retry Kubernetes requests and does not claim the remote server supports the chosen protocol. Closed, foreign-session and unknown tasks fail visibly.
- About preserves the six existing C# project/support destinations, exposes the actual shipped sound sources/licenses and displays the complete bundled Podlord MIT license without network access. External links open only after a user action, never by visiting About. Browser-launch acceptance is distinct from actual browser availability and network success. Library notices and corresponding source distribution remain a separate release gate.

### Native Command-Line And Package Boundary Checks (2026-10-06)

- `--help` and `--version` are explicit non-operational actions. They MUST exit
  successfully without importing ambient kubeconfig, running its authentication
  provider, contacting Kubernetes, creating a profile, or changing an invalid
  profile supplied alongside the metadata option.
- Unknown command-line options MUST fail explicitly with a nonzero exit and
  understandable feedback, without importing ambient credentials.
- The application, About presentation, and macOS bundle version MUST use the
  CMake project version. Package verification compares command-line output with
  the actual bundle version rather than maintaining another literal.
- The macOS dependency gate MUST resolve executable-relative, loader-relative,
  rpath-relative and symlink paths before accepting their application boundary.
  Existing system-library exceptions remain explicit. External runtime plugins
  MUST NOT evade the check just because their link is loaded dynamically.
- Internal relative aliases remain supported. Symlink traversal is bounded to
  forty links and unresolved links fail explicitly. Tests mutate private copies
  of actual packages and preserve the original relative alias targets.
- Native preflight CI retains dependency-boundary and packaged-startup evidence
  separately. These checks do not establish Metal rendering, visual parity,
  signing/notarization, license compliance, or other-platform support.
- Both implementation release coverage gates remain 95% lines and 90% branches. Lower
  historical thresholds and excluded legacy UI files do not establish release
  readiness. Coverage requires a complete, isolated instrumented execution.
  CI reports coverage and performance target misses diagnostically; functional
  failures remain authoritative CI failures. A successful CI status does not waive
  unmet release coverage or performance requirements.
- Desktop comparisons MUST select each bundle's declared executable before
  starting Kubernetes. An exited comparison process MUST fail the comparison and
  trigger owned cleanup, never an automatic application relaunch outside its
  isolated profile. Screenshots from mismatched resource snapshots MUST identify
  that limitation rather than claim exact Radar parity.

### When is a mutation result trustworthy?

A non-streaming HTTP response with a declared content length must complete that transfer before its body can establish an acknowledgement or absence. Automatically decompressed replies use Qt's current response metadata; decoded bytes must not be compared with a removed original compressed length. Valid complete chunked or gzip responses remain supported. The local byte cap on log streams is not interpreted as a mutation transfer failure.

A fresh resource returned after a write must retain the original UID before an acknowledged outcome may clear the original draft. A replacement object with the same API path or matching desired values is a different resource: preserve the draft, report the identity conflict, and never automatically write it to the replacement. Unavailable, malformed or mismatched read-back data cannot prove success. Authentication recovery and further write attempts remain explicit user actions.

A confirmed YAML mutation must also use the API version of its current cached target. Matching UID, kind, name and namespace do not permit a draft prepared against a different API version to enter the write queue. Reject it before sending; refresh and prepare a target-correct preview explicitly.

Session configuration keeps its Close action in a fixed dialog footer, outside scrollable fields. At a narrow viewport, labels wrap and Close must remain mouse- and keyboard-accessible without scrolling to the bottom. Responsive evidence is captured after the real Qt Quick scene reaches the requested size, not before resizing. A component rendering at phone-sized dimensions does not establish an iOS or Android release.

### How are the native alert sounds selected and packaged?

The native catalog must preserve all 117 existing reference entries, including the silent action, stable persisted IDs, descriptions, attribution, source URLs and music flags. Its 116 real sound assets are embedded in the application; selecting, searching or previewing them never downloads media or contacts Kubernetes. The local catalog is built once and reused.

Sound search matches the existing reference metadata case-insensitively and trims surrounding whitespace. Filtering the chooser must not change an alert's chosen sound, including when no options match. Only explicit selection changes the draft. Saving an unrelated edit must retain that chosen sound. Locked built-in definitions remain locked, and muted preferences disable preview. The editor shows the chosen sound's name, purpose, author, license and source as plain text.

Audio release evidence must use real embedded assets and the actual platform backend. An offscreen backend timeout or an advertised codec list alone does not prove whether native playback works. A timed decoding/completion check and repeated-use check are required; silent automated playback is not a claim of human-observed audibility.

### Can referenced credential files block context resolution?

Token, CA, client-certificate and private-key references share the existing 16 MiB external configuration budget per file. Resolve relative paths against the original configuration's directory. Reject missing, empty, non-regular and oversized files explicitly without leaking contents. Follow symlinks only when they resolve to a regular file, and bound the actual read even after the size check. Importing metadata never executes a provider or contacts a cluster. The resource YAML apply limit remains separately configurable.

### Which legacy provider credentials can the native app use without running a provider?

Preserve the C# cached-token capability: when no explicit token or token-file value is selected, read `auth-provider.config.access-token`, falling back to `id-token` when absent or empty. Validate the chosen value with the shared bearer-token checks and never expose it in source metadata or errors. Malformed selected provider token/config types fail explicitly. Import and connection resolution remain network- and execution-free.

This is not a legacy provider refresh implementation. Without a cached token, report that the user must refresh externally and explicitly reimport. A 401 suspends automatic requests and does not launch a browser or executable. Reimported inline provider credentials create a new snapshot; old sessions remain bound to the old one. Referenced token files remain externally owned rather than becoming frozen copies. Mixed exec/provider configurations and unsupported impersonation, including `as-uid`, fail explicitly rather than silently changing identity.

### May an appearance update reset unrelated settings inputs?

No. A successful or rejected theme update must preserve unsaved synchronization and YAML-limit inputs without saving them implicitly. Appearance notifications synchronize only the theme/variant controls. Opening a settings surface or explicitly changing its section may still initialize that section from saved values; this rule does not introduce settings-draft persistence.

### How does the native command palette preserve existing action safety?

Use the existing native Qt actions, not a second implementation of navigation, filtering or forwarding. Ctrl/Meta+K and a visible Commands button open the modal palette. Focus starts in its search field; arrow keys select a command, Enter executes only an enabled selection, and Escape/Close dismiss it. Reopening starts with an empty query. Matching is case-insensitive, trimmed and based on command-word prefixes; searching Sources must not select Resources accidentally. Empty results and disabled actions remain explicit, without executing a fallback.

Opening and searching the palette use local action metadata and must not request Kubernetes data. Resource/Event/Settings navigation and Problems filtering continue through their cache-backed owners. Port-forward preparation targets the current eligible selection and still requires the existing separate start confirmation. Source navigation selects Sources settings without importing or opening a session. Keep the button and Close action reachable at narrow widths.

The native catalog implements all seven reference actions, including generated K3D import. Import uses the existing asynchronous source owner, an installed CLI without a shell, bounded output and explicit failure feedback. It preserves generated configuration bytes in immutable snapshots, normalizes only parsed local endpoints, and neither changes the user's kubeconfig nor opens a session or authentication implicitly. Missing tooling disables no unrelated capability. Generated sources require explicit re-export rather than file refresh. An embedded container terminal is a different capability.

## How Does Explicit Native Startup Import Work?

LEG-001 now exposes `--kubeconfig PATH` for one explicit file, local file URL or
folder. It enters the existing asynchronous source-import workflow after the
window is created and the workspace is ready. It does not import ambient
configuration, activate a session or run authentication. Empty/repeated options
fail before profile creation; help/version remain side-effect-free. Missing or
malformed source content uses the existing import error presentation rather than
terminating the application. [ADR 0040](../adr/0040-explicit-native-startup-source-import.md)
records this bounded migration and remaining startup-path/device evidence gaps;
[the test map](k3d-test-map.md#which-bottom-docked-inspector-and-startup-checks-passed-on-2026-10-06)
owns executed results. No complete source-management parity is claimed.

## How Do Long Log Entries Remain Usable Without Heavy List Rendering?

The log list renders a bounded preview of at most 1,024 UTF-16 code units of the
entry body, without cutting a surrogate pair. This is a display bound, not a
retention or data-loss rule. The existing retained cache remains authoritative.
Open, double-click and Return/Enter expose the complete selected cached entry;
copy preserves its full text. Those actions MUST NOT schedule a log request.
Closing the full-entry dialog clears its hidden text. The existing 5 MB default,
user setting, pause/follow behavior and stable scroll-position rules still apply.

## How Are Native Dependency Notices Exposed?

About MUST provide explicit, local, read-only access to the bundled dependency
license texts as well as Podlord's own license. The existing dialog supports
selection/copy, Escape and keyboard opening, fits a 680x480 window and clears its
text when closed. Reopening Podlord's license MUST NOT retain dependency text.
Opening or copying notices MUST NOT authenticate, contact Kubernetes or launch a
browser. Original supplier texts are retained unchanged.

Portable package preflight MUST preserve those texts and the matching configured
Qt SDK's supplier SPDX documents. Missing required metadata is an explicit
packaging failure. Text presence is not proof of corresponding-source,
replacement/relinking, installation or distribution-term compliance; those
remain independently evidenced release gates.

## What Must An Embedded Container Terminal Support?

The confirmed terminal capability is a fully interactive container shell,
including PTY input, ANSI rendering, terminal resizing, and interactive
programs such as vi. A command-only output panel does not satisfy this
requirement. The implementation must use the Kubernetes streaming API
without requiring kubectl or a browser frontend.

The native inspector now implements this capability using the session-owned
transport and VT emulator in [ADR 0042](../adr/0042-native-container-terminal.md).
The test map distinguishes external-boundary UI checks from real local K3S
and Cocoa/Metal shell, vi read-back, interrupt and cleanup evidence. This does
not establish all-device support, an installed terminal workflow or complete
product parity.

The user explicitly chooses one declared regular container and a POSIX shell
path. No shell, protocol, authentication or connection is retried automatically.
The terminal retains its original target across inspector/session navigation;
session close and confirmed context removal close its stream. Paint and scroll
read its retained screen rather than fetching. Clipboard paste is explicitly
confirmed against a captured snapshot and the original terminal.

## What Happens When An Imported Context Is Removed?

The native implementation uses the single catalog commit in
[ADR 0041](../adr/0041-atomic-imported-context-removal.md): a version 3 catalog
atomically removes the confirmed saved sessions and excludes the immutable
context identity from listing and connection resolution. Version 2 remains
readable. An explicit unchanged-source reimport restores availability, not
deleted sessions, and retains the existing snapshot import time. Shared owned
snapshot content and original kubeconfigs are not rewritten. A changed affected
session set invalidates confirmation. An owned unapplied YAML draft must be
saved or explicitly discarded first.

The confirmed 2026-10-06 behavior is explicit, confirmed cascade removal of the
selected imported context and its associated saved sessions. The confirmation
must identify the context and affected sessions before any persistent deletion.
Cancellation changes nothing. Other contexts, unrelated sessions, and the
original kubeconfig file remain untouched.

Affected open tabs and their session-owned streams/forwards must close on
successful removal. Already dispatched ordinary requests finish processing;
obsolete results must not reappear in another session. Retained resource cache
continues under its existing TTL rather than becoming another deletion owner.
Persistence failures must be explicit and must not masquerade as a successful
cascade or silently lose unrelated configuration. Implementation and executed
evidence are recorded in ADR 0041 and the test map; whole-product parity remains
separate.

## Which empty-state and search controls must match the reference?

- UI-EMPTY-01: Resources with no loaded or matching rows show the existing product logo, a localized state title and the actual empty/loading/filter explanation. The logo is bundled, non-interactive, proportionally scaled and bounded by the available space. It must not cover loaded rows or create a network request.
- UI-SEARCH-01: The on-demand Resources/Events global search includes Previous, Next and a current/total match label. Previous and Next loop in the current cached table order, select and reveal a row, and do not open the inspector or fetch data. Enter advances; Shift+Enter goes backwards. Editing the expression starts at the first matching row. Empty expressions, zero matches and invalid expressions have disabled navigation and a 0/0 label. Existing field filters remain effective. Table-local Find remains independent and does not alter global filters.
- UI-APPEAR-01: Theme intensity was explicitly removed on 2026-10-08. All 19 named palettes and dark/light remain. Remove the selector, runtime state, API argument, adjustable render branches and translation keys. Read old settings without implicit writes; the next explicit save discards only the obsolete field. ADR 0024 owns migration and rollback.

## How are native release updates discovered?

Confirmed on 2026-10-08: automatic checks are weekly, not daily. See [ADR 0043](../adr/0043-weekly-native-release-checks.md).

| ID | Requirement |
|---|---|
| UPD-001 | The native application MUST check the latest stable release anonymously at most once per seven days by default, including across restarts and simultaneous windows/processes. Explicit Check for updates MUST bypass the time interval, but MUST NOT duplicate an in-flight request. |
| UPD-002 | About MUST render cached status without network activity caused by displaying, scrolling or hovering. It MUST expose an accessible explicit check action and verified release/download actions when applicable. |
| UPD-003 | A download MUST be offered only for a newer stable version with an uploaded native package matching the running OS and architecture, within the distribution size budget. C# packages and guessed cross-platform installers MUST NOT be offered. |
| UPD-004 | Release/download URLs MUST exactly match the repository and validated version/asset. Redirects MUST NOT be followed. Opening a URL requires deliberate user input; automatic installation and browser login MUST NOT occur. |
| UPD-005 | Checks MUST have one lifecycle owner, a bounded metadata response and a 10-second metadata deadline. Failures, malformed metadata, authentication failures and rate limits MUST surface a readable error without immediate retries or upstream response-body disclosure. Existing verified update information MUST survive failed checks. |
| UPD-006 | Attempts/results MUST be stored atomically in a private operational cache without overwriting settings, sessions or sources. Invalid, unsupported or symlinked cache/lock paths MUST fail explicitly without destroying existing data. |
| UPD-007 | Startup MUST NOT wait for release HTTP before opening the workspace. A running application MUST schedule the next due weekly check. Application activation MUST NOT bypass the interval. `PODLORD_DISABLE_UPDATE_CHECK=1` MUST suppress automatic and explicit checks. |
| UPD-008 | Public behavior MUST be tested through HTTP and actual About controls, including weekly boundaries, failure retention, concurrency, invalid metadata, incompatible assets and deliberate safe browser handoff. Published native assets and current desktop/mobile visual proof remain separate release evidence. |
