using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using Avalonia.Media;
using Podlord.Core;
using Podlord.Kubernetes;

namespace Podlord.App;

public sealed class ImportedContextRowViewModel : INotifyPropertyChanged
{
    private string displayName;

    public ImportedContextRowViewModel(
        string contextId,
        string sourcePath,
        string displayName,
        Action<ImportedContextRowViewModel> removeAction,
        Action<ImportedContextRowViewModel, string> renameAction,
        Action<ImportedContextRowViewModel>? activateAction = null,
        bool isActive = false,
        string sourceName = "",
        string hash = "")
    {
        ContextId = contextId;
        SourcePath = sourcePath;
        this.displayName = displayName;
        RemoveCommand = new RelayCommand(() => removeAction(this));
        RenameCommand = new RelayCommand(() => renameAction(this, this.displayName));
        ActivateCommand = new RelayCommand(() => activateAction?.Invoke(this));
        IsActive = isActive;
        SourceName = sourceName;
        Hash = hash;
    }

    public string ContextId { get; }

    public string SourcePath { get; }

    public bool IsActive { get; }

    public string ActiveMark => IsActive ? "ACTIVE" : "USE";

    public string SourceName { get; }

    public string Hash { get; }

    public event PropertyChangedEventHandler? PropertyChanged;

    public string DisplayName
    {
        get => displayName;
        set
        {
            if (displayName == value)
            {
                return;
            }

            displayName = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(DisplayName)));
        }
    }

    public System.Windows.Input.ICommand RemoveCommand { get; }

    public System.Windows.Input.ICommand RenameCommand { get; }

    public System.Windows.Input.ICommand ActivateCommand { get; }
}

public sealed class SourceStatusRow : INotifyPropertyChanged
{
    private readonly Func<SourceStatusRow, string, string>? renameAction;
    private readonly Func<SourceStatusRow, string, string>? filterAction;
    private string context;
    private string filterName;

    public SourceStatusRow(
        string name,
        string hash,
        string importedAt,
        string source,
        string context,
        string cluster,
        string user,
        string authType,
        string status,
        string detail,
        string contextId = "",
        string ownedKubeconfigPath = "",
        string server = "",
        string filterName = "default",
        Func<SourceStatusRow, string, string>? renameAction = null,
        Func<SourceStatusRow, string, string>? filterAction = null)
    {
        Name = name;
        Hash = hash;
        ImportedAt = importedAt;
        Source = source;
        this.context = context;
        Cluster = cluster;
        User = user;
        AuthType = authType;
        Status = status;
        Detail = detail;
        ContextId = contextId;
        OwnedKubeconfigPath = ownedKubeconfigPath;
        Server = server;
        this.filterName = string.IsNullOrWhiteSpace(filterName) ? FilterPresetStore.DefaultFilterName : filterName;
        this.renameAction = renameAction;
        this.filterAction = filterAction;
    }

    public string Name { get; }

    public string Hash { get; }

    public string ImportedAt { get; }

    public string Source { get; }

    public string Context
    {
        get => context;
        set
        {
            var requested = value ?? string.Empty;
            var normalized = renameAction?.Invoke(this, requested) ?? requested;
            if (context == normalized)
            {
                return;
            }

            context = normalized;
            OnPropertyChanged();
        }
    }

    public string Cluster { get; }

    public string User { get; }

    public string AuthType { get; }

    public string Status { get; }

    public string Detail { get; }

    public string ContextId { get; }

    public string OwnedKubeconfigPath { get; }

    public string Server { get; }

    public string FilterName
    {
        get => filterName;
        set
        {
            var requested = string.IsNullOrWhiteSpace(value) ? FilterPresetStore.DefaultFilterName : value;
            var normalized = filterAction?.Invoke(this, requested) ?? requested;
            if (filterName == normalized)
            {
                return;
            }

            filterName = normalized;
            OnPropertyChanged();
        }
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}

public sealed record RequestAuditRow(
    string StartedAt,
    string Method,
    string Path,
    string Priority,
    string Status,
    string Duration,
    string Outcome);

public sealed record DiagnosticMetricRow(
    string Label,
    string Value,
    string Description);

public sealed record RelationshipRow(
    string FromKind,
    string FromName,
    string Link,
    string ToKind,
    string ToName,
    string Namespace,
    string Status);

public sealed record EventTimelineRow(
    string Type,
    string Name,
    string Reason,
    string Object,
    string Namespace,
    string Age,
    string Message,
    string ResourceId = "")
{
    public string Status => Type;

    public string AgeDisplay => Podlord.Core.FlatResourceRow.FormatAgeWithSpaces(Age);
}

public sealed record FocusMetricRow(
    string Label,
    string Value,
    double Percent,
    bool HasBar,
    string Suggestion = "",
    double SuggestionPercent = 0,
    bool HealthyWhenFull = false)
{
    public bool HasSuggestion => !string.IsNullOrWhiteSpace(Suggestion) && SuggestionPercent > 0;

    public double SuggestionLeft => Math.Clamp(SuggestionPercent, 0, 100) * 1.54d;

    public bool IsLimitedMetric => Label is "CPU" or "Memory";

    public bool HasMarker => HasSuggestion || (HasBar && !HealthyWhenFull && IsLimitedMetric);

    public double MarkerLeft => HasSuggestion ? SuggestionLeft : 154d;

    public bool IsResourceRef =>
        Label is "Node" or "Owner" or "Namespace" or "Kind" or "Name"
        && !string.IsNullOrWhiteSpace(Value)
        && Value != "-"
        && Value != "cluster";

    public string ReferenceValue => Label switch
    {
        "Node" => $"Node/{Value}",
        "Namespace" => $"Namespace/{Value}",
        "Kind" => Value,
        "Name" => Value,
        _ => Value
    };

    public string ColorSeed => string.IsNullOrWhiteSpace(Value) ? Label : Value;

    public string MetricTooltip => HasSuggestion
        ? $"{Label}: {Value}{Environment.NewLine}Suggestion: {Suggestion}"
        : $"{Label}: {Value}";

    /// <summary>State key for the bar brush. Readiness/availability rows are healthy when full; utilization rows are critical when full.</summary>
    public string BarState => BarStateFor(Percent, HealthyWhenFull);

    public IBrush BarBrush => AppThemeCatalog.StatusBrush(BarState);

    /// <summary>
    /// Maps a metric percentage to a status key. When <paramref name="healthyWhenFull"/> is true the scale is inverted:
    /// a full bar (e.g. all replicas ready) is healthy and an empty bar is critical. Otherwise high utilization is critical.
    /// </summary>
    internal static string BarStateFor(double percent, bool healthyWhenFull)
    {
        var clamped = Math.Clamp(percent, 0, 100);
        return healthyWhenFull
            ? clamped switch
            {
                >= 100 => "HEALTHY",
                > 0 => "WARNING",
                _ => "CRITICAL"
            }
            : clamped switch
            {
                >= 90 => "CRITICAL",
                >= 70 => "WARNING",
                _ => "HEALTHY"
            };
    }
}

public sealed record PulseMetricCard(
    string Label,
    string Value,
    double Percent,
    string Badge,
    string Tooltip,
    bool HasBar = true);

public sealed class ResourceValueRow : INotifyPropertyChanged
{
    private bool isRevealed;

    public ResourceValueRow(string key, string rawValue, bool sensitive, bool base64Encoded)
    {
        Key = key;
        RawValue = rawValue;
        IsSensitive = sensitive;
        IsBase64Encoded = base64Encoded;
        DecodedValue = base64Encoded ? DecodeBase64(rawValue) : rawValue;
    }

    public string Key { get; }

    public string RawValue { get; }

    public string DecodedValue { get; }

    public bool IsSensitive { get; }

    public bool IsBase64Encoded { get; }

    public string Encoding => IsBase64Encoded ? "base64" : "plain";

    public string DisplayValue
    {
        get
        {
            if (IsSensitive && !IsRevealed)
            {
                return "••••••••••••";
            }

            return Preview(PreferredCopyValue);
        }
    }

    public string PreferredCopyValue => IsBase64Encoded ? DecodedValue : RawValue;

    public bool HasPlainCopy => !IsBase64Encoded;

    public bool HasBase64Copy => IsBase64Encoded;

    public string RevealLabel => IsRevealed ? "HIDE" : "REVEAL";

    public bool IsRevealed
    {
        get => isRevealed;
        private set
        {
            if (isRevealed == value)
            {
                return;
            }

            isRevealed = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(DisplayValue));
            OnPropertyChanged(nameof(RevealLabel));
        }
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public void RevealTemporarily()
    {
        IsRevealed = true;
    }

    private static string Preview(string value)
    {
        var compact = NormalizeVisibleWhitespace(value);
        return compact.Length <= 180 ? compact : compact[..177] + "...";
    }

    internal static string NormalizeVisibleWhitespace(string value)
    {
        if (value.Length == 0)
        {
            return string.Empty;
        }

        var normalized = value
            .Replace("\r\n", "\n", StringComparison.Ordinal)
            .Replace('\r', '\n')
            .Replace("\t", "    ", StringComparison.Ordinal);
        var builder = new System.Text.StringBuilder(normalized.Length);
        foreach (var character in normalized)
        {
            if (!char.IsControl(character) || character == '\n')
            {
                builder.Append(character);
                continue;
            }

            builder.Append(character switch
            {
                '\0' => "\\0",
                '\b' => "\\b",
                '\f' => "\\f",
                _ => $"\\u{(int)character:x4}"
            });
        }

        return builder.ToString();
    }

    public void Hide()
    {
        IsRevealed = false;
    }

    private static string DecodeBase64(string value)
    {
        try
        {
            return System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(value));
        }
        catch (FormatException)
        {
            return value;
        }
        catch (ArgumentException)
        {
            return value;
        }
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}

public sealed record HealthSegmentViewModel(
    string State,
    int Count,
    double Percent,
    double Height,
    IBrush Brush);

public sealed class GraphNodeViewModel : INotifyPropertyChanged
{
    private bool isSearchMatch;
    private bool isCurrentSearchMatch;

    public GraphNodeViewModel(string kind, string name, string ns, string status, FlatResourceRow? resource = null)
    {
        Kind = kind;
        Name = name;
        Namespace = ns;
        Status = status;
        Resource = resource;
    }

    public string Kind { get; }

    public string Name { get; }

    public string Namespace { get; }

    public string Status { get; }

    public FlatResourceRow? Resource { get; }

    public bool HasResource => Resource is not null;

    public ObservableCollection<GraphNodeViewModel> Children { get; } = [];

    public event PropertyChangedEventHandler? PropertyChanged;

    public bool IsSearchMatch
    {
        get => isSearchMatch;
        set
        {
            if (isSearchMatch == value)
            {
                return;
            }

            isSearchMatch = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(BorderBrush));
            OnPropertyChanged(nameof(BackgroundBrush));
        }
    }

    public bool IsCurrentSearchMatch
    {
        get => isCurrentSearchMatch;
        set
        {
            if (isCurrentSearchMatch == value)
            {
                return;
            }

            isCurrentSearchMatch = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(BorderBrush));
            OnPropertyChanged(nameof(BackgroundBrush));
        }
    }

    public IBrush BorderBrush => IsCurrentSearchMatch
        ? AppThemeCatalog.StatusBrush("WARNING")
        : IsSearchMatch ? AppThemeCatalog.StatusBrush("HEALTHY")
        : Resource is { AlertColor.Length: > 0 } row && !row.AlertColor.Equals("none", StringComparison.OrdinalIgnoreCase) ? AlertBrush(row)
        : Resource is { IsAnnouncing: true } ? AppThemeCatalog.StatusBrush("HEALTHY")
        : AppThemeCatalog.StatusBrush("UNKNOWN");

    public IBrush BackgroundBrush => IsCurrentSearchMatch
        ? SolidColorBrush.Parse("#332A190C")
        : IsSearchMatch ? SolidColorBrush.Parse("#26141D12")
        : Resource is { IsAnnouncing: true } ? SolidColorBrush.Parse("#24141D12")
        : SolidColorBrush.Parse("#18000000");

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }

    private static IBrush AlertBrush(FlatResourceRow row)
    {
        if (row.AlertColor.StartsWith('#') && row.AlertColor.Length is 7 or 9)
        {
            try
            {
                return SolidColorBrush.Parse(row.AlertColor);
            }
            catch (FormatException)
            {
                return AppThemeCatalog.StatusBrush(row.Status);
            }
        }

        return row.AlertColor.ToLowerInvariant() switch
        {
            "fresh" or "cyan" or "green" => AppThemeCatalog.StatusBrush("HEALTHY"),
            "amber" or "yellow" => AppThemeCatalog.StatusBrush("WARNING"),
            "red" => AppThemeCatalog.StatusBrush("CRITICAL"),
            "status" => ProblemAwareStatusBrush(row),
            _ => AppThemeCatalog.StatusBrush(row.Status)
        };
    }

    private static IBrush ProblemAwareStatusBrush(FlatResourceRow row)
    {
        var problem = ResourceFilterMatcher.ProblemReason(row);
        if (problem.Length == 0)
        {
            return AppThemeCatalog.StatusBrush(row.Status);
        }

        return row.Status is "CrashLoopBackOff" or "CreateContainerConfigError" or "CreateContainerError" or "ErrImagePull" or "Error" or "Failed" or "ImagePullBackOff" or "NotReady" or "OOMKilled" or "Unavailable"
               || problem.Contains("Crash", StringComparison.OrdinalIgnoreCase)
               || problem.Contains("Error", StringComparison.OrdinalIgnoreCase)
               || problem.Contains("Failed", StringComparison.OrdinalIgnoreCase)
               || problem.Contains("Unavailable", StringComparison.OrdinalIgnoreCase)
            ? AppThemeCatalog.StatusBrush("CRITICAL")
            : AppThemeCatalog.StatusBrush("WARNING");
    }
}

public sealed class RadarIdleCellViewModel(double x, double y, double width, double height, IBrush brush) : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;

    public double X { get; private set; } = x;

    public double Y { get; private set; } = y;

    public double Width { get; private set; } = width;

    public double Height { get; private set; } = height;

    public IBrush Brush { get; private set; } = brush;

    public void UpdateFrom(RadarIdleCellViewModel source)
    {
        if (!X.Equals(source.X)) { X = source.X; OnPropertyChanged(nameof(X)); }
        if (!Y.Equals(source.Y)) { Y = source.Y; OnPropertyChanged(nameof(Y)); }
        if (!Width.Equals(source.Width)) { Width = source.Width; OnPropertyChanged(nameof(Width)); }
        if (!Height.Equals(source.Height)) { Height = source.Height; OnPropertyChanged(nameof(Height)); }
        if (!ReferenceEquals(Brush, source.Brush)) { Brush = source.Brush; OnPropertyChanged(nameof(Brush)); }
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}

public sealed class RadarBlockViewModel(
    FlatResourceRow resource,
    string group,
    double x,
    double y,
    double width,
    double height,
    IBrush brush,
    string problem,
    string metrics,
    bool isPlaceholder = false,
    bool isSelected = false,
    IBrush? borderBrush = null,
    IBrush? announceBrush = null,
    bool showProblemGlyph = false,
    bool isEventShallow = false,
    string? displayKind = null,
    string? displayName = null,
    bool isClickable = true,
    bool isAnnouncing = false,
    string alertAnimation = "",
    string alertColor = "",
    bool isDimmed = false) : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;

    public FlatResourceRow Resource { get; private set; } = resource;

    public string Group { get; private set; } = group;

    public double X { get; private set; } = x;

    public double Y { get; private set; } = y;

    public double Width { get; private set; } = width;

    public double Height { get; private set; } = height;

    public IBrush Brush { get; private set; } = brush;

    public IBrush BorderBrush { get; private set; } = borderBrush ?? Brushes.Transparent;

    public IBrush AnnounceBrush { get; private set; } = announceBrush ?? borderBrush ?? Brushes.White;

    public string Problem { get; private set; } = problem;

    public string Metrics { get; private set; } = metrics;

    public bool IsPlaceholder { get; private set; } = isPlaceholder;

    public bool IsSelected { get; private set; } = isSelected;

    public string DisplayKind { get; private set; } = displayKind ?? resource.Kind;

    public string DisplayName { get; private set; } = displayName ?? resource.Name;

    public bool ShowProblemGlyph { get; private set; } = showProblemGlyph;

    public bool IsEventShallow { get; private set; } = isEventShallow;

    public bool IsClickable { get; private set; } = isClickable;

    public bool IsAnnouncing { get; private set; } = isAnnouncing;

    public string AlertAnimation { get; private set; } = NormalizeAlertAnimation(alertAnimation);

    public string AlertColor { get; private set; } = alertColor;

    public bool IsBlinkAnimation => IsAnnouncing && AlertAnimation.Equals("blink", StringComparison.OrdinalIgnoreCase);

    public bool IsPulseAnimation => IsAnnouncing && (AlertAnimation.Length == 0 || AlertAnimation.Equals("pulse", StringComparison.OrdinalIgnoreCase));

    public bool IsSweepAnimation => IsAnnouncing && AlertAnimation.Equals("sweep", StringComparison.OrdinalIgnoreCase);

    public bool IsOutlineAnimation => IsAnnouncing && AlertAnimation.Equals("outline", StringComparison.OrdinalIgnoreCase);

    public bool IsDimmed { get; private set; } = isDimmed;

    public double Opacity => IsDimmed ? 0.72 : 1;

    public double BorderThickness => IsSelected ? 2 : IsDimmed ? 0.5 : Problem.Length > 0 ? 1.5 : IsEventShallow ? 1 : 0.5;

    public string ToolTipTitle => IsPlaceholder && IsClickable ? "RADAR IDLE CELL" : $"{DisplayKind}/{DisplayName}";

    public string ToolTipNamespace => IsPlaceholder ? Group : Resource.Namespace ?? "cluster";

    public void UpdateFrom(RadarBlockViewModel source)
    {
        if (Equals(Resource, source.Resource)
            && Group.Equals(source.Group, StringComparison.Ordinal)
            && X.Equals(source.X)
            && Y.Equals(source.Y)
            && Width.Equals(source.Width)
            && Height.Equals(source.Height)
            && ReferenceEquals(Brush, source.Brush)
            && ReferenceEquals(BorderBrush, source.BorderBrush)
            && ReferenceEquals(AnnounceBrush, source.AnnounceBrush)
            && Problem.Equals(source.Problem, StringComparison.Ordinal)
            && Metrics.Equals(source.Metrics, StringComparison.Ordinal)
            && IsPlaceholder == source.IsPlaceholder
            && IsSelected == source.IsSelected
            && DisplayKind.Equals(source.DisplayKind, StringComparison.Ordinal)
            && DisplayName.Equals(source.DisplayName, StringComparison.Ordinal)
            && ShowProblemGlyph == source.ShowProblemGlyph
            && IsEventShallow == source.IsEventShallow
            && IsClickable == source.IsClickable
            && IsAnnouncing == source.IsAnnouncing
            && AlertAnimation.Equals(source.AlertAnimation, StringComparison.Ordinal)
            && AlertColor.Equals(source.AlertColor, StringComparison.Ordinal)
            && IsDimmed == source.IsDimmed)
        {
            return;
        }

        Resource = source.Resource;
        Group = source.Group;
        X = source.X;
        Y = source.Y;
        Width = source.Width;
        Height = source.Height;
        Brush = source.Brush;
        BorderBrush = source.BorderBrush;
        AnnounceBrush = source.AnnounceBrush;
        Problem = source.Problem;
        Metrics = source.Metrics;
        IsPlaceholder = source.IsPlaceholder;
        IsSelected = source.IsSelected;
        DisplayKind = source.DisplayKind;
        DisplayName = source.DisplayName;
        ShowProblemGlyph = source.ShowProblemGlyph;
        IsEventShallow = source.IsEventShallow;
        IsClickable = source.IsClickable;
        IsAnnouncing = source.IsAnnouncing;
        AlertAnimation = source.AlertAnimation;
        AlertColor = source.AlertColor;
        IsDimmed = source.IsDimmed;
        OnPropertyChanged(string.Empty);
    }

    public void SetSelected(bool value)
    {
        if (IsSelected == value)
        {
            return;
        }

        IsSelected = value;
        OnPropertyChanged(nameof(IsSelected));
        OnPropertyChanged(nameof(BorderThickness));
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }

    private static string NormalizeAlertAnimation(string value)
    {
        var normalized = value.Trim().ToLowerInvariant();
        return normalized is "blink" or "pulse" or "sweep" or "outline" ? normalized : "pulse";
    }
}

public sealed class PortForwardTaskViewModel : INotifyPropertyChanged
{
    private string status;
    private Process? process;
    private PodlordPortForward? forwarder;

    public PortForwardTaskViewModel(
        string id,
        string session,
        string kind,
        string name,
        string ns,
        int containerPort,
        int localPort,
        string command,
        string status)
    {
        Id = id;
        Session = session;
        Kind = kind;
        Name = name;
        Namespace = ns;
        ContainerPort = containerPort;
        LocalPort = localPort;
        Command = command;
        this.status = status;
    }

    public string Id { get; }

    public string Session { get; }

    public string Kind { get; }

    public string Name { get; }

    public string Namespace { get; }

    public int ContainerPort { get; }

    public int LocalPort { get; }

    public string Command { get; }

    public event PropertyChangedEventHandler? PropertyChanged;

    public string Status
    {
        get => status;
        set
        {
            if (status == value)
            {
                return;
            }

            status = value;
            OnPropertyChanged();
        }
    }

    public Process? Process
    {
        get => process;
        set
        {
            process = value;
            OnPropertyChanged(nameof(IsRunning));
        }
    }

    public PodlordPortForward? Forwarder
    {
        get => forwarder;
        set
        {
            forwarder = value;
            OnPropertyChanged(nameof(IsRunning));
        }
    }

    public bool IsRunning => Forwarder?.IsRunning == true || Process is { HasExited: false };

    public void Stop()
    {
        Forwarder?.Dispose();
        Forwarder = null;
        if (Process is { HasExited: false } processToStop)
        {
            processToStop.Kill(entireProcessTree: true);
        }
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}
