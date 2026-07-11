namespace Podlord.Core;

public interface IKubernetesApplicationPort :
    IKubernetesResourceSnapshotPort,
    IKubernetesResourceDetailPort,
    IKubernetesPodLogPort,
    IKubernetesPortForwardPort,
    IKubernetesDiagnosticsPort
{
    IKubernetesApplicationPort CreateIndependentPipeline();
}

public interface IKubernetesResourceSnapshotPort
{
    Task<ResourceExplorerSnapshot> ListClusterResourcesAsync(ResourceQuery query, CancellationToken cancellationToken = default);

    ResourceExplorerSnapshot GetCachedResourceSnapshot(ResourceQuery query);

    ResourceExplorerSnapshot GetCachedResourceSnapshot(ResourceQuery query, bool applyFilters);

    Task<ResourceExplorerSnapshot> WarmResourceCacheAsync(
        ResourceQuery query,
        KubernetesRequestPriority priority = KubernetesRequestPriority.Background,
        CancellationToken cancellationToken = default);

    int EstimateListRequestCount(ResourceQuery query);

    bool HasFreshResourceCache(ResourceQuery query);

    bool HasRecentWarmResourceCompletion(ResourceQuery query);
}

public interface IKubernetesResourceDetailPort
{
    Task<ResourceDetail> GetResourceDetailAsync(ResourceIdentity identity, CancellationToken cancellationToken = default);

    ResourceDetail? GetCachedResourceDetail(ResourceIdentity identity);

    Task<ResourceDetail> GetResourceDetailAsync(
        ResourceIdentity identity,
        bool forceRefresh,
        KubernetesRequestPriority priority,
        CancellationToken cancellationToken = default);

    Task<ResourceDetail> ApplyResourceYamlAsync(
        ResourceIdentity identity,
        string yaml,
        CancellationToken cancellationToken = default);

    Task DeleteResourceAsync(ResourceIdentity identity, CancellationToken cancellationToken = default);
}

public interface IKubernetesPodLogPort
{
    Task<PodLogSnapshot> GetPodLogsAsync(PodLogRequest request, CancellationToken cancellationToken = default);

    PodLogSnapshot? GetCachedPodLogs(PodLogRequest request);

    Task<PodLogSnapshot> GetPodLogsAsync(
        PodLogRequest request,
        bool forceRefresh,
        KubernetesRequestPriority priority,
        CancellationToken cancellationToken = default);
}

public interface IKubernetesPortForwardPort
{
    Task<IPodlordPortForward> StartPortForwardAsync(
        PortForwardRequest request,
        CancellationToken cancellationToken = default);
}

public interface IKubernetesDiagnosticsPort
{
    KubernetesRequestTelemetry RequestTelemetry(string? sessionId = null);

    int CompletedRequestsSinceStart(DateTimeOffset start);

    int CompletedRequestsSinceStart(DateTimeOffset start, string? sessionId);

    IReadOnlyList<KubernetesRequestAuditEntry> RequestAuditLog();

    KubernetesCacheTelemetry CacheTelemetry();

    KubernetesDiagnosticRecordResult RecordDiagnostic(
        string scope,
        string outcome,
        KubernetesRequestPriority priority = KubernetesRequestPriority.UserVisible);
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

public sealed record KubernetesCacheTelemetry(
    int ListEntries,
    int DetailEntries,
    int LogEntries,
    int PulseEntries,
    long EstimatedBytes)
{
    public int TotalEntries => ListEntries + DetailEntries + LogEntries + PulseEntries;
}

public sealed record KubernetesRequestAuditEntry(
    string StartedAt,
    string Method,
    string Path,
    string Priority,
    string Status,
    string Duration,
    string Outcome);

public sealed record KubernetesDiagnosticRecordResult(
    string Scope,
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

public interface IPodlordPortForward : IDisposable, IAsyncDisposable
{
    event EventHandler<PortForwardStatusEventArgs>? StatusChanged;

    bool IsRunning { get; }
}
