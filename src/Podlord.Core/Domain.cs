using System.Collections.ObjectModel;
using System.Globalization;
using System.Text.Json.Serialization;

namespace Podlord.Core;

public enum SafetyLevel
{
    Local,
    Dev,
    Test,
    Staging,
    Production,
    Unknown
}

public enum FreshnessState
{
    Fresh,
    Updating,
    Reconnecting,
    Relisting,
    Stale,
    Forbidden,
    Gone,
    Unknown
}

public enum NamespaceScopeKind
{
    All,
    One,
    Many
}

public enum CommandRiskLevel
{
    None,
    ReadOnly,
    Mutating,
    Destructive,
    Critical
}

public sealed record NamespaceScope(NamespaceScopeKind Kind, IReadOnlyList<string> Names)
{
    public static NamespaceScope All { get; } = new(NamespaceScopeKind.All, Array.Empty<string>());

    public static NamespaceScope One(string name)
    {
        var normalized = NormalizeNames([name]);
        return normalized.Count == 0 ? All : new NamespaceScope(NamespaceScopeKind.One, normalized);
    }

    public static NamespaceScope Many(IEnumerable<string> names)
    {
        var normalized = NormalizeNames(names);
        return normalized.Count switch
        {
            0 => All,
            1 => One(normalized[0]),
            _ => new NamespaceScope(NamespaceScopeKind.Many, normalized)
        };
    }

    public string Label => Kind switch
    {
        NamespaceScopeKind.All => "all sectors",
        NamespaceScopeKind.One => Names[0],
        NamespaceScopeKind.Many => string.Join(",", Names),
        _ => "all sectors"
    };

    private static ReadOnlyCollection<string> NormalizeNames(IEnumerable<string> names)
    {
        return names
            .Select(name => name.Trim())
            .Where(name => name.Length > 0)
            .Distinct(StringComparer.Ordinal)
            .ToList()
            .AsReadOnly();
    }
}

public sealed record Settings(
    string Theme,
    byte PixelEffectIntensity,
    byte AnimationIntensity,
    string DefaultLandingView,
    NamespaceScope DefaultNamespaceScope,
    bool WorkspaceRestore,
    string SecretRevealPolicy,
    bool TelemetryEnabled,
    bool ScreensaverEnabled = true,
    bool RadarWaterEnabled = true,
    byte RadarWaterSpeed = 45,
    bool RadarAutoFollowAlerts = true,
    int InactiveSyncMinutes = 0,
    int RequestHardLimitPerMinute = 0,
    string ThemeVariant = "dark",
    string Language = "system",
    IReadOnlyList<TableColumnLayout>? TableColumnLayouts = null)
{
    public static Settings Default { get; } = new(
        Theme: "Sirocco Command",
        PixelEffectIntensity: 18,
        AnimationIntensity: 60,
        DefaultLandingView: "resources",
        DefaultNamespaceScope: NamespaceScope.All,
        WorkspaceRestore: true,
        SecretRevealPolicy: "explicit-reveal",
        TelemetryEnabled: false,
        ScreensaverEnabled: true,
        RadarWaterEnabled: true,
        RadarWaterSpeed: 45,
        RadarAutoFollowAlerts: true,
        InactiveSyncMinutes: 0,
        RequestHardLimitPerMinute: 0,
        ThemeVariant: "dark",
        Language: "system",
        TableColumnLayouts: Array.Empty<TableColumnLayout>());
}

public sealed record TableColumnLayout(
    string TableId,
    string ColumnId,
    int DisplayIndex,
    bool IsVisible);

public sealed record ImportedContext(
    string ContextId,
    string SourcePath,
    string? OwnedKubeconfigPath,
    string Name,
    string DisplayName,
    string ClusterName,
    string UserName,
    string? Namespace,
    string? Server,
    string AuthType,
    SafetyLevel SafetyLevel,
    IReadOnlyList<string> BrokenReferences,
    string ImportedAt,
    string SourceName = "",
    string SourceContentHash = "",
    string FilterName = "default");

public sealed record PodlordSession(
    string Id,
    string DisplayName,
    string ContextId,
    string ClusterName,
    NamespaceScope NamespaceScope,
    SafetyLevel SafetyLevel,
    string? Color,
    string? Icon,
    bool Active,
    string CreatedAt);

public sealed record AppStore(
    Settings Settings,
    IReadOnlyList<ImportedContext> ImportedContexts,
    IReadOnlyList<PodlordSession> Sessions,
    string? ActiveSessionId)
{
    public static AppStore Empty { get; } = new(
        Settings.Default,
        Array.Empty<ImportedContext>(),
        Array.Empty<PodlordSession>(),
        null);
}

public sealed record KubeconfigImportSummary(
    string SourcePath,
    IReadOnlyList<ImportedContext> Contexts,
    IReadOnlyList<string> Warnings,
    int CreatedSessionCount);

public sealed record CommandRisk(
    CommandRiskLevel Level,
    IReadOnlyList<string> MatchedTerms,
    bool RequiresConfirmation,
    string Explanation);

public sealed record ResourceQuery(
    string? SessionId = null,
    string? Search = null,
    string? Id = null,
    string? Issue = null,
    string? Kind = null,
    string? Name = null,
    string? Namespace = null,
    string? Cluster = null,
    string? Status = null,
    string? Age = null,
    string? Node = null,
    string? Image = null,
    string? Ready = null,
    string? Restarts = null,
    string? Owner = null,
    bool ProblemsOnly = false,
    bool ActivityOnly = false,
    int Limit = 256,
    bool ForceRefresh = false,
    string? Cpu = null,
    string? Memory = null,
    string? Storage = null);

public sealed record FlatResourceRow(
    string Id,
    string Status,
    string Kind,
    string Name,
    string? Namespace,
    string Cluster,
    string Age,
    string Ready,
    int Restarts,
    string? Node,
    string ImageSummary,
    string? Owner,
    string LastChange,
    FreshnessState Freshness,
    string EventName = "",
    string EventReason = "",
    string EventMessage = "",
    string EventObject = "",
    bool IsAnnouncing = false)
{
    public ResourcePulse Pulse { get; init; } = ResourcePulse.Empty;

    public string CpuDisplay => Pulse.CpuDisplay;

    public string CpuSummaryDisplay => Pulse.CpuSummaryDisplay;

    public string MemoryDisplay => Pulse.MemoryDisplay;

    public string MemorySummaryDisplay => Pulse.MemorySummaryDisplay;

    public string CpuPercentDisplay => Pulse.CpuPercentDisplay;

    public string CpuCompactDisplay => Pulse.CpuCompactDisplay;

    public string CpuMetricDetail => Pulse.CpuMetricDetail;

    public bool HasCpuMetricBar => Pulse.CpuLimitMillicores is > 0;

    public bool HasCpuMetricInfo => Pulse.CpuMillicores is not null || Pulse.CpuLimitMillicores is > 0;

    public bool HasCpuMetricTextOnly => HasCpuMetricInfo && !HasCpuMetricBar;

    public string MemoryPercentDisplay => Pulse.MemoryPercentDisplay;

    public string MemoryCompactDisplay => Pulse.MemoryCompactDisplay;

    public string MemoryMetricDetail => Pulse.MemoryMetricDetail;

    public bool HasMemoryMetricBar => Pulse.MemoryLimitBytes is > 0;

    public bool HasStorageMetricBar => Pulse.StorageLimitBytes is > 0;

    public string StoragePercentDisplay => Pulse.StoragePercentDisplay;

    public string StorageCompactDisplay => Pulse.StorageCompactDisplay;

    public string StorageMetricDetail => Pulse.StorageMetricDetail;

    public bool HasMemoryMetricInfo => Pulse.MemoryBytes is not null || Pulse.MemoryLimitBytes is > 0;

    public bool HasMemoryMetricTextOnly => HasMemoryMetricInfo && !HasMemoryMetricBar;

    public bool HasStorageMetricInfo => Pulse.StorageUsedBytes is not null || Pulse.StorageLimitBytes is > 0;

    public bool HasStorageMetricTextOnly => HasStorageMetricInfo && !HasStorageMetricBar;

    public bool HasNetworkMetricInfo => Pulse.NetworkInBytesPerSecond is not null || Pulse.NetworkOutBytesPerSecond is not null;

    public bool HasReadyInfo => Ready != "-";

    public bool HasRestartInfo => Kind == "Pod" || Restarts > 0;

    public bool HasNodeInfo => !string.IsNullOrWhiteSpace(Node) && Node != "-";

    public bool HasImageInfo => !string.IsNullOrWhiteSpace(ImageSummary) && ImageSummary != "-";

    public bool HasOwnerInfo => !string.IsNullOrWhiteSpace(Owner) && Owner != "-";

    public string NetworkDisplay => Pulse.NetworkDisplay;

    public string StorageDisplay => Pulse.StorageDisplay;

    public string MetricSourceBadge => Pulse.SourceBadge;

    public string MetricTooltip => Pulse.Tooltip;
}

public sealed record ResourcePulse(
    double? CpuMillicores,
    double? CpuLimitMillicores,
    long? MemoryBytes,
    long? MemoryLimitBytes,
    long? NetworkInBytesPerSecond,
    long? NetworkOutBytesPerSecond,
    long? StorageUsedBytes,
    long? StorageLimitBytes,
    string SourceBadge,
    string Tooltip)
{
    public static ResourcePulse Empty { get; } = new(
        null,
        null,
        null,
        null,
        null,
        null,
        null,
        null,
        "API",
        "No live metrics source is available for this resource yet.");

    public string CpuDisplay => CpuMillicores is null ? "-" : FormatCpu(CpuMillicores.Value);

    public string CpuLimitDisplay => CpuLimitMillicores is null ? "-" : FormatCpu(CpuLimitMillicores.Value);

    public string CpuSummaryDisplay => CpuLimitMillicores is null
        ? CpuDisplay
        : CpuMillicores is null ? $"-/{CpuLimitDisplay}" : $"{CpuDisplay} / {CpuLimitDisplay}";

    public string MemoryDisplay => MemoryBytes is null ? "-" : FormatBytes(MemoryBytes.Value);

    public string MemoryLimitDisplay => MemoryLimitBytes is null ? "-" : FormatBytes(MemoryLimitBytes.Value);

    public string MemorySummaryDisplay => MemoryLimitBytes is null
        ? MemoryDisplay
        : MemoryBytes is null ? $"-/{MemoryLimitDisplay}" : $"{MemoryDisplay} / {MemoryLimitDisplay}";

    public string CpuPercentDisplay => Percent(CpuMillicores, CpuLimitMillicores);

    public string MemoryPercentDisplay => Percent(MemoryBytes, MemoryLimitBytes);

    public string StoragePercentDisplay => Percent(StorageUsedBytes, StorageLimitBytes);

    public string CpuCompactDisplay => CpuMillicores is null || CpuLimitMillicores is not > 0
        ? "-"
        : CpuPercentDisplay;

    public string MemoryCompactDisplay => MemoryBytes is null || MemoryLimitBytes is not > 0
        ? "-"
        : MemoryPercentDisplay;

    public string StorageCompactDisplay => StorageUsedBytes is null || StorageLimitBytes is not > 0
        ? "-"
        : StoragePercentDisplay;

    public string CpuMetricDetail => MetricDetail("CPU", CpuSummaryDisplay, CpuPercentDisplay, CpuLimitSuggestion);

    public string MemoryMetricDetail => MetricDetail("Memory", MemorySummaryDisplay, MemoryPercentDisplay, MemoryLimitSuggestion);

    public string StorageMetricDetail => MetricDetail("Storage", StorageDisplay, StoragePercentDisplay, "-");

    public string NetworkDisplay => NetworkInBytesPerSecond is null && NetworkOutBytesPerSecond is null
        ? "-"
        : $"↓{FormatBytes(NetworkInBytesPerSecond ?? 0)}/s ↑{FormatBytes(NetworkOutBytesPerSecond ?? 0)}/s";

    public string StorageDisplay => StorageUsedBytes is null
        ? StorageLimitBytes is null ? "-" : $"-/{FormatBytes(StorageLimitBytes.Value)}"
        : StorageLimitBytes is null ? FormatBytes(StorageUsedBytes.Value) : $"{FormatBytes(StorageUsedBytes.Value)} / {FormatBytes(StorageLimitBytes.Value)}";

    public double CpuPercent => RatioPercent(CpuMillicores, CpuLimitMillicores);

    public double MemoryPercent => RatioPercent(MemoryBytes, MemoryLimitBytes);

    public double StoragePercent => RatioPercent(StorageUsedBytes, StorageLimitBytes);

    public bool HasLiveMetrics => SourceBadge.Contains("LIVE", StringComparison.OrdinalIgnoreCase);

    public string CpuLimitSuggestion => CpuMillicores is null
        ? "-"
        : $"{FormatCpu(RoundCpu(CpuMillicores.Value * 1.25, 10, 10))} request / {FormatCpu(RoundCpu(CpuMillicores.Value * 2.0, 50, 50))} limit";

    public string MemoryLimitSuggestion => MemoryBytes is null
        ? "-"
        : $"{FormatBytes(RoundBytes(MemoryBytes.Value * 1.25, 16, 32))} request / {FormatBytes(RoundBytes(MemoryBytes.Value * 1.8, 32, 64))} limit";

    public ResourcePulse WithLiveUsage(double? cpuMillicores, long? memoryBytes, string sourceBadge, string tooltip)
    {
        return this with
        {
            CpuMillicores = cpuMillicores ?? CpuMillicores,
            MemoryBytes = memoryBytes ?? MemoryBytes,
            SourceBadge = sourceBadge,
            Tooltip = tooltip
        };
    }

    private static string Percent(double? current, double? total)
    {
        if (current is null || total is null || total <= 0)
        {
            return "-";
        }

        return $"{Math.Clamp(current.Value / total.Value * 100, 0, 999):0}%";
    }

    private static double RatioPercent(double? current, double? total)
    {
        if (current is null || total is null || total <= 0)
        {
            return 0;
        }

        return Math.Clamp(current.Value / total.Value * 100, 0, 100);
    }

    private static string FormatCpu(double millicores)
    {
        if (millicores <= 0)
        {
            return "0m";
        }

        if (millicores < 1)
        {
            return "<1m";
        }

        return millicores < 1000
            ? $"{millicores.ToString("0", CultureInfo.InvariantCulture)}m"
            : $"{(millicores / 1000d).ToString("0.#", CultureInfo.InvariantCulture)}c";
    }

    private static string FormatBytes(long bytes)
    {
        string[] units = ["B", "Ki", "Mi", "Gi", "Ti", "Pi"];
        var value = (double)Math.Max(0, bytes);
        var unit = 0;
        while (value >= 1024 && unit < units.Length - 1)
        {
            value /= 1024;
            unit++;
        }

        return unit == 0
            ? $"{value.ToString("0", CultureInfo.InvariantCulture)}{units[unit]}"
            : $"{value.ToString("0.#", CultureInfo.InvariantCulture)}{units[unit]}";
    }

    private static double RoundCpu(double value, double stepMillicores, double minimumMillicores)
    {
        return Math.Max(minimumMillicores, Math.Ceiling(value / stepMillicores) * stepMillicores);
    }

    private static long RoundBytes(double value, int stepMi, int minimumMi)
    {
        var stepBytes = stepMi * 1024d * 1024d;
        var minimumBytes = minimumMi * 1024d * 1024d;
        return (long)Math.Max(minimumBytes, Math.Ceiling(value / stepBytes) * stepBytes);
    }

    private static string MetricDetail(string label, string value, string percent, string suggestion)
    {
        var lines = new List<string> { $"{label}: {value}", $"Usage: {percent}" };
        if (!string.IsNullOrWhiteSpace(suggestion) && suggestion != "-")
        {
            lines.Add($"Suggestion: {suggestion}");
        }

        return string.Join('\n', lines);
    }
}

public sealed record ResourceListFailure(
    string Kind,
    FreshnessState Freshness,
    string Message,
    string NextAction);

public sealed record ResourceExplorerSnapshot(
    string SessionId,
    string ContextId,
    string Cluster,
    string Context,
    NamespaceScope NamespaceScope,
    string ListedAt,
    FreshnessState Freshness,
    IReadOnlyList<FlatResourceRow> Rows,
    IReadOnlyList<string> Namespaces,
    IReadOnlyList<string> Kinds,
    IReadOnlyList<string> Statuses,
    IReadOnlyList<string> Nodes,
    IReadOnlyList<string> Images,
    IReadOnlyList<string> Owners,
    IReadOnlyList<string> ReadyValues,
    IReadOnlyList<ResourceListFailure> Failures);

public sealed record ResourceIdentity(
    string? SessionId,
    string Kind,
    string? Namespace,
    string Name);

public sealed record DetailItem(string Label, string Value);

public sealed record EventSummary(
    string EventType,
    string Reason,
    string Message,
    int Count,
    string LastSeen);

public sealed record ResourceValueItem(
    string Key,
    string Value,
    bool Sensitive,
    bool Base64Encoded);

public sealed record ResourceDetail(
    ResourceIdentity Identity,
    string Status,
    FreshnessState Freshness,
    string Yaml,
    IReadOnlyList<DetailItem> Summary,
    IReadOnlyList<DetailItem> Conditions,
    IReadOnlyList<EventSummary> Events,
    IReadOnlyList<ResourceValueItem> Values);

public sealed record PodLogRequest(
    string? SessionId,
    string Namespace,
    string PodName,
    string? Container,
    int TailLines,
    bool Previous);

public sealed record PodLogSnapshot(
    ResourceIdentity Identity,
    string? Container,
    bool Previous,
    int TailLines,
    string FetchedAt,
    string Text);

public sealed record SessionConnection(
    PodlordSession Session,
    ImportedContext Context,
    string KubeconfigPath);

public sealed record BootstrapSection(string Id, string Label);

public sealed record AppBootstrap(
    AppStore Store,
    IReadOnlyList<BootstrapSection> ShellSections)
{
    public static AppBootstrap FromStore(AppStore store)
    {
        return new AppBootstrap(store, [
            new BootstrapSection("resources", "Resources"),
            new BootstrapSection("sources", "Sources"),
            new BootstrapSection("settings", "Settings")
        ]);
    }
}

[JsonSerializable(typeof(AppStore))]
[JsonSerializable(typeof(Settings))]
[JsonSerializable(typeof(NamespaceScope))]
internal sealed partial class PodlordJsonContext : JsonSerializerContext;
