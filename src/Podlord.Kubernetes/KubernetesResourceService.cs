using System.Net;
using System.Net.Http.Headers;
using System.Net.Security;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Security.Cryptography.X509Certificates;
using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading;
using Podlord.Core;
using YamlDotNet.Serialization;
using K8sClient = k8s.Kubernetes;
using K8sConfiguration = k8s.KubernetesClientConfiguration;
using K8sStreamDemuxer = k8s.StreamDemuxer;
using K8sStreamType = k8s.StreamType;
using K8sWebSocketProtocol = k8s.WebSocketProtocol;

namespace Podlord.Kubernetes;

public sealed class KubernetesResourceService
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    private static readonly TimeSpan ListCacheTtl = TimeSpan.FromSeconds(25);
    private static readonly TimeSpan ListDisplayCacheTtl = TimeSpan.FromHours(24);
    private static readonly TimeSpan DetailCacheTtl = TimeSpan.FromSeconds(10);
    private static readonly TimeSpan LogCacheTtl = TimeSpan.FromSeconds(4);
    private static readonly TimeSpan PulseCacheTtl = TimeSpan.FromSeconds(15);
    private static readonly TimeSpan PulseUnavailableTtl = TimeSpan.FromMinutes(1);
    private static readonly TimeSpan MinimumRequestSpacing = TimeSpan.FromMilliseconds(400);
    private static readonly SemaphoreSlim SharedRequestGate = new(1, 1);
    private static readonly object SharedRateLock = new();
    private static DateTimeOffset sharedLastRequestAt = DateTimeOffset.MinValue;
    private static DateTimeOffset? sharedBackoffUntil;

    private readonly AppState state;
    private readonly HttpMessageHandler? handler;
    private readonly IPodlordClock clock;
    private readonly object cacheLock = new();
    private readonly object queueLock = new();
    private readonly object telemetryLock = new();
    private readonly PriorityQueue<IQueuedRequest, QueuedRequestOrder> requestQueue = new();
    private readonly Dictionary<ResourceListCacheKey, ResourceListCacheEntry> listCache = [];
    private readonly Dictionary<ResourceDetailCacheKey, ResourceDetailCacheEntry> detailCache = [];
    private readonly Dictionary<PodLogCacheKey, PodLogCacheEntry> logCache = [];
    private readonly Dictionary<string, ResourcePulseCacheEntry> pulseCache = [];
    private readonly Queue<DateTimeOffset> requestStarts = [];
    private readonly Queue<KubernetesRequestAuditEntry> requestAudit = [];
    private long requestSequence;
    private bool queueRunning;
    private DateTimeOffset lastRequestAt = DateTimeOffset.MinValue;
    private DateTimeOffset? backoffUntil;

    public KubernetesResourceService(AppState state, HttpMessageHandler? handler = null, IPodlordClock? clock = null)
    {
        this.state = state;
        this.handler = handler;
        this.clock = clock ?? new SystemPodlordClock();
    }

    public async Task<ResourceExplorerSnapshot> ListClusterResourcesAsync(ResourceQuery query, CancellationToken cancellationToken = default)
    {
        return await WarmResourceCacheAsync(query, KubernetesRequestPriority.UserVisible, cancellationToken).ConfigureAwait(false);
    }

    public KubernetesRequestTelemetry RequestTelemetry()
    {
        var now = DateTimeOffset.UtcNow;
        int requestsLastMinute;
        lock (telemetryLock)
        {
            PruneRequestStarts(now);
            requestsLastMinute = requestStarts.Count;
        }

        int queuedRequests;
        lock (queueLock)
        {
            queuedRequests = requestQueue.Count;
        }

        return new KubernetesRequestTelemetry(
            requestsLastMinute,
            Math.Round(requestsLastMinute / 60d, 2),
            queuedRequests,
            BackoffUntil(now));
    }

    public IReadOnlyList<KubernetesRequestAuditEntry> RequestAuditLog()
    {
        lock (telemetryLock)
        {
            return requestAudit.Reverse().ToList();
        }
    }

    public ResourceExplorerSnapshot GetCachedResourceSnapshot(ResourceQuery query)
    {
        return GetCachedResourceSnapshot(query, applyFilters: true);
    }

    public ResourceExplorerSnapshot GetCachedResourceSnapshot(ResourceQuery query, bool applyFilters)
    {
        var connection = state.SessionConnection(query.SessionId);
        var rows = new List<FlatResourceRow>();
        var specs = PlannedSpecs(query).ToList();
        var namespaces = PlannedNamespaces(query);

        foreach (var spec in specs)
        {
            var scope = spec.Namespaced && namespaces.Count > 0
                ? namespaces
                : [null];
            foreach (var ns in scope)
            {
                rows.AddRange(GetCachedRows(connection.Session.Id, spec.Kind, ns));
            }
        }

        var snapshotQuery = applyFilters ? query : UnfilteredSnapshotQuery(query);
        return SnapshotFromRows(connection, rows, [], snapshotQuery);
    }

    public async Task<ResourceExplorerSnapshot> WarmResourceCacheAsync(
        ResourceQuery query,
        KubernetesRequestPriority priority = KubernetesRequestPriority.Background,
        CancellationToken cancellationToken = default)
    {
        var connection = state.SessionConnection(query.SessionId);
        using var client = CreateClient(connection);
        var rows = new List<FlatResourceRow>();
        var failures = new List<ResourceListFailure>();
        var specs = PlannedSpecs(query).ToList();
        var namespaces = PlannedNamespaces(query);

        foreach (var spec in specs)
        {
            var scope = spec.Namespaced && namespaces.Count > 0
                ? namespaces
                : [null];
            foreach (var ns in scope)
            {
                try
                {
                    rows.AddRange(await ListRowsForSpec(client, connection, spec, ns, query.ForceRefresh, priority, cancellationToken).ConfigureAwait(false));
                }
                catch (KubernetesStatusException ex) when (ex.StatusCode == HttpStatusCode.NotFound && spec.Optional)
                {
                    // Optional APIs such as Gateway are not installed on every cluster.
                }
                catch (KubernetesStatusException ex)
                {
                    failures.Add(Failure(spec.Kind, ex));
                }
                catch (HttpRequestException ex)
                {
                    failures.Add(new ResourceListFailure(
                        spec.Kind,
                        FreshnessState.Stale,
                        HttpFailureMessage(ex),
                        "Check cluster connectivity and retry."));
                }
            }
        }

        var enrichedRows = await EnrichPulseMetricsAsync(client, connection, rows, priority, cancellationToken).ConfigureAwait(false);
        StorePulseRows(connection.Session.Id, enrichedRows);
        return SnapshotFromRows(connection, enrichedRows, failures, query);
    }

    public async Task<ResourceDetail> GetResourceDetailAsync(ResourceIdentity identity, CancellationToken cancellationToken = default)
    {
        return await GetResourceDetailAsync(identity, false, KubernetesRequestPriority.Foreground, cancellationToken).ConfigureAwait(false);
    }

    public ResourceDetail? GetCachedResourceDetail(ResourceIdentity identity)
    {
        var connection = state.SessionConnection(identity.SessionId);
        var cacheKey = new ResourceDetailCacheKey(connection.Session.Id, identity.Kind, identity.Namespace, identity.Name);
        lock (cacheLock)
        {
            return detailCache.TryGetValue(cacheKey, out var entry) ? entry.Detail : null;
        }
    }

    public async Task<ResourceDetail> GetResourceDetailAsync(
        ResourceIdentity identity,
        bool forceRefresh,
        KubernetesRequestPriority priority,
        CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrWhiteSpace(identity.Kind))
        {
            throw PodlordException.InvalidInput("Resource kind is required.", "Select a resource first.");
        }

        if (string.IsNullOrWhiteSpace(identity.Name))
        {
            throw PodlordException.InvalidInput("Resource name is required.", "Select a resource first.");
        }

        var connection = state.SessionConnection(identity.SessionId);
        var spec = ResourceSpecs.ForKind(identity.Kind) ?? throw PodlordException.UnsupportedResourceKind(identity.Kind);
        if (spec.Namespaced && string.IsNullOrWhiteSpace(identity.Namespace))
        {
            throw PodlordException.InvalidInput(
                $"Namespace is required for {identity.Kind}.",
                "Select the resource from the explorer so Podlord can bind the namespace.");
        }

        var cacheKey = new ResourceDetailCacheKey(connection.Session.Id, identity.Kind, identity.Namespace, identity.Name);
        if (!forceRefresh && TryGetDetail(cacheKey, out var cached))
        {
            return cached;
        }

        using var client = CreateClient(connection);
        try
        {
            var path = spec.DetailPath(identity.Namespace, identity.Name);
            var document = await GetJsonQueuedAsync(client, path, priority, cancellationToken).ConfigureAwait(false);
            var sanitized = SanitizeObject(document.DeepClone()!.AsObject(), identity.Kind);
            var row = RowFromObject(connection, identity.Kind, document.AsObject());
            var events = await RelatedEvents(client, identity, priority, cancellationToken).ConfigureAwait(false);
            var detail = new ResourceDetail(
                identity,
                row.Status,
                FreshnessState.Fresh,
                ToYaml(sanitized),
                SummaryItems(document.AsObject(), row),
                ConditionItems(document.AsObject()),
                events,
                ValueItems(document.AsObject(), identity.Kind));
            StoreDetail(cacheKey, detail);
            return detail;
        }
        catch (KubernetesStatusException ex)
        {
            throw PodlordException.KubernetesApi(connection.Context.Name, identity.Kind, ex.Message, ex);
        }
        catch (HttpRequestException ex)
        {
            throw PodlordException.KubernetesApi(connection.Context.Name, identity.Kind, HttpFailureMessage(ex), ex);
        }
    }

    public async Task<PodLogSnapshot> GetPodLogsAsync(PodLogRequest request, CancellationToken cancellationToken = default)
    {
        return await GetPodLogsAsync(request, false, KubernetesRequestPriority.Foreground, cancellationToken).ConfigureAwait(false);
    }

    public PodLogSnapshot? GetCachedPodLogs(PodLogRequest request)
    {
        var connection = state.SessionConnection(request.SessionId);
        var cacheKey = new PodLogCacheKey(connection.Session.Id, request.Namespace, request.PodName, request.Container, request.TailLines, request.Previous);
        lock (cacheLock)
        {
            return logCache.TryGetValue(cacheKey, out var entry) ? entry.Snapshot : null;
        }
    }

    public async Task<PodLogSnapshot> GetPodLogsAsync(
        PodLogRequest request,
        bool forceRefresh,
        KubernetesRequestPriority priority,
        CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrWhiteSpace(request.Namespace))
        {
            throw PodlordException.InvalidInput("Namespace is required for pod logs.", "Select a pod with a namespace.");
        }

        if (string.IsNullOrWhiteSpace(request.PodName))
        {
            throw PodlordException.InvalidInput("Pod name is required for pod logs.", "Select a pod first.");
        }

        var connection = state.SessionConnection(request.SessionId);
        var cacheKey = new PodLogCacheKey(connection.Session.Id, request.Namespace, request.PodName, request.Container, request.TailLines, request.Previous);
        if (!forceRefresh && TryGetLog(cacheKey, out var cached))
        {
            return cached;
        }

        using var client = CreateClient(connection);
        var path = $"/api/v1/namespaces/{Escape(request.Namespace)}/pods/{Escape(request.PodName)}/log?tailLines={Math.Max(1, request.TailLines)}&timestamps=true";
        if (!string.IsNullOrWhiteSpace(request.Container))
        {
            path += $"&container={Escape(request.Container)}";
        }

        if (request.Previous)
        {
            path += "&previous=true";
        }

        try
        {
            var text = await GetTextQueuedAsync(client, path, priority, cancellationToken).ConfigureAwait(false);
            var snapshot = new PodLogSnapshot(
                new ResourceIdentity(request.SessionId, "Pod", request.Namespace, request.PodName),
                request.Container,
                request.Previous,
                Math.Max(1, request.TailLines),
                PodlordText.NowUtcString(clock),
                text);
            StoreLog(cacheKey, snapshot);
            return snapshot;
        }
        catch (KubernetesStatusException ex)
        {
            throw PodlordException.KubernetesApi(connection.Context.Name, "Pod logs", ex.Message, ex);
        }
        catch (HttpRequestException ex)
        {
            throw PodlordException.KubernetesApi(connection.Context.Name, "Pod logs", HttpFailureMessage(ex), ex);
        }
    }

    public async Task<PodlordPortForward> StartPortForwardAsync(
        PortForwardRequest request,
        CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrWhiteSpace(request.Namespace))
        {
            throw PodlordException.InvalidInput("Namespace is required for port forward.", "Select a namespaced Pod or Service.");
        }

        if (string.IsNullOrWhiteSpace(request.Name))
        {
            throw PodlordException.InvalidInput("Resource name is required for port forward.", "Select a Pod or Service first.");
        }

        if (request.LocalPort <= 0 || request.RemotePort <= 0)
        {
            throw PodlordException.InvalidInput("Port numbers must be positive.", "Choose a valid local and container port.");
        }

        var connection = state.SessionConnection(request.SessionId);
        if (string.IsNullOrWhiteSpace(connection.Context.Server))
        {
            throw PodlordException.KubernetesConfig(connection.Context.Name, "cluster server is missing");
        }

        using var client = CreateClient(connection);
        var target = await ResolvePortForwardTargetAsync(client, request, cancellationToken).ConfigureAwait(false);
        var forward = new PodlordPortForward(
            connection.KubeconfigPath,
            connection.Context.Name,
            target.Namespace,
            target.PodName,
            request.LocalPort,
            target.RemotePort);
        await forward.StartAsync(cancellationToken).ConfigureAwait(false);
        RecordAudit(
            DateTimeOffset.UtcNow,
            "WS",
            forward.KubernetesPath,
            KubernetesRequestPriority.Foreground,
            HttpStatusCode.SwitchingProtocols,
            TimeSpan.Zero,
            "port-forward listening");
        return forward;
    }

    private async Task<ResolvedPortForwardTarget> ResolvePortForwardTargetAsync(
        HttpClient client,
        PortForwardRequest request,
        CancellationToken cancellationToken)
    {
        if (request.Kind.Equals("Pod", StringComparison.OrdinalIgnoreCase))
        {
            return new ResolvedPortForwardTarget(request.Namespace, request.Name, request.RemotePort);
        }

        if (!request.Kind.Equals("Service", StringComparison.OrdinalIgnoreCase))
        {
            throw PodlordException.InvalidInput("Port forward supports Pods and Services.", "Select a Running pod or a namespaced service.");
        }

        var servicePath = $"/api/v1/namespaces/{Escape(request.Namespace)}/services/{Escape(request.Name)}";
        var service = await GetJsonQueuedAsync(client, servicePath, KubernetesRequestPriority.Foreground, cancellationToken).ConfigureAwait(false);
        var selector = LabelSelector(service);
        if (selector.Count == 0)
        {
            throw PodlordException.InvalidInput(
                $"Service {request.Name} has no selector.",
                "Select a Pod directly or choose a Service with backing pods.");
        }

        var podsPath = $"/api/v1/namespaces/{Escape(request.Namespace)}/pods?labelSelector={Escape(LabelSelectorExpression(selector))}";
        var pods = await GetJsonQueuedAsync(client, podsPath, KubernetesRequestPriority.Foreground, cancellationToken).ConfigureAwait(false);
        var pod = Items(pods).FirstOrDefault(item => Text(item, "/status/phase", "-").Equals("Running", StringComparison.OrdinalIgnoreCase))
                  ?? throw PodlordException.InvalidInput(
                      $"Service {request.Name} has no Running backing pods.",
                      "Wait for a pod to become Running or select another resource.");
        var podName = Text(pod, "/metadata/name", "-");
        var remotePort = ResolveServicePort(service, pod, request.RemotePort);
        return new ResolvedPortForwardTarget(request.Namespace, podName, remotePort);
    }

    private async Task<IReadOnlyList<FlatResourceRow>> ListRowsForSpec(
        HttpClient client,
        SessionConnection connection,
        ResourceSpec spec,
        string? ns,
        bool forceRefresh,
        KubernetesRequestPriority priority,
        CancellationToken cancellationToken)
    {
        var key = new ResourceListCacheKey(connection.Session.Id, spec.Kind, ns);
        if (!forceRefresh && TryGetListRows(key, out var cached))
        {
            return cached;
        }

        if (!forceRefresh && TryGetBackoffRows(key, out var staleRows))
        {
            return staleRows;
        }

        var document = await GetJsonQueuedAsync(client, spec.ListPathForNamespace(ns), priority, cancellationToken).ConfigureAwait(false);
        var rows = Items(document)
            .Select(item => RowFromObject(connection, spec.Kind, item))
            .ToList();
        StoreListRows(key, rows);
        return rows;
    }

    private ResourceExplorerSnapshot SnapshotFromRows(
        SessionConnection connection,
        IReadOnlyList<FlatResourceRow> rows,
        IReadOnlyList<ResourceListFailure> failures,
        ResourceQuery query)
    {
        var filtered = ResourceFilterMatcher.FilterRows(rows, query with { Limit = 5_000 })
            .OrderBy(row => row.Kind, StringComparer.Ordinal)
            .ThenBy(row => row.Namespace ?? string.Empty, StringComparer.Ordinal)
            .ThenBy(row => row.Name, StringComparer.Ordinal)
            .Take(ResourceFilterMatcher.NormalizeLimit(query.Limit))
            .ToList();

        var optionRows = rows.Count == 0 ? filtered : rows;
        var freshness = failures.Count > 0
            ? FreshnessState.Stale
            : rows.Count == 0 ? FreshnessState.Unknown : RowsFreshness(rows);
        return new ResourceExplorerSnapshot(
            connection.Session.Id,
            connection.Context.ContextId,
            connection.Context.ClusterName,
            connection.Context.Name,
            connection.Session.NamespaceScope,
            PodlordText.NowUtcString(clock),
            freshness,
            filtered,
            optionRows.Select(row => row.Namespace ?? "cluster").Distinct(StringComparer.Ordinal).Order().ToList(),
            ResourceSpecs.Listable.Select(spec => spec.Kind).Distinct(StringComparer.Ordinal).Order().ToList(),
            optionRows.Select(row => row.Status).Distinct(StringComparer.Ordinal).Order().ToList(),
            optionRows.Select(row => row.Node).OfType<string>().Where(value => !string.IsNullOrWhiteSpace(value)).Distinct(StringComparer.Ordinal).Order().ToList(),
            optionRows.Select(row => row.ImageSummary).Where(value => value != "-").Distinct(StringComparer.Ordinal).Order().ToList(),
            optionRows.Select(row => row.Owner).OfType<string>().Where(value => !string.IsNullOrWhiteSpace(value)).Distinct(StringComparer.Ordinal).Order().ToList(),
            optionRows.Select(row => row.Ready).Where(value => value != "-").Distinct(StringComparer.Ordinal).Order().ToList(),
            failures);
    }

    private static FreshnessState RowsFreshness(IReadOnlyList<FlatResourceRow> rows)
    {
        return rows.Any(row => row.Freshness == FreshnessState.Stale) ? FreshnessState.Stale : FreshnessState.Fresh;
    }

    private static ResourceQuery UnfilteredSnapshotQuery(ResourceQuery query)
    {
        return query with
        {
            Search = null,
            Id = null,
            Status = null,
            Node = null,
            Image = null,
            Ready = null,
            Restarts = null,
            Owner = null,
            ProblemsOnly = false,
            ActivityOnly = false,
            Limit = 5_000
        };
    }

    private static IEnumerable<ResourceSpec> PlannedSpecs(ResourceQuery query)
    {
        var specs = string.IsNullOrWhiteSpace(query.Kind)
            ? ResourceSpecs.Listable
            : ResourceSpecs.Listable.Where(spec => ResourceFilterMatcher.MatchesText(spec.Kind, query.Kind)).ToList();
        return query.ProblemsOnly && string.IsNullOrWhiteSpace(query.Kind)
            ? specs.Where(spec => spec.DefaultProblemScan)
            : specs;
    }

    private static IReadOnlyList<string?> PlannedNamespaces(ResourceQuery query)
    {
        return ResourceFilterMatcher.ExactTerms(query.Namespace)
            .Where(term => !term.Equals("cluster", StringComparison.OrdinalIgnoreCase))
            .Select(term => (string?)term)
            .Distinct(StringComparer.Ordinal)
            .ToList();
    }

    private bool TryGetListRows(ResourceListCacheKey key, out IReadOnlyList<FlatResourceRow> rows)
    {
        lock (cacheLock)
        {
            if (listCache.TryGetValue(key, out var entry) && IsFresh(entry.StoredAt, ListCacheTtl))
            {
                rows = entry.Rows;
                return true;
            }
        }

        rows = Array.Empty<FlatResourceRow>();
        return false;
    }

    private IReadOnlyList<FlatResourceRow> GetCachedRows(string sessionId, string kind, string? ns)
    {
        var key = new ResourceListCacheKey(sessionId, kind, ns);
        lock (cacheLock)
        {
            if (!listCache.TryGetValue(key, out var entry))
            {
                return Array.Empty<FlatResourceRow>();
            }

            if (!IsFresh(entry.StoredAt, ListDisplayCacheTtl))
            {
                return Array.Empty<FlatResourceRow>();
            }

            return IsFresh(entry.StoredAt, ListCacheTtl)
                ? entry.Rows
                : entry.Rows.Select(row => row with { Freshness = FreshnessState.Stale }).ToList();
        }
    }

    private bool TryGetBackoffRows(ResourceListCacheKey key, out IReadOnlyList<FlatResourceRow> rows)
    {
        lock (cacheLock)
        {
            if (backoffUntil is { } until && DateTimeOffset.UtcNow < until && listCache.TryGetValue(key, out var entry))
            {
                rows = entry.Rows.Select(row => row with { Freshness = FreshnessState.Stale }).ToList();
                return true;
            }
        }

        rows = Array.Empty<FlatResourceRow>();
        return false;
    }

    private void StoreListRows(ResourceListCacheKey key, IReadOnlyList<FlatResourceRow> rows)
    {
        lock (cacheLock)
        {
            listCache[key] = new ResourceListCacheEntry(DateTimeOffset.UtcNow, rows);
        }
    }

    private bool TryGetDetail(ResourceDetailCacheKey key, out ResourceDetail detail)
    {
        lock (cacheLock)
        {
            if (detailCache.TryGetValue(key, out var entry) && IsFresh(entry.StoredAt, DetailCacheTtl))
            {
                detail = entry.Detail;
                return true;
            }
        }

        detail = null!;
        return false;
    }

    private void StoreDetail(ResourceDetailCacheKey key, ResourceDetail detail)
    {
        lock (cacheLock)
        {
            detailCache[key] = new ResourceDetailCacheEntry(DateTimeOffset.UtcNow, detail);
        }
    }

    private void RemoveCachedResource(string sessionId, ResourceIdentity identity)
    {
        lock (cacheLock)
        {
            detailCache.Remove(new ResourceDetailCacheKey(sessionId, identity.Kind, identity.Namespace, identity.Name));
            foreach (var key in listCache.Keys.Where(key =>
                         key.SessionId.Equals(sessionId, StringComparison.Ordinal)
                         && key.Kind.Equals(identity.Kind, StringComparison.OrdinalIgnoreCase)
                         && string.Equals(key.Namespace, identity.Namespace, StringComparison.Ordinal)).ToList())
            {
                var entry = listCache[key];
                var rows = entry.Rows
                    .Where(row => !row.Name.Equals(identity.Name, StringComparison.Ordinal))
                    .ToList();
                listCache[key] = new ResourceListCacheEntry(entry.StoredAt, rows);
            }
        }
    }

    private bool TryGetLog(PodLogCacheKey key, out PodLogSnapshot snapshot)
    {
        lock (cacheLock)
        {
            if (logCache.TryGetValue(key, out var entry) && IsFresh(entry.StoredAt, LogCacheTtl))
            {
                snapshot = entry.Snapshot;
                return true;
            }
        }

        snapshot = null!;
        return false;
    }

    private void StoreLog(PodLogCacheKey key, PodLogSnapshot snapshot)
    {
        lock (cacheLock)
        {
            logCache[key] = new PodLogCacheEntry(DateTimeOffset.UtcNow, snapshot);
        }
    }

    private async Task<IReadOnlyList<FlatResourceRow>> EnrichPulseMetricsAsync(
        HttpClient client,
        SessionConnection connection,
        IReadOnlyList<FlatResourceRow> rows,
        KubernetesRequestPriority priority,
        CancellationToken cancellationToken)
    {
        if (!rows.Any(row => row.Kind is "Pod" or "Node"))
        {
            return rows;
        }

        var podNamespaces = rows
            .Where(row => row.Kind == "Pod" && !string.IsNullOrWhiteSpace(row.Namespace))
            .Select(row => row.Namespace!)
            .Distinct(StringComparer.Ordinal)
            .Order(StringComparer.Ordinal)
            .ToArray();
        var snapshot = await GetPulseSnapshotAsync(client, connection.Session.Id, podNamespaces, priority, cancellationToken).ConfigureAwait(false);
        return rows
            .Select(row => row.Kind switch
            {
                "Pod" when snapshot.PodUsage.TryGetValue(PulsePodKey(row.Namespace, row.Name), out var usage) =>
                    row with { Pulse = row.Pulse.WithLiveUsage(usage.CpuMillicores, usage.MemoryBytes, snapshot.SourceBadge, snapshot.Tooltip) },
                "Node" when snapshot.NodeUsage.TryGetValue(row.Name, out var usage) =>
                    row with { Pulse = row.Pulse.WithLiveUsage(usage.CpuMillicores, usage.MemoryBytes, snapshot.SourceBadge, snapshot.Tooltip) },
                "Pod" or "Node" => row with { Pulse = row.Pulse with { SourceBadge = snapshot.SourceBadge, Tooltip = snapshot.Tooltip } },
                _ => row
            })
            .ToList();
    }

    private async Task<ResourcePulseSnapshot> GetPulseSnapshotAsync(
        HttpClient client,
        string sessionId,
        IReadOnlyList<string> podNamespaces,
        KubernetesRequestPriority priority,
        CancellationToken cancellationToken)
    {
        var globalCacheKey = PulseCacheKey(sessionId, null);
        var scopedCacheKey = PulseCacheKey(sessionId, podNamespaces);
        var needsPodMetrics = podNamespaces.Count > 0;
        lock (cacheLock)
        {
            if (needsPodMetrics
                && pulseCache.TryGetValue(scopedCacheKey, out var scopedEntry)
                && IsFresh(scopedEntry.StoredAt, scopedEntry.Snapshot.Available ? PulseCacheTtl : PulseUnavailableTtl))
            {
                return scopedEntry.Snapshot;
            }

            if (pulseCache.TryGetValue(globalCacheKey, out var globalEntry)
                && IsFresh(globalEntry.StoredAt, globalEntry.Snapshot.Available ? PulseCacheTtl : PulseUnavailableTtl)
                && (!needsPodMetrics || globalEntry.Snapshot.PodUsage.Count > 0))
            {
                return globalEntry.Snapshot;
            }
        }

        var snapshot = await FetchPulseSnapshotAsync(client, podNamespaces, priority, cancellationToken).ConfigureAwait(false);
        lock (cacheLock)
        {
            pulseCache[snapshot.NamespaceScoped ? scopedCacheKey : globalCacheKey] = new ResourcePulseCacheEntry(DateTimeOffset.UtcNow, snapshot);
        }

        return snapshot;
    }

    private async Task<ResourcePulseSnapshot> FetchPulseSnapshotAsync(
        HttpClient client,
        IReadOnlyList<string> podNamespaces,
        KubernetesRequestPriority priority,
        CancellationToken cancellationToken)
    {
        Dictionary<string, LivePulseUsage> podUsage = new(StringComparer.Ordinal);
        Dictionary<string, LivePulseUsage> nodeUsage = new(StringComparer.Ordinal);
        var unavailableReason = string.Empty;
        var namespaceScoped = false;

        try
        {
            var podDocument = await GetJsonQueuedAsync(client, "/apis/metrics.k8s.io/v1beta1/pods", priority, cancellationToken).ConfigureAwait(false);
            podUsage = Items(podDocument)
                .Select(PodMetricUsage)
                .Where(item => item.Key.Length > 0)
                .ToDictionary(item => item.Key, item => item.Usage, StringComparer.Ordinal);
            if (podUsage.Count == 0 && podNamespaces.Count > 0)
            {
                namespaceScoped = true;
                var fallback = await FetchNamespacePodMetricsAsync(
                    client,
                    podNamespaces,
                    priority,
                    "Cluster-wide pod metrics returned no pod usage",
                    cancellationToken).ConfigureAwait(false);
                podUsage = fallback.Usage;
                unavailableReason = fallback.Message;
            }
        }
        catch (KubernetesStatusException ex) when (ex.StatusCode == HttpStatusCode.Forbidden && podNamespaces.Count > 0)
        {
            namespaceScoped = true;
            var fallback = await FetchNamespacePodMetricsAsync(
                client,
                podNamespaces,
                priority,
                $"Pod metrics unavailable: {ShortStatus(ex)}",
                cancellationToken).ConfigureAwait(false);
            podUsage = fallback.Usage;
            unavailableReason = fallback.Message;
        }
        catch (KubernetesStatusException ex) when (IsOptionalMetricsFailure(ex))
        {
            unavailableReason = $"Pod metrics unavailable: {ShortStatus(ex)}";
        }

        try
        {
            var nodeDocument = await GetJsonQueuedAsync(client, "/apis/metrics.k8s.io/v1beta1/nodes", priority, cancellationToken).ConfigureAwait(false);
            nodeUsage = Items(nodeDocument)
                .Select(NodeMetricUsage)
                .Where(item => item.Key.Length > 0)
                .ToDictionary(item => item.Key, item => item.Usage, StringComparer.Ordinal);
        }
        catch (KubernetesStatusException ex) when (IsOptionalMetricsFailure(ex))
        {
            unavailableReason = string.IsNullOrWhiteSpace(unavailableReason)
                ? $"Node metrics unavailable: {ShortStatus(ex)}"
                : $"{unavailableReason}; node metrics unavailable: {ShortStatus(ex)}";
        }

        var available = podUsage.Count > 0 || nodeUsage.Count > 0;
        return new ResourcePulseSnapshot(
            available,
            available ? "API LIVE" : "API",
            available
                ? namespaceScoped
                    ? NamespaceScopedPulseTooltip(unavailableReason)
                    : "LIVE uses metrics.k8s.io current CPU and memory; API uses object status, limits, requests, and capacity."
                : string.IsNullOrWhiteSpace(unavailableReason)
                    ? "metrics.k8s.io returned no usage; API status, limits, requests, and capacity are still shown."
                    : $"{unavailableReason}. API status, limits, requests, and capacity are still shown.",
            podUsage,
            nodeUsage,
            namespaceScoped);
    }

    private static string NamespaceScopedPulseTooltip(string reason)
    {
        var prefix = string.IsNullOrWhiteSpace(reason) ? string.Empty : $"{reason}. ";
        return $"{prefix}LIVE uses namespace-scoped metrics.k8s.io current CPU and memory; API uses object status, limits, requests, and capacity.";
    }

    private async Task<(Dictionary<string, LivePulseUsage> Usage, string Message)> FetchNamespacePodMetricsAsync(
        HttpClient client,
        IReadOnlyList<string> podNamespaces,
        KubernetesRequestPriority priority,
        string reason,
        CancellationToken cancellationToken)
    {
        Dictionary<string, LivePulseUsage> podUsage = new(StringComparer.Ordinal);
        var failures = new List<string>();
        foreach (var ns in podNamespaces)
        {
            try
            {
                var path = $"/apis/metrics.k8s.io/v1beta1/namespaces/{Uri.EscapeDataString(ns)}/pods";
                var namespaceDocument = await GetJsonQueuedAsync(client, path, priority, cancellationToken).ConfigureAwait(false);
                foreach (var item in Items(namespaceDocument).Select(PodMetricUsage).Where(item => item.Key.Length > 0))
                {
                    podUsage[item.Key] = item.Usage;
                }
            }
            catch (KubernetesStatusException namespaceEx) when (IsOptionalMetricsFailure(namespaceEx))
            {
                failures.Add($"{ns}: {ShortStatus(namespaceEx)}");
            }
        }

        if (podUsage.Count > 0)
        {
            return (podUsage, $"{reason}; loaded namespace-scoped pod metrics for {podUsage.Count} pod(s)");
        }

        return failures.Count == 0
            ? (podUsage, reason)
            : (podUsage, $"{reason}; namespace fallback failed ({string.Join("; ", failures.Take(4))})");
    }

    private static string PulseCacheKey(string sessionId, IReadOnlyList<string>? podNamespaces)
    {
        return podNamespaces is null || podNamespaces.Count == 0
            ? $"{sessionId}:metrics:global"
            : $"{sessionId}:metrics:ns:{string.Join(",", podNamespaces)}";
    }

    private void StorePulseRows(string sessionId, IReadOnlyList<FlatResourceRow> rows)
    {
        var byId = rows.ToDictionary(row => row.Id, StringComparer.Ordinal);
        var touched = false;
        lock (cacheLock)
        {
            foreach (var key in listCache.Keys.Where(key => key.SessionId.Equals(sessionId, StringComparison.Ordinal)).ToList())
            {
                var entry = listCache[key];
                var changed = false;
                var merged = entry.Rows.Select(row =>
                {
                    if (!byId.TryGetValue(row.Id, out var enriched))
                    {
                        return row;
                    }

                    changed = true;
                    return enriched;
                }).ToList();
                if (!changed)
                {
                    continue;
                }

                listCache[key] = new ResourceListCacheEntry(entry.StoredAt, merged);
                touched = true;
            }

            if (touched)
            {
                return;
            }

            foreach (var group in rows.GroupBy(row => new ResourceListCacheKey(sessionId, row.Kind, row.Namespace)))
            {
                listCache[group.Key] = new ResourceListCacheEntry(DateTimeOffset.UtcNow, group.ToList());
            }
        }
    }

    private async Task<T> EnqueueRequest<T>(
        KubernetesRequestPriority priority,
        Func<CancellationToken, Task<T>> work,
        CancellationToken cancellationToken)
    {
        var completion = new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);
        var request = new QueuedRequest<T>(work, completion, cancellationToken);
        lock (queueLock)
        {
            requestQueue.Enqueue(request, new QueuedRequestOrder((int)priority, ++requestSequence));
            if (!queueRunning)
            {
                queueRunning = true;
                _ = Task.Run(ProcessQueue);
            }
        }

        using var registration = cancellationToken.Register(
            static state => ((TaskCompletionSource<T>)state!).TrySetCanceled(),
            completion);
        return await completion.Task.ConfigureAwait(false);
    }

    private async Task ProcessQueue()
    {
        while (true)
        {
            IQueuedRequest request;
            lock (queueLock)
            {
                if (!requestQueue.TryDequeue(out request!, out _))
                {
                    queueRunning = false;
                    return;
                }
            }

            await request.Execute(this).ConfigureAwait(false);
        }
    }

    internal async Task<RequestSlotLease> WaitForRequestSlot(CancellationToken cancellationToken)
    {
        await SharedRequestGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            while (BackoffDelay() is { } delay)
            {
                await Task.Delay(delay, cancellationToken).ConfigureAwait(false);
            }

            var now = DateTimeOffset.UtcNow;
            var last = LastRequestAt();
            var nextAllowed = last + RequestSpacing();
            if (nextAllowed > now)
            {
                await Task.Delay(nextAllowed - now, cancellationToken).ConfigureAwait(false);
            }

            MarkRequestStarted(DateTimeOffset.UtcNow);
            return new RequestSlotLease(SharedRequestGate);
        }
        catch
        {
            SharedRequestGate.Release();
            throw;
        }
    }

    private TimeSpan? BackoffDelay()
    {
        var now = DateTimeOffset.UtcNow;
        DateTimeOffset? until;
        lock (SharedRateLock)
        {
            until = sharedBackoffUntil is { } shared && shared > now ? shared : null;
        }

        lock (cacheLock)
        {
            if (backoffUntil is { } local && local > now && (until is null || local > until))
            {
                until = local;
            }
        }

        return until is { } value && value > now ? value - now : null;
    }

    private TimeSpan RequestSpacing()
    {
        var dynamicSpacing = handler is null ? MinimumRequestSpacing : TimeSpan.Zero;
        var hardLimit = state.Settings().RequestHardLimitPerMinute;
        if (hardLimit <= 0)
        {
            return dynamicSpacing;
        }

        var hardLimitSpacing = TimeSpan.FromSeconds(60d / Math.Clamp(hardLimit, 1, 60_000));
        return hardLimitSpacing > dynamicSpacing ? hardLimitSpacing : dynamicSpacing;
    }

    private DateTimeOffset LastRequestAt()
    {
        lock (SharedRateLock)
        {
            return sharedLastRequestAt > lastRequestAt ? sharedLastRequestAt : lastRequestAt;
        }
    }

    private void MarkRequestStarted(DateTimeOffset startedAt)
    {
        lock (SharedRateLock)
        {
            sharedLastRequestAt = startedAt;
        }

        lastRequestAt = startedAt;
        lock (telemetryLock)
        {
            requestStarts.Enqueue(startedAt);
            PruneRequestStarts(startedAt);
        }
    }

    private void PruneRequestStarts(DateTimeOffset now)
    {
        while (requestStarts.Count > 0 && now - requestStarts.Peek() > TimeSpan.FromMinutes(1))
        {
            requestStarts.Dequeue();
        }
    }

    private DateTimeOffset? BackoffUntil(DateTimeOffset now)
    {
        DateTimeOffset? until;
        lock (SharedRateLock)
        {
            until = sharedBackoffUntil is { } shared && shared > now ? shared : null;
        }

        lock (cacheLock)
        {
            if (backoffUntil is { } local && local > now && (until is null || local > until))
            {
                until = local;
            }
        }

        return until;
    }

    private void SetBackoff(TimeSpan delay)
    {
        var until = DateTimeOffset.UtcNow + delay;
        lock (SharedRateLock)
        {
            if (sharedBackoffUntil is null || until > sharedBackoffUntil)
            {
                sharedBackoffUntil = until;
            }
        }

        lock (cacheLock)
        {
            if (backoffUntil is null || until > backoffUntil)
            {
                backoffUntil = until;
            }
        }
    }

    public async Task<ResourceDetail> ApplyResourceYamlAsync(
        ResourceIdentity identity,
        string yaml,
        CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrWhiteSpace(identity.Kind))
        {
            throw PodlordException.InvalidInput("Resource kind is required.", "Select a resource first.");
        }

        if (string.IsNullOrWhiteSpace(identity.Name))
        {
            throw PodlordException.InvalidInput("Resource name is required.", "Select a resource first.");
        }

        if (string.IsNullOrWhiteSpace(yaml))
        {
            throw PodlordException.InvalidInput("YAML is empty.", "Load a resource before applying.");
        }

        var connection = state.SessionConnection(identity.SessionId);
        var spec = ResourceSpecs.ForKind(identity.Kind) ?? throw PodlordException.UnsupportedResourceKind(identity.Kind);
        if (spec.Namespaced && string.IsNullOrWhiteSpace(identity.Namespace))
        {
            throw PodlordException.InvalidInput(
                $"Namespace is required for {identity.Kind}.",
                "Select the resource from the explorer so Podlord can bind the namespace.");
        }

        using var client = CreateClient(connection);
        try
        {
            var path = spec.DetailPath(identity.Namespace, identity.Name) + "?fieldManager=podlord";
            using var content = new StringContent(yaml, Encoding.UTF8);
            content.Headers.ContentType = new MediaTypeHeaderValue("application/apply-patch+yaml");
            var document = await SendJsonQueuedAsync(client, HttpMethod.Patch, path, content, KubernetesRequestPriority.Foreground, cancellationToken).ConfigureAwait(false);
            var sanitized = SanitizeObject(document.DeepClone()!.AsObject(), identity.Kind);
            var row = RowFromObject(connection, identity.Kind, document.AsObject());
            var events = await RelatedEvents(client, identity, KubernetesRequestPriority.Foreground, cancellationToken).ConfigureAwait(false);
            var detail = new ResourceDetail(
                identity,
                row.Status,
                FreshnessState.Fresh,
                ToYaml(sanitized),
                SummaryItems(document.AsObject(), row),
                ConditionItems(document.AsObject()),
                events,
                ValueItems(document.AsObject(), identity.Kind));
            StoreDetail(new ResourceDetailCacheKey(connection.Session.Id, identity.Kind, identity.Namespace, identity.Name), detail);
            return detail;
        }
        catch (KubernetesStatusException ex)
        {
            throw PodlordException.KubernetesApi(connection.Context.Name, identity.Kind, ex.Message, ex);
        }
        catch (HttpRequestException ex)
        {
            throw PodlordException.KubernetesApi(connection.Context.Name, identity.Kind, HttpFailureMessage(ex), ex);
        }
    }

    public async Task DeleteResourceAsync(ResourceIdentity identity, CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrWhiteSpace(identity.Kind))
        {
            throw PodlordException.InvalidInput("Resource kind is required.", "Select a resource first.");
        }

        if (string.IsNullOrWhiteSpace(identity.Name))
        {
            throw PodlordException.InvalidInput("Resource name is required.", "Select a resource first.");
        }

        var connection = state.SessionConnection(identity.SessionId);
        var spec = ResourceSpecs.ForKind(identity.Kind) ?? throw PodlordException.UnsupportedResourceKind(identity.Kind);
        if (spec.Namespaced && string.IsNullOrWhiteSpace(identity.Namespace))
        {
            throw PodlordException.InvalidInput(
                $"Namespace is required for {identity.Kind}.",
                "Select the resource from the explorer so Podlord can bind the namespace.");
        }

        using var client = CreateClient(connection);
        try
        {
            var path = spec.DetailPath(identity.Namespace, identity.Name);
            await EnqueueRequest(
                KubernetesRequestPriority.Foreground,
                token => SendTextRawAsync(client, HttpMethod.Delete, path, null, KubernetesRequestPriority.Foreground, token),
                cancellationToken).ConfigureAwait(false);
            RemoveCachedResource(connection.Session.Id, identity);
        }
        catch (KubernetesStatusException ex)
        {
            throw PodlordException.KubernetesApi(connection.Context.Name, identity.Kind, ex.Message, ex);
        }
        catch (HttpRequestException ex)
        {
            throw PodlordException.KubernetesApi(connection.Context.Name, identity.Kind, HttpFailureMessage(ex), ex);
        }
    }

    private bool IsFresh(DateTimeOffset storedAt, TimeSpan ttl)
    {
        return DateTimeOffset.UtcNow - storedAt <= ttl;
    }

    private HttpClient CreateClient(SessionConnection connection)
    {
        if (string.IsNullOrWhiteSpace(connection.Context.Server))
        {
            throw PodlordException.KubernetesConfig(connection.Context.Name, "cluster server is missing");
        }

        var auth = KubeconfigAuthLoader.Load(connection.KubeconfigPath, connection.Context.Name);
        var client = handler is null
            ? new HttpClient(CreateHandler(auth), disposeHandler: true)
            : new HttpClient(handler, disposeHandler: false);
        client.BaseAddress = new Uri(connection.Context.Server, UriKind.Absolute);
        client.Timeout = TimeSpan.FromSeconds(20);
        if (auth.BearerToken is { Length: > 0 } token)
        {
            client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", token);
        }

        if (auth.BasicAuthHeader is { Length: > 0 } basic)
        {
            client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Basic", basic);
        }

        return client;
    }

    private static HttpClientHandler CreateHandler(KubeconfigAuth auth)
    {
        var handler = new HttpClientHandler();
        if (auth.ClientCertificate is not null)
        {
            handler.ClientCertificates.Add(auth.ClientCertificate);
        }

        if (auth.SkipTlsVerify)
        {
            handler.ServerCertificateCustomValidationCallback = HttpClientHandler.DangerousAcceptAnyServerCertificateValidator;
        }
        else if (auth.CertificateAuthority is not null)
        {
            handler.ServerCertificateCustomValidationCallback = (_, certificate, _, sslErrors) =>
                ValidateServerCertificate(certificate, auth.CertificateAuthority, sslErrors);
        }

        return handler;
    }

    private static bool ValidateServerCertificate(
        X509Certificate2? certificate,
        X509Certificate2 certificateAuthority,
        SslPolicyErrors sslErrors)
    {
        if (certificate is null)
        {
            return false;
        }

        if (sslErrors == SslPolicyErrors.None)
        {
            return true;
        }

        using var chain = new X509Chain();
        chain.ChainPolicy.TrustMode = X509ChainTrustMode.CustomRootTrust;
        chain.ChainPolicy.CustomTrustStore.Add(certificateAuthority);
        chain.ChainPolicy.RevocationMode = X509RevocationMode.NoCheck;
        chain.ChainPolicy.VerificationFlags = X509VerificationFlags.AllowUnknownCertificateAuthority;
        return chain.Build(certificate);
    }

    private async Task<JsonObject> GetJsonQueuedAsync(
        HttpClient client,
        string path,
        KubernetesRequestPriority priority,
        CancellationToken cancellationToken)
    {
        return await SendJsonQueuedAsync(client, HttpMethod.Get, path, null, priority, cancellationToken).ConfigureAwait(false);
    }

    private async Task<string> GetTextQueuedAsync(
        HttpClient client,
        string path,
        KubernetesRequestPriority priority,
        CancellationToken cancellationToken)
    {
        return await EnqueueRequest(priority, token => SendTextRawAsync(client, HttpMethod.Get, path, null, priority, token), cancellationToken).ConfigureAwait(false);
    }

    private async Task<JsonObject> SendJsonQueuedAsync(
        HttpClient client,
        HttpMethod method,
        string path,
        HttpContent? content,
        KubernetesRequestPriority priority,
        CancellationToken cancellationToken)
    {
        return await EnqueueRequest(priority, token => SendJsonRawAsync(client, method, path, content, priority, token), cancellationToken).ConfigureAwait(false);
    }

    private async Task<JsonObject> SendJsonRawAsync(
        HttpClient client,
        HttpMethod method,
        string path,
        HttpContent? content,
        KubernetesRequestPriority priority,
        CancellationToken cancellationToken)
    {
        var startedAt = DateTimeOffset.UtcNow;
        var stopwatch = Stopwatch.StartNew();
        try
        {
            using var request = new HttpRequestMessage(method, path) { Content = content };
            using var response = await client.SendAsync(request, cancellationToken).ConfigureAwait(false);
            var body = await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
            RecordAudit(startedAt, method.Method, path, priority, response.StatusCode, stopwatch.Elapsed, response.IsSuccessStatusCode ? "ok" : "failed");
            if (!response.IsSuccessStatusCode)
            {
                var retryAfter = RetryAfter(response);
                if (response.StatusCode == HttpStatusCode.TooManyRequests && retryAfter is { } delay)
                {
                    SetBackoff(delay);
                }

                throw new KubernetesStatusException(response.StatusCode, Sanitize(body), retryAfter);
            }

            return JsonNode.Parse(body)?.AsObject()
                   ?? throw new HttpRequestException($"Invalid JSON response for {path}");
        }
        catch (HttpRequestException)
        {
            RecordAudit(startedAt, method.Method, path, priority, null, stopwatch.Elapsed, "network error");
            throw;
        }
    }

    private async Task<string> SendTextRawAsync(
        HttpClient client,
        HttpMethod method,
        string path,
        HttpContent? content,
        KubernetesRequestPriority priority,
        CancellationToken cancellationToken)
    {
        var startedAt = DateTimeOffset.UtcNow;
        var stopwatch = Stopwatch.StartNew();
        try
        {
            using var request = new HttpRequestMessage(method, path) { Content = content };
            using var response = await client.SendAsync(request, cancellationToken).ConfigureAwait(false);
            var body = await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
            RecordAudit(startedAt, method.Method, path, priority, response.StatusCode, stopwatch.Elapsed, response.IsSuccessStatusCode ? "ok" : "failed");
            if (!response.IsSuccessStatusCode)
            {
                var retryAfter = RetryAfter(response);
                if (response.StatusCode == HttpStatusCode.TooManyRequests && retryAfter is { } delay)
                {
                    SetBackoff(delay);
                }

                throw new KubernetesStatusException(response.StatusCode, Sanitize(body), retryAfter);
            }

            return body;
        }
        catch (HttpRequestException)
        {
            RecordAudit(startedAt, method.Method, path, priority, null, stopwatch.Elapsed, "network error");
            throw;
        }
    }

    private void RecordAudit(
        DateTimeOffset startedAt,
        string method,
        string path,
        KubernetesRequestPriority priority,
        HttpStatusCode? status,
        TimeSpan duration,
        string outcome)
    {
        var entry = new KubernetesRequestAuditEntry(
            startedAt.ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss.fffffff'Z'"),
            method,
            path,
            priority.ToString(),
            status is null ? "-" : $"{(int)status.Value} {status.Value}",
            $"{Math.Max(0, duration.TotalMilliseconds):0}ms",
            outcome);
        lock (telemetryLock)
        {
            requestAudit.Enqueue(entry);
            while (requestAudit.Count > 256)
            {
                requestAudit.Dequeue();
            }
        }
    }

    private static TimeSpan? RetryAfter(HttpResponseMessage response)
    {
        if (response.Headers.RetryAfter?.Delta is { } delta)
        {
            return delta;
        }

        if (response.Headers.RetryAfter?.Date is { } date)
        {
            var delay = date - DateTimeOffset.UtcNow;
            return delay > TimeSpan.Zero ? delay : TimeSpan.Zero;
        }

        return response.StatusCode == HttpStatusCode.TooManyRequests ? TimeSpan.FromSeconds(1) : null;
    }

    private async Task<IReadOnlyList<EventSummary>> RelatedEvents(
        HttpClient client,
        ResourceIdentity identity,
        KubernetesRequestPriority priority,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(identity.Namespace))
        {
            return Array.Empty<EventSummary>();
        }

        var path = $"/api/v1/namespaces/{Escape(identity.Namespace)}/events?fieldSelector=involvedObject.name={Escape(identity.Name)}";
        try
        {
            var document = await GetJsonQueuedAsync(client, path, priority, cancellationToken).ConfigureAwait(false);
            return Items(document)
                .Select(item => new EventSummary(
                    Text(item, "/type", "-"),
                    Text(item, "/reason", "-"),
                    Text(item, "/message", "-"),
                    Int(item, "/count"),
                    Text(item, "/lastTimestamp", Text(item, "/eventTime", "-"))))
                .ToList();
        }
        catch (KubernetesStatusException ex) when (ex.StatusCode == HttpStatusCode.NotFound || ex.StatusCode == HttpStatusCode.Forbidden)
        {
            return Array.Empty<EventSummary>();
        }
    }

    internal static IEnumerable<JsonObject> Items(JsonObject document)
    {
        return document["items"]?.AsArray().OfType<JsonObject>() ?? Array.Empty<JsonObject>();
    }

    internal static (string Key, LivePulseUsage Usage) PodMetricUsage(JsonObject item)
    {
        var ns = OptionalText(item, "/metadata/namespace");
        var name = OptionalText(item, "/metadata/name");
        if (string.IsNullOrWhiteSpace(ns) || string.IsNullOrWhiteSpace(name))
        {
            return (string.Empty, LivePulseUsage.Empty);
        }

        var usage = item["containers"]?.AsArray()
            .OfType<JsonObject>()
            .Select(container => ContainerMetricUsage(container["usage"]?.AsObject()))
            .Aggregate(LivePulseUsage.Empty, static (left, right) => left.Add(right)) ?? LivePulseUsage.Empty;
        return (PulsePodKey(ns, name), usage);
    }

    internal static (string Key, LivePulseUsage Usage) NodeMetricUsage(JsonObject item)
    {
        var name = OptionalText(item, "/metadata/name");
        return string.IsNullOrWhiteSpace(name)
            ? (string.Empty, LivePulseUsage.Empty)
            : (name, ContainerMetricUsage(item["usage"]?.AsObject()));
    }

    internal static LivePulseUsage ContainerMetricUsage(JsonObject? usage)
    {
        return usage is null
            ? LivePulseUsage.Empty
            : new LivePulseUsage(
                ParseCpuQuantity(OptionalText(usage, "/cpu")),
                ParseByteQuantity(OptionalText(usage, "/memory")));
    }

    internal static bool IsOptionalMetricsFailure(KubernetesStatusException ex)
    {
        return ex.StatusCode
            is HttpStatusCode.NotFound
            or HttpStatusCode.Forbidden
            or HttpStatusCode.ServiceUnavailable
            or HttpStatusCode.TooManyRequests;
    }

    internal static string ShortStatus(KubernetesStatusException ex)
    {
        return $"{(int)ex.StatusCode} {ex.StatusCode}";
    }

    internal static string PulsePodKey(string? ns, string name)
    {
        return $"{ns ?? string.Empty}/{name}";
    }

    internal static IReadOnlyDictionary<string, string> LabelSelector(JsonObject service)
    {
        return service["spec"]?["selector"]?.AsObject()
                   .Where(pair => pair.Value is JsonValue value && value.TryGetValue<string>(out _))
                   .Select(pair => (pair.Key, Value: pair.Value!.GetValue<string>()))
                   .Where(pair => !string.IsNullOrWhiteSpace(pair.Value))
                   .ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.Ordinal)
               ?? new Dictionary<string, string>(StringComparer.Ordinal);
    }

    internal static string LabelSelectorExpression(IReadOnlyDictionary<string, string> selector)
    {
        return string.Join(",", selector.Select(pair => $"{pair.Key}={pair.Value}"));
    }

    internal static int ResolveServicePort(JsonObject service, JsonObject pod, int requestedPort)
    {
        var selected = service["spec"]?["ports"]?.AsArray()
            .OfType<JsonObject>()
            .FirstOrDefault(port => Int(port, "/port") == requestedPort || Int(port, "/targetPort") == requestedPort);
        if (selected is null)
        {
            return requestedPort;
        }

        var targetPort = Pointer(selected, "/targetPort");
        if (targetPort is JsonValue value && value.TryGetValue<int>(out var integer))
        {
            return integer;
        }

        if (targetPort is JsonValue nameValue && nameValue.TryGetValue<string>(out var name) && !string.IsNullOrWhiteSpace(name))
        {
            return NamedContainerPort(pod, name) ?? requestedPort;
        }

        return Int(selected, "/port", requestedPort);
    }

    internal static int? NamedContainerPort(JsonObject pod, string name)
    {
        var containers = pod["spec"]?["containers"]?.AsArray().OfType<JsonObject>() ?? Array.Empty<JsonObject>();
        foreach (var container in containers)
        {
            var port = container["ports"]?.AsArray()
                .OfType<JsonObject>()
                .FirstOrDefault(item => Text(item, "/name", string.Empty).Equals(name, StringComparison.Ordinal));
            if (port is not null)
            {
                return Int(port, "/containerPort");
            }
        }

        return null;
    }

    private FlatResourceRow RowFromObject(SessionConnection connection, string kind, JsonObject item)
    {
        var name = Text(item, "/metadata/name", "-");
        var ns = OptionalText(item, "/metadata/namespace");
        var uid = OptionalText(item, "/metadata/uid");
        var created = Date(item, "/metadata/creationTimestamp");
        var owner = Owner(kind, item);
        var eventInfo = EventInfo(kind, item);
        var lastObserved = LastObserved(kind, item, created);
        return new FlatResourceRow(
            $"{connection.Session.Id}:{kind}:{ns ?? "cluster"}:{name}:{uid ?? "-"}",
            Status(kind, item),
            kind,
            name,
            ns,
            connection.Context.ClusterName,
            PodlordText.HumanAge(created, clock),
            Ready(kind, item),
            Restarts(kind, item),
            OptionalText(item, "/spec/nodeName"),
            ImageSummary(kind, item),
            owner,
            PodlordText.HumanAge(lastObserved, clock),
            FreshnessState.Fresh,
            eventInfo.Name,
            eventInfo.Reason,
            eventInfo.Message,
            eventInfo.Object)
        {
            Pulse = ApiPulse(kind, item)
        };
    }

    internal static ResourcePulse ApiPulse(string kind, JsonObject item)
    {
        return kind switch
        {
            "Pod" => PodApiPulse(item),
            "Node" => NodeApiPulse(item),
            "PersistentVolume" => StorageApiPulse(item, "/spec/capacity/storage"),
            "PersistentVolumeClaim" => StorageApiPulse(item, "/spec/resources/requests/storage"),
            _ => ResourcePulse.Empty
        };
    }

    private static ResourcePulse PodApiPulse(JsonObject item)
    {
        var containers = item["spec"]?["containers"]?.AsArray().OfType<JsonObject>().ToList() ?? [];
        var cpuLimit = SumCpu(containers, "limits") ?? SumCpu(containers, "requests");
        var memoryLimit = SumMemory(containers, "limits") ?? SumMemory(containers, "requests");
        return ResourcePulse.Empty with
        {
            CpuLimitMillicores = cpuLimit,
            MemoryLimitBytes = memoryLimit,
            SourceBadge = "API",
            Tooltip = "API loaded pod requests and limits. Install metrics-server for LIVE current CPU and memory."
        };
    }

    private static ResourcePulse NodeApiPulse(JsonObject item)
    {
        return ResourcePulse.Empty with
        {
            CpuLimitMillicores = ParseCpuQuantity(OptionalText(item, "/status/allocatable/cpu") ?? OptionalText(item, "/status/capacity/cpu")),
            MemoryLimitBytes = ParseByteQuantity(OptionalText(item, "/status/allocatable/memory") ?? OptionalText(item, "/status/capacity/memory")),
            StorageLimitBytes = ParseByteQuantity(OptionalText(item, "/status/allocatable/ephemeral-storage") ?? OptionalText(item, "/status/capacity/ephemeral-storage")),
            SourceBadge = "API",
            Tooltip = "API loaded node allocatable capacity. Install metrics-server for LIVE current CPU and memory."
        };
    }

    private static ResourcePulse StorageApiPulse(JsonObject item, string capacityPointer)
    {
        return ResourcePulse.Empty with
        {
            StorageLimitBytes = ParseByteQuantity(OptionalText(item, capacityPointer)),
            SourceBadge = "API",
            Tooltip = "API loaded declared storage capacity. Live storage usage requires node or storage metrics integration."
        };
    }

    private static double? SumCpu(IEnumerable<JsonObject> containers, string resourceKind)
    {
        var values = containers
            .Select(container => ParseCpuQuantity(OptionalText(container, $"/resources/{resourceKind}/cpu")))
            .OfType<double>()
            .ToList();
        return values.Count == 0 ? null : values.Sum();
    }

    private static long? SumMemory(IEnumerable<JsonObject> containers, string resourceKind)
    {
        var values = containers
            .Select(container => ParseByteQuantity(OptionalText(container, $"/resources/{resourceKind}/memory")))
            .OfType<long>()
            .ToList();
        return values.Count == 0 ? null : values.Sum();
    }

    private static ResourceListFailure Failure(string kind, KubernetesStatusException ex)
    {
        if (ex.StatusCode == HttpStatusCode.TooManyRequests)
        {
            var retry = ex.RetryAfter is null ? "shortly" : $"in {Math.Max(1, (int)Math.Ceiling(ex.RetryAfter.Value.TotalSeconds))}s";
            return new ResourceListFailure(
                kind,
                FreshnessState.Stale,
                $"Kubernetes rate limited {kind}. Podlord is backing off and using cached data when available.",
                $"Retry automatically {retry}; narrow Kind/Namespace filters to reduce API pressure.");
        }

        return new ResourceListFailure(
            kind,
            ex.StatusCode == HttpStatusCode.Forbidden ? FreshnessState.Forbidden : FreshnessState.Stale,
            ex.Message,
            ex.StatusCode == HttpStatusCode.Forbidden
                ? $"Check RBAC: Podlord needs list/watch permission for {kind}."
                : "Check cluster connectivity and retry.");
    }

    internal static string Status(string kind, JsonObject item)
    {
        if (kind == "Pod")
        {
            var deletion = OptionalText(item, "/metadata/deletionTimestamp");
            if (deletion is not null)
            {
                return "Terminating";
            }

            var waiting = item["status"]?["containerStatuses"]?.AsArray()
                .OfType<JsonObject>()
                .Select(status => OptionalText(status, "/state/waiting/reason"))
                .FirstOrDefault(reason => !string.IsNullOrWhiteSpace(reason));
            return waiting ?? Text(item, "/status/phase", "Unknown");
        }

        if (kind == "Deployment")
        {
            var desired = Int(item, "/spec/replicas", 1);
            var available = Int(item, "/status/availableReplicas");
            return available >= desired ? "Available" : "Unavailable";
        }

        if (kind == "ReplicaSet")
        {
            var desired = Int(item, "/spec/replicas", 1);
            var ready = Int(item, "/status/readyReplicas");
            if (desired == 0)
            {
                return "ScaledZero";
            }

            return ready >= desired ? "Available" : ready > 0 ? "Progressing" : "Unavailable";
        }

        if (kind == "Node")
        {
            var ready = item["status"]?["conditions"]?.AsArray()
                .OfType<JsonObject>()
                .FirstOrDefault(condition => Text(condition, "/type", string.Empty) == "Ready");
            return Text(ready, "/status", "Unknown") == "True" ? "Ready" : "NotReady";
        }

        if (kind == "Event")
        {
            return Text(item, "/type", "Event");
        }

        if (kind == "Job")
        {
            if (Int(item, "/status/failed") > 0)
            {
                return "Failed";
            }

            if (Int(item, "/status/succeeded") > 0)
            {
                return "Complete";
            }

            return "Running";
        }

        if (kind == "CronJob" && Bool(item, "/spec/suspend"))
        {
            return "Suspended";
        }

        return OptionalText(item, "/status/phase")
               ?? OptionalText(item, "/status/type")
               ?? OptionalText(item, "/spec/type")
               ?? "Observed";
    }

    internal static string Ready(string kind, JsonObject item)
    {
        if (kind == "Pod")
        {
            var statuses = item["status"]?["containerStatuses"]?.AsArray().OfType<JsonObject>().ToList() ?? [];
            if (statuses.Count == 0)
            {
                return "-";
            }

            var ready = statuses.Count(status => Bool(status, "/ready"));
            return $"{ready}/{statuses.Count}";
        }

        if (kind is "Deployment" or "ReplicaSet" or "StatefulSet")
        {
            return $"{Int(item, "/status/readyReplicas")}/{Int(item, "/spec/replicas", 1)}";
        }

        return "-";
    }

    internal static int Restarts(string kind, JsonObject item)
    {
        if (kind != "Pod")
        {
            return 0;
        }

        return item["status"]?["containerStatuses"]?.AsArray()
            .OfType<JsonObject>()
            .Sum(status => Int(status, "/restartCount")) ?? 0;
    }

    internal static string ImageSummary(string kind, JsonObject item)
    {
        if (kind == "Event")
        {
            return Text(item, "/message", "-");
        }

        if (kind == "Pod")
        {
            return Images(item["spec"]?["containers"]?.AsArray());
        }

        if (kind is "Deployment" or "ReplicaSet" or "StatefulSet" or "DaemonSet" or "Job" or "CronJob")
        {
            var containers = kind == "CronJob"
                ? item["spec"]?["jobTemplate"]?["spec"]?["template"]?["spec"]?["containers"]?.AsArray()
                : item["spec"]?["template"]?["spec"]?["containers"]?.AsArray();
            return Images(containers);
        }

        if (kind == "ConfigMap")
        {
            var count = item["data"]?.AsObject().Count ?? 0;
            return $"{count} keys";
        }

        if (kind == "Secret")
        {
            return "metadata only";
        }

        return "-";
    }

    private static string Images(JsonArray? containers)
    {
        var images = containers?
            .OfType<JsonObject>()
            .Select(container => OptionalText(container, "/image"))
            .Where(image => !string.IsNullOrWhiteSpace(image))
            .Select(image => image!.Split('/').Last())
            .ToList() ?? [];
        return images.Count == 0 ? "-" : string.Join(", ", images);
    }

    internal static string? Owner(string kind, JsonObject item)
    {
        if (kind == "Event")
        {
            return EventObject(item);
        }

        var owner = item["metadata"]?["ownerReferences"]?.AsArray().OfType<JsonObject>().FirstOrDefault();
        return owner is null ? null : $"{Text(owner, "/kind", "-")}/{Text(owner, "/name", "-")}";
    }

    internal static (string Name, string Reason, string Message, string Object) EventInfo(string kind, JsonObject item)
    {
        if (kind != "Event")
        {
            return (string.Empty, string.Empty, string.Empty, string.Empty);
        }

        return (
            Text(item, "/metadata/name", "-"),
            Text(item, "/reason", "-"),
            Text(item, "/message", "-"),
            EventObject(item));
    }

    private static DateTimeOffset? LastObserved(string kind, JsonObject item, DateTimeOffset? created)
    {
        if (kind != "Event")
        {
            return Date(item, "/metadata/managedFields/0/time") ?? created;
        }

        return Date(item, "/series/lastObservedTime")
               ?? Date(item, "/lastTimestamp")
               ?? Date(item, "/eventTime")
               ?? Date(item, "/deprecatedLastTimestamp")
               ?? Date(item, "/metadata/managedFields/0/time")
               ?? created;
    }

    private static string EventObject(JsonObject item)
    {
        if (item["involvedObject"] is not JsonObject involved)
        {
            return "-";
        }

        var kind = Text(involved, "/kind", "-");
        var name = Text(involved, "/name", "-");
        return kind == "-" ? name : $"{kind}/{name}";
    }

    internal static IReadOnlyList<DetailItem> SummaryItems(JsonObject item, FlatResourceRow row)
    {
        var summary = new List<DetailItem>
        {
            new DetailItem("Kind", row.Kind),
            new DetailItem("Name", row.Name),
            new DetailItem("Namespace", row.Namespace ?? "cluster"),
            new DetailItem("Status", row.Status),
            new DetailItem("Ready", row.Ready),
            new DetailItem("Restarts", row.Restarts.ToString()),
            new DetailItem("CPU", row.CpuSummaryDisplay),
            new DetailItem("CPU %", row.CpuPercentDisplay),
            new DetailItem("Memory", row.MemorySummaryDisplay),
            new DetailItem("Memory %", row.MemoryPercentDisplay),
            new DetailItem("Network", row.NetworkDisplay),
            new DetailItem("Storage", row.StorageDisplay),
            new DetailItem("Metric source", row.MetricSourceBadge),
            new DetailItem("Node", row.Node ?? "-"),
            new DetailItem("Image", row.ImageSummary),
            new DetailItem("Owner", row.Owner ?? "-"),
            new DetailItem("UID", Text(item, "/metadata/uid", "-"))
        };

        if (row.Kind == "Event")
        {
            summary.AddRange(new[]
            {
                new DetailItem("Reason", row.EventReason.Length == 0 ? "-" : row.EventReason),
                new DetailItem("Message", row.EventMessage.Length == 0 ? "-" : row.EventMessage),
                new DetailItem("Involved object", row.EventObject.Length == 0 ? "-" : row.EventObject)
            });
        }

        if (row.Pulse.CpuMillicores is not null)
        {
            summary.Add(new DetailItem("CPU limit suggestion", row.Pulse.CpuLimitSuggestion));
        }

        if (row.Pulse.MemoryBytes is not null)
        {
            summary.Add(new DetailItem("Memory limit suggestion", row.Pulse.MemoryLimitSuggestion));
        }

        if (row.Kind is "ReplicaSet" or "Deployment" or "StatefulSet" or "DaemonSet")
        {
            summary.AddRange(new[]
            {
                new DetailItem("Replicas desired", Int(item, "/spec/replicas", row.Kind == "DaemonSet" ? 0 : 1).ToString(CultureInfo.InvariantCulture)),
                new DetailItem("Replicas current", Int(item, "/status/replicas").ToString(CultureInfo.InvariantCulture)),
                new DetailItem("Replicas ready", Int(item, "/status/readyReplicas").ToString(CultureInfo.InvariantCulture)),
                new DetailItem("Replicas available", Int(item, "/status/availableReplicas").ToString(CultureInfo.InvariantCulture)),
                new DetailItem("Replicas fully labeled", Int(item, "/status/fullyLabeledReplicas").ToString(CultureInfo.InvariantCulture)),
                new DetailItem("Observed generation", Int(item, "/status/observedGeneration").ToString(CultureInfo.InvariantCulture))
            });
        }

        return DetailItemFilter.Available(summary);
    }

    internal static IReadOnlyList<DetailItem> ConditionItems(JsonObject item)
    {
        var conditions = item["status"]?["conditions"]?.AsArray().OfType<JsonObject>().ToList() ?? [];
        return conditions
            .Select(condition => new DetailItem(
                Text(condition, "/type", "-"),
                $"{Text(condition, "/status", "-")} {Text(condition, "/reason", string.Empty)}".Trim()))
            .ToList();
    }

    internal static IReadOnlyList<ResourceValueItem> ValueItems(JsonObject item, string kind)
    {
        if (kind == "ConfigMap")
        {
            return KeyValues(item["data"]?.AsObject(), sensitive: false, base64Encoded: false, detectBase64: true)
                .Concat(KeyValues(item["binaryData"]?.AsObject(), sensitive: false, base64Encoded: true))
                .OrderBy(value => value.Key, StringComparer.Ordinal)
                .ToList();
        }

        if (kind == "Secret")
        {
            return KeyValues(item["data"]?.AsObject(), sensitive: true, base64Encoded: true)
                .Concat(KeyValues(item["stringData"]?.AsObject(), sensitive: true, base64Encoded: false))
                .OrderBy(value => value.Key, StringComparer.Ordinal)
                .ToList();
        }

        return Array.Empty<ResourceValueItem>();
    }

    private static IEnumerable<ResourceValueItem> KeyValues(
        JsonObject? values,
        bool sensitive,
        bool base64Encoded,
        bool detectBase64 = false)
    {
        if (values is null)
        {
            yield break;
        }

        foreach (var pair in values)
        {
            var value = pair.Value is JsonValue jsonValue && jsonValue.TryGetValue<string>(out var text)
                ? text
                : pair.Value?.ToJsonString() ?? string.Empty;
            yield return new ResourceValueItem(pair.Key, value, sensitive, base64Encoded || detectBase64 && LooksLikeBase64(value));
        }
    }

    internal static bool LooksLikeBase64(string value)
    {
        var text = value.Trim();
        if (text.Length < 8 || text.Length % 4 != 0 || text.Any(character => !char.IsLetterOrDigit(character) && character is not '+' and not '/' and not '='))
        {
            return false;
        }

        try
        {
            var bytes = Convert.FromBase64String(text);
            return bytes.Length > 0 && Convert.ToBase64String(bytes).TrimEnd('=').Equals(text.TrimEnd('='), StringComparison.Ordinal);
        }
        catch (FormatException)
        {
            return false;
        }
    }

    internal static JsonObject SanitizeObject(JsonObject item, string kind)
    {
        if (item["metadata"] is JsonObject metadata)
        {
            metadata.Remove("managedFields");
            if (metadata["annotations"] is JsonObject annotations)
            {
                annotations.Remove("kubectl.kubernetes.io/last-applied-configuration");
                annotations.Remove("deployment.kubernetes.io/revision");
                if (annotations.Count == 0)
                {
                    metadata.Remove("annotations");
                }
            }
        }

        if (kind == "Secret")
        {
            item.Remove("data");
            item.Remove("stringData");
            if (item["metadata"] is JsonObject secretMetadata)
            {
                secretMetadata.Remove("annotations");
            }
        }

        return item;
    }

    internal static string ToYaml(JsonObject item)
    {
        return new SerializerBuilder().Build().Serialize(PlainJsonValue(item));
    }

    internal static object? PlainJsonValue(JsonNode? node)
    {
        return node switch
        {
            null => null,
            JsonObject obj => obj.ToDictionary(pair => pair.Key, pair => PlainJsonValue(pair.Value), StringComparer.Ordinal),
            JsonArray array => array.Select(PlainJsonValue).ToList(),
            JsonValue value => PlainJsonScalar(value),
            _ => node.ToJsonString()
        };
    }

    private static object? PlainJsonScalar(JsonValue value)
    {
        if (value.TryGetValue<string>(out var text))
        {
            return text;
        }

        if (value.TryGetValue<bool>(out var flag))
        {
            return flag;
        }

        if (value.TryGetValue<int>(out var integer))
        {
            return integer;
        }

        if (value.TryGetValue<long>(out var longInteger))
        {
            return longInteger;
        }

        if (value.TryGetValue<double>(out var number))
        {
            return number;
        }

        if (value.TryGetValue<JsonElement>(out var element))
        {
            return PlainJsonElement(element);
        }

        return value.ToJsonString();
    }

    private static object? PlainJsonElement(JsonElement element)
    {
        return element.ValueKind switch
        {
            JsonValueKind.Object => element.EnumerateObject().ToDictionary(property => property.Name, property => PlainJsonElement(property.Value), StringComparer.Ordinal),
            JsonValueKind.Array => element.EnumerateArray().Select(PlainJsonElement).ToList(),
            JsonValueKind.String => element.GetString(),
            JsonValueKind.Number when element.TryGetInt64(out var integer) => integer,
            JsonValueKind.Number when element.TryGetDouble(out var number) => number,
            JsonValueKind.True => true,
            JsonValueKind.False => false,
            JsonValueKind.Null => null,
            _ => element.ToString()
        };
    }

    internal static string Escape(string value)
    {
        return Uri.EscapeDataString(value);
    }

    internal static string Sanitize(string message)
    {
        return message
            .Replace("token", "redacted", StringComparison.OrdinalIgnoreCase)
            .Replace("password", "redacted", StringComparison.OrdinalIgnoreCase);
    }

    internal static string HttpFailureMessage(HttpRequestException ex)
    {
        var message = Sanitize(ex.Message);
        var inner = ex.InnerException?.Message;
        return string.IsNullOrWhiteSpace(inner)
            ? message
            : $"{message}: {Sanitize(inner)}";
    }

    internal static string Text(JsonNode? node, string pointer, string fallback)
    {
        return OptionalText(node, pointer) ?? fallback;
    }

    internal static string? OptionalText(JsonNode? node, string pointer)
    {
        var value = Pointer(node, pointer);
        return value switch
        {
            JsonValue jsonValue when jsonValue.TryGetValue<string>(out var text) => text,
            JsonValue jsonValue when jsonValue.TryGetValue<int>(out var number) => number.ToString(),
            JsonValue jsonValue when jsonValue.TryGetValue<bool>(out var flag) => flag ? "True" : "False",
            _ => null
        };
    }

    internal static int Int(JsonNode? node, string pointer, int fallback = 0)
    {
        var value = Pointer(node, pointer);
        if (value is JsonValue jsonValue && jsonValue.TryGetValue<int>(out var number))
        {
            return number;
        }

        return fallback;
    }

    internal static bool Bool(JsonNode? node, string pointer)
    {
        var value = Pointer(node, pointer);
        return value is JsonValue jsonValue && jsonValue.TryGetValue<bool>(out var flag) && flag;
    }

    internal static double? ParseCpuQuantity(string? quantity)
    {
        if (string.IsNullOrWhiteSpace(quantity))
        {
            return null;
        }

        var trimmed = quantity.Trim();
        return trimmed switch
        {
            _ when trimmed.EndsWith('n') && TryQuantityNumber(trimmed[..^1], out var value) => value / 1_000_000d,
            _ when trimmed.EndsWith('u') && TryQuantityNumber(trimmed[..^1], out var value) => value / 1_000d,
            _ when trimmed.EndsWith('m') && TryQuantityNumber(trimmed[..^1], out var value) => value,
            _ when TryQuantityNumber(trimmed, out var value) => value * 1000d,
            _ => null
        };
    }

    internal static long? ParseByteQuantity(string? quantity)
    {
        if (string.IsNullOrWhiteSpace(quantity))
        {
            return null;
        }

        var trimmed = quantity.Trim();
        var suffixes = new (string Suffix, double Factor)[]
        {
            ("Ki", 1024d),
            ("Mi", Math.Pow(1024d, 2)),
            ("Gi", Math.Pow(1024d, 3)),
            ("Ti", Math.Pow(1024d, 4)),
            ("Pi", Math.Pow(1024d, 5)),
            ("K", 1000d),
            ("M", 1_000_000d),
            ("G", 1_000_000_000d),
            ("T", 1_000_000_000_000d),
            ("P", 1_000_000_000_000_000d)
        };

        foreach (var (suffix, factor) in suffixes)
        {
            if (trimmed.EndsWith(suffix, StringComparison.Ordinal)
                && TryQuantityNumber(trimmed[..^suffix.Length], out var value))
            {
                return (long)Math.Round(value * factor);
            }
        }

        return TryQuantityNumber(trimmed, out var bytes)
            ? (long)Math.Round(bytes)
            : null;
    }

    private static bool TryQuantityNumber(string text, out double value)
    {
        return double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out value);
    }

    private static DateTimeOffset? Date(JsonNode? node, string pointer)
    {
        var text = OptionalText(node, pointer);
        return DateTimeOffset.TryParse(text, out var parsed) ? parsed : null;
    }

    private static JsonNode? Pointer(JsonNode? node, string pointer)
    {
        var current = node;
        foreach (var rawPart in pointer.Split('/', StringSplitOptions.RemoveEmptyEntries))
        {
            var part = rawPart.Replace("~1", "/", StringComparison.Ordinal).Replace("~0", "~", StringComparison.Ordinal);
            current = current switch
            {
                JsonObject obj => obj.TryGetPropertyValue(part, out var child) ? child : null,
                JsonArray arr when int.TryParse(part, out var index) && index >= 0 && index < arr.Count => arr[index],
                _ => null
            };
            if (current is null)
            {
                return null;
            }
        }

        return current;
    }
}

internal sealed class KubernetesStatusException : Exception
{
    public KubernetesStatusException(HttpStatusCode statusCode, string message, TimeSpan? retryAfter = null)
        : base($"{(int)statusCode} {statusCode}: {message}")
    {
        StatusCode = statusCode;
        RetryAfter = retryAfter;
    }

    public HttpStatusCode StatusCode { get; }

    public TimeSpan? RetryAfter { get; }
}

internal sealed record ResourceListCacheKey(string SessionId, string Kind, string? Namespace);

internal sealed record ResourceListCacheEntry(DateTimeOffset StoredAt, IReadOnlyList<FlatResourceRow> Rows);

internal sealed record ResourceDetailCacheKey(string SessionId, string Kind, string? Namespace, string Name);

internal sealed record ResourceDetailCacheEntry(DateTimeOffset StoredAt, ResourceDetail Detail);

internal sealed record PodLogCacheKey(string SessionId, string Namespace, string PodName, string? Container, int TailLines, bool Previous);

internal sealed record PodLogCacheEntry(DateTimeOffset StoredAt, PodLogSnapshot Snapshot);

internal sealed record ResourcePulseCacheEntry(DateTimeOffset StoredAt, ResourcePulseSnapshot Snapshot);

internal sealed record ResourcePulseSnapshot(
    bool Available,
    string SourceBadge,
    string Tooltip,
    IReadOnlyDictionary<string, LivePulseUsage> PodUsage,
    IReadOnlyDictionary<string, LivePulseUsage> NodeUsage,
    bool NamespaceScoped);

internal sealed record LivePulseUsage(double? CpuMillicores, long? MemoryBytes)
{
    public static LivePulseUsage Empty { get; } = new(null, null);

    public LivePulseUsage Add(LivePulseUsage other)
    {
        return new LivePulseUsage(
            Sum(CpuMillicores, other.CpuMillicores),
            Sum(MemoryBytes, other.MemoryBytes));
    }

    private static double? Sum(double? left, double? right)
    {
        return left is null && right is null ? null : (left ?? 0) + (right ?? 0);
    }

    private static long? Sum(long? left, long? right)
    {
        return left is null && right is null ? null : (left ?? 0) + (right ?? 0);
    }
}

public enum KubernetesRequestPriority
{
    Foreground = 0,
    UserVisible = 1,
    Background = 2
}

public sealed record KubernetesRequestTelemetry(
    int RequestsLastMinute,
    double RequestsPerSecond,
    int QueuedRequests,
    DateTimeOffset? BackoffUntil);

public sealed record KubernetesRequestAuditEntry(
    string StartedAt,
    string Method,
    string Path,
    string Priority,
    string Status,
    string Duration,
    string Outcome);

public sealed record PortForwardRequest(
    string? SessionId,
    string Kind,
    string Namespace,
    string Name,
    int LocalPort,
    int RemotePort);

public sealed class PortForwardStatusEventArgs(string status) : EventArgs
{
    public string Status { get; } = status;
}

public sealed class PodlordPortForward : IDisposable, IAsyncDisposable
{
    private readonly string kubeconfigPath;
    private readonly string contextName;
    private readonly string ns;
    private readonly string podName;
    private readonly int localPort;
    private readonly int remotePort;
    private readonly CancellationTokenSource stop = new();
    private readonly List<Task> connectionTasks = [];
    private readonly object sync = new();
    private TcpListener? listener;
    private Task? acceptLoop;
    private bool disposed;

    internal PodlordPortForward(
        string kubeconfigPath,
        string contextName,
        string ns,
        string podName,
        int localPort,
        int remotePort)
    {
        this.kubeconfigPath = kubeconfigPath;
        this.contextName = contextName;
        this.ns = ns;
        this.podName = podName;
        this.localPort = localPort;
        this.remotePort = remotePort;
    }

    public event EventHandler<PortForwardStatusEventArgs>? StatusChanged;

    public string KubernetesPath => $"/api/v1/namespaces/{Uri.EscapeDataString(ns)}/pods/{Uri.EscapeDataString(podName)}/portforward?ports={remotePort}";

    public bool IsRunning => !disposed && listener is not null && !stop.IsCancellationRequested;

    public async Task StartAsync(CancellationToken cancellationToken)
    {
        listener = new TcpListener(IPAddress.Loopback, localPort);
        listener.Start();
        acceptLoop = AcceptLoop(cancellationToken);
        await Task.Yield();
        OnStatus("running");
    }

    public void Dispose()
    {
        DisposeAsync().AsTask().GetAwaiter().GetResult();
    }

    public async ValueTask DisposeAsync()
    {
        if (disposed)
        {
            return;
        }

        disposed = true;
        stop.Cancel();
        listener?.Stop();
        if (acceptLoop is not null)
        {
            await IgnoreCancellation(acceptLoop).ConfigureAwait(false);
        }

        Task[] tasks;
        lock (sync)
        {
            tasks = connectionTasks.ToArray();
        }

        await Task.WhenAll(tasks.Select(IgnoreCancellation)).ConfigureAwait(false);
        stop.Dispose();
        OnStatus("stopped");
    }

    private async Task AcceptLoop(CancellationToken startCancellation)
    {
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(startCancellation, stop.Token);
        while (!linked.IsCancellationRequested)
        {
            TcpClient local;
            try
            {
                local = await listener!.AcceptTcpClientAsync(linked.Token).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                return;
            }
            catch (ObjectDisposedException)
            {
                return;
            }
            catch (SocketException ex)
            {
                OnStatus($"error: {ex.Message}");
                return;
            }

            var task = HandleConnection(local, linked.Token);
            lock (sync)
            {
                connectionTasks.Add(task);
            }

            _ = task.ContinueWith(completed =>
            {
                lock (sync)
                {
                    connectionTasks.Remove(completed);
                }
            }, CancellationToken.None, TaskContinuationOptions.ExecuteSynchronously, TaskScheduler.Default);
        }
    }

    private async Task HandleConnection(TcpClient local, CancellationToken cancellationToken)
    {
        using var localClient = local;
        await using var localStream = local.GetStream();
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, stop.Token);
        using var client = CreatePortForwardClient();
        using var websocket = await client.WebSocketNamespacedPodPortForwardAsync(
            podName,
            ns,
            [remotePort],
            K8sWebSocketProtocol.V4BinaryWebsocketProtocol,
            cancellationToken: linked.Token).ConfigureAwait(false);
        using var demuxer = new K8sStreamDemuxer(websocket, K8sStreamType.PortForward, ownsSocket: true);
        demuxer.Start();
        using var remoteStream = demuxer.GetStream((byte?)0, (byte?)0);
        var localToRemote = CopyStream(localStream, remoteStream, linked.Token);
        var remoteToLocal = CopyStream(remoteStream, localStream, linked.Token);
        var completed = await Task.WhenAny(localToRemote, remoteToLocal).ConfigureAwait(false);
        linked.Cancel();
        await IgnoreCancellation(completed).ConfigureAwait(false);
    }

    private K8sClient CreatePortForwardClient()
    {
        var configuration = K8sConfiguration.BuildConfigFromConfigFile(
            kubeconfigPath,
            contextName,
            masterUrl: null,
            useRelativePaths: true);
        return new K8sClient(configuration);
    }

    private static async Task CopyStream(
        Stream source,
        Stream destination,
        CancellationToken cancellationToken)
    {
        var buffer = new byte[32 * 1024];
        while (!cancellationToken.IsCancellationRequested)
        {
            var read = await source.ReadAsync(buffer, cancellationToken).ConfigureAwait(false);
            if (read == 0)
            {
                return;
            }

            await destination.WriteAsync(buffer.AsMemory(0, read), cancellationToken).ConfigureAwait(false);
            await destination.FlushAsync(cancellationToken).ConfigureAwait(false);
        }
    }

    private void OnStatus(string status)
    {
        StatusChanged?.Invoke(this, new PortForwardStatusEventArgs(status));
    }

    private static async Task IgnoreCancellation(Task task)
    {
        try
        {
            await task.ConfigureAwait(false);
        }
        catch (Exception ex) when (ex is OperationCanceledException or ObjectDisposedException or IOException or WebSocketException)
        {
        }
    }
}

internal sealed record ResolvedPortForwardTarget(string Namespace, string PodName, int RemotePort);

internal sealed class RequestSlotLease : IDisposable
{
    private readonly SemaphoreSlim gate;
    private int disposed;

    public RequestSlotLease(SemaphoreSlim gate)
    {
        this.gate = gate;
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref disposed, 1) == 0)
        {
            gate.Release();
        }
    }
}

internal readonly record struct QueuedRequestOrder(int Priority, long Sequence) : IComparable<QueuedRequestOrder>
{
    public int CompareTo(QueuedRequestOrder other)
    {
        var priority = Priority.CompareTo(other.Priority);
        return priority != 0 ? priority : Sequence.CompareTo(other.Sequence);
    }
}

internal interface IQueuedRequest
{
    Task Execute(KubernetesResourceService service);
}

internal sealed class QueuedRequest<T>(
    Func<CancellationToken, Task<T>> work,
    TaskCompletionSource<T> completion,
    CancellationToken cancellationToken) : IQueuedRequest
{
    public async Task Execute(KubernetesResourceService service)
    {
        if (completion.Task.IsCompleted)
        {
            return;
        }

        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            using var lease = await service.WaitForRequestSlot(cancellationToken).ConfigureAwait(false);
            var result = await work(cancellationToken).ConfigureAwait(false);
            completion.TrySetResult(result);
        }
        catch (OperationCanceledException)
        {
            completion.TrySetCanceled(cancellationToken);
        }
        catch (Exception ex)
        {
            completion.TrySetException(ex);
        }
    }
}
