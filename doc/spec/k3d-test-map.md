# k3d And Performance Test Map

Podlord's Kubernetes integration tests use a disposable k3d cluster. The suite is intentionally slow and proof-oriented.

Not every behavior belongs in k3d. Real Kubernetes proves API truth, RBAC, metrics, port-forwarding, and object lifecycle behavior. UI freeze and redraw behavior must be verified with deterministic headless/UI budget tests because cluster startup, Docker, image pulls, and API latency would hide the actual regression. Fog machine, wrong room.

## Cluster

- One k3d server node
- Disposable kubeconfig under the test temp directory
- Admin context from k3d
- Limited RBAC context generated from a ServiceAccount token

## Scenarios

| Scenario | Resources | Proof |
|---|---|---|
| Flat resource explorer | Namespace, Node, Pod, Deployment, Service, EndpointSlice, ConfigMap, Secret, Job, CronJob, PVC, NetworkPolicy, Event | Podlord can list a real cluster across namespaces and API groups. |
| Filters | Pod and Deployment queries by kind, namespace, status, and search | Filtering is applied after real API reads. |
| Secret redaction | `Secret/podlord-secret` | Secret data and managed fields do not reach YAML output. |
| Broken workload | `Deployment/podlord-broken` and its pod events | Image-pull failures surface as status/events. |
| Logs | `Pod/podlord-log` | Pod log tail uses the bound kubeconfig context. |
| Jobs | `Job/podlord-success`, `Job/podlord-fail`, `CronJob/podlord-cron` | Batch statuses are visible. |
| Networking | `Service/podlord-healthy`, generated EndpointSlice, NetworkPolicy | Network resources list and inspect. |
| Cluster-scoped detail | Node and Namespace | Cluster-scoped detail paths work without namespaces. |
| RBAC | `ServiceAccount/podlord-limited` | Forbidden API responses become explicit freshness failures. |
| Boundary validation | Pod detail without namespace | Invalid input fails before network calls. |

## UI And Performance Map

| Behavior | Verification | Current proof | Budget |
|---|---|---|---|
| First load does not show partial rows or radar | Fake Kubernetes HTTP + ViewModel | `First_load_does_not_render_partial_cache_rows_or_radar_until_sync_finishes` | Rows/radar stay empty until first sync finishes. |
| Real cluster first load, tabs, radar, health, inspector | k3d E2E + ViewModel | `Podlord_ui_state_survives_real_k3d_loading_tabs_radar_health_and_inspector` | Tab switch while loading under 750 ms. |
| Real deployments, events, metrics, inspector | k3d E2E + warmed cache | `Podlord_ui_drives_real_k3d_events_deployments_metrics_and_inspector` | Real API behavior, no timing budget. |
| Real RBAC fallback | k3d E2E + limited kubeconfig | `Podlord_ui_drives_real_k3d_limited_rbac_namespace_scope` | Forbidden state appears; namespace scope still loads. |
| Real native port-forward | k3d E2E + localhost HTTP | `Podlord_ui_starts_and_stops_real_k3d_port_forward` | Local HTTP responds through the forward. |
| Large cache filter apply and clear | Public ViewModel budget test | `Large_cache_filter_changes_are_cache_only_and_budgeted` | Each local filter action under 1 s; no network request. |
| Radar resize and zoom | Public ViewModel budget test | `Large_cache_radar_viewport_and_zoom_updates_are_budgeted` | Resize under 1.5 s; zoom under 1 s; no network request. |
| Graph/events workspace materialization | Public ViewModel budget test | `Large_cache_graph_and_events_workspace_materialization_are_budgeted` | Workspace materialization under 1.5 s; no network request. |
| Cached tab switch | Public ViewModel budget test | `Cached_tab_switches_restore_session_state_under_budget` | Each cached tab switch under 1.5 s; no network request. |
| Inspector cache-first focus | Public ViewModel budget test | `Inspector_focus_renders_cached_summary_before_fresh_detail_returns` | Cached inspector appears under 150 ms before fresh detail returns. |
| Dispatcher stall on settings/source view | Headless Avalonia UI | `Opening_sources_settings_updates_title_and_active_tab_state_without_stalling_dispatcher` | Open sources settings under 250 ms. |
| No redraw churn on unchanged data | Fake Kubernetes HTTP + ViewModel | `Unchanged_refresh_does_not_redraw_resource_table_or_radar` | Resource and radar collections do not republish. |
| Visual frame stability on tiny cache delta | Headless Avalonia UI | `Tiny_cache_delta_keeps_visible_rows_and_frame_stable` | Render hash stays identical. |

## Known Hot Paths

| Path | Why it is watched | Guard |
|---|---|---|
| `MainWindowViewModel.ApplyLocalFilterCore` | Sorts and filters cached rows, evaluates alerts, syncs resource rows, updates radar and pulse data. | Large cache filter budget tests. |
| `MainWindowViewModel.UpdateRadarBlocks` | Rebuilds the deterministic radar island and alert visual state. | Radar budget tests and radar behavior tests. |
| `MainWindowViewModel.SelectWorkspace` | Schedules secondary graph/events work and may restore rendered state. | Graph/events workspace budget tests. |
| `MainWindowViewModel.SelectedSession` / `ActivateSessionTab` | Saves and restores per-session rendered state, filter, radar, and selection. | Cached tab switch tests and k3d multi-session tests. |
| `MainWindowViewModel.OpenSelectedResourceAsync` | Must render cached data before fresh API detail returns. | Inspector cache-first budget test. |
| `KubernetesResourceService.WarmResourceCacheAsync` | Real API fan-out, request queue, cache merge, metrics fallback. | fake Kubernetes cache tests and k3d E2E tests. |

## Gaps

| Behavior | Best test layer | Status |
|---|---|---|
| Full window FPS under manual pointer movement | Manual profiling or future UI automation harness | Not stable enough for k3d. |
| OS compositor flicker | Real desktop smoke/profiling | Headless tests can catch redraw churn, not compositor brightness flicker. |
| Very large production cluster scale above fixture size | Synthetic public ViewModel budget tests plus optional stress fixture | k3d can grow this later, but runtime would be slow. |
| Long-running memory growth | Runtime profiler and diagnostics assertions | Not a k3d-only problem. |

Run:

```sh
scripts/test.sh
```

The script starts Colima when available, ensures k3d/kubectl exist, creates the cluster during tests, and deletes it afterward.
