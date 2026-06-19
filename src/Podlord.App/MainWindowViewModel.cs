using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.Net;
using System.Net.Sockets;
using System.Runtime.CompilerServices;
using System.Text.RegularExpressions;
using System.Text.Json;
using Avalonia.Media;
using Avalonia.Threading;
using Podlord.Core;
using Podlord.Kubernetes;
using YamlDotNet.Core;
using YamlDotNet.RepresentationModel;

namespace Podlord.App;

public sealed class MainWindowViewModel : INotifyPropertyChanged, IDisposable
{
    private const string AllPodLogContainersOption = "All containers";
    private const int RadarLifeColumns = 64;
    private const int RadarLifeRows = 28;

    private readonly AppState state;
    private readonly KubernetesResourceService service;
    private readonly IAlertSoundPlayer soundPlayer;
    private readonly List<FlatResourceRow> cachedRows = [];
    private readonly List<FileSystemWatcher> sourceWatchers = [];
    private readonly HashSet<RadarLifeCell> radarLifeCells = [];
    private readonly Random radarLifeRandom = new();
    private readonly Dictionary<string, int> radarLifeSeenSignatures = [];
    private readonly DispatcherTimer radarIdleTimer = new();
    private readonly DispatcherTimer radarWaterPauseTimer = new();
    private readonly DispatcherTimer radarAutoFollowTimer = new();
    private readonly DispatcherTimer alertSoundQueueTimer = new();
    private readonly DispatcherTimer alertAnimationExpiryTimer = new();
    private readonly DispatcherTimer footerTimer = new();
    private readonly CancellationTokenSource lifetime = new();
    private CancellationTokenSource? refreshDebounce;
    private CancellationTokenSource? filterDebounce;
    private CancellationTokenSource? focusDebounce;
    private CancellationTokenSource? focusLoad;
    private CancellationTokenSource? logTail;
    private CancellationTokenSource? sourceRefreshDebounce;
    private Task? backgroundRefreshTask;
    private PodlordSession? selectedSession;
    private readonly Dictionary<string, DateTimeOffset> sessionSyncedAt = new(StringComparer.Ordinal);
    private FlatResourceRow? selectedResource;
    private FlatResourceRow? selectedResourceRow;
    private SourceStatusRow? selectedSource;
    private string? selectedRadarResourceId;
    private readonly List<string> inspectorHistoryIds = new();
    private int inspectorHistoryCursor = -1;
    private bool suppressInspectorHistory;
    private const int InspectorHistoryMax = 32;
    private string search = string.Empty;
    private string restartFilter = string.Empty;
    private string limitText = "256";
    private string presetName = string.Empty;
    private string importPath = string.Empty;
    private string pasteName = string.Empty;
    private string pasteKubeconfig = string.Empty;
    private string sessionDisplayName = string.Empty;
    private string sessionNamespaceScope = string.Empty;
    private string commandText = string.Empty;
    private string resourceQuickSearch = string.Empty;
    private string graphSearch = string.Empty;
    private string eventQuickSearch = string.Empty;
    private string portQuickSearch = string.Empty;
    private bool isPortSearchOpen;
    private string presetSearch = string.Empty;
    private string selectedWorkspace = "resources";
    private string portContainerPort = "80";
    private string portLocalPort = "8080";
    private string portForwardStatusLine = string.Empty;
    private string statusLine = string.Empty;
    private string detailYaml = string.Empty;
    private string editableYaml = string.Empty;
    private string yamlApplyStatus = string.Empty;
    private string yamlAssistStatus = string.Empty;
    private string? deleteConfirmationResourceId;
    private string logText = string.Empty;
    private string selectedPodLogContainer = AllPodLogContainersOption;
    private string requestWorkLabel = "API 0/min";
    private string healthSummary = string.Empty;
    private int radarWaterActivityRate;
    private double radarCanvasWidth = 480;
    private double radarCanvasHeight = 200;
    private double radarPanelHeight = 184;
    private double radarZoom = 1d;
    private double radarPanX;
    private double radarPanY;
    private bool problemsOnly;
    private bool activityOnly;
    private bool isRefreshing;
    private bool refreshInFlight;
    private bool refreshAgainRequested;
    private bool isCommandPaletteOpen;
    private bool isResourceSearchOpen;
    private bool isGraphSearchOpen;
    private bool isEventSearchOpen;
    private bool applyingPreset;
    private bool logsPaused;
    private bool isPortForwardToolOpen;
    private bool isInspectorVisible;
    private bool isDetailLoading;
    private bool isYamlLoaded;
    private bool isAudioMuted;
    private bool isAppFocused = true;
    private bool isWindowVisible = true;
    private DateTimeOffset? lastSyncedAt;
    private DateTimeOffset lastUserActivityAt = DateTimeOffset.Now;
    private FilterPreset? selectedPreset;
    private AlertRuleRowViewModel? selectedAlertRule;
    private FlatResourceRow? portForwardResource;
    private PortForwardTaskViewModel? selectedPortForward;
    private GraphNodeViewModel? selectedGraphNode;
    private EventTimelineRow? selectedEvent;
    private FlatResourceRow? currentResourceSearchMatch;
    private EventTimelineRow? currentEventSearchMatch;
    private readonly List<GraphNodeViewModel> graphSearchMatches = [];
    private readonly List<FlatResourceRow> resourceSearchMatches = [];
    private readonly List<EventTimelineRow> eventSearchMatches = [];
    private int graphSearchIndex = -1;
    private int resourceSearchIndex = -1;
    private int eventSearchIndex = -1;
    private int radarLifeGeneration;
    private int radarLifeSeed;
    private int radarLifeRuleIndex;
    private int radarLifeStagnantGenerations;
    private string radarLifeLastSignature = string.Empty;
    private int restartOutlierThreshold = ResourceFilterMatcher.DefaultRestartOutlierThreshold;
    private string resourceSortColumn = "Age";
    private ResourceSortDirection resourceSortDirection = ResourceSortDirection.None;
    private string eventSortColumn = "Age";
    private ResourceSortDirection eventSortDirection = ResourceSortDirection.None;
    private bool selectingResource;
    private bool suppressTableSelectionChanges;
    private int portForwardBadgeVersion;
    private DateTimeOffset lastBroadRefreshAt = DateTimeOffset.MinValue;
    private IReadOnlyList<int> portDeclaredPorts = [];
    private string? portDeclaredPortsResourceId;
    private string portDeclaredPortsLabel = "Declared target ports are checked when a resource is selected.";
    private int selectedInspectorTabIndex;
    private int selectedSettingsTabIndex;
    private string? activeLogTailKey;
    private bool isRadarWaterPaused;
    private int radarAutoFollowStep;
    private double radarAutoFollowStartPanX;
    private double radarAutoFollowStartPanY;
    private double radarAutoFollowStartZoom;
    private double radarAutoFollowTargetZoom;
    private double radarAutoFollowTargetPanX;
    private double radarAutoFollowTargetPanY;
    private string lastRadarAutoFollowAlertKey = string.Empty;
    private readonly Queue<RadarAutoFollowRequest> radarAutoFollowQueue = new();
    private readonly HashSet<string> previousVisibleRadarAlertIds = new(StringComparer.Ordinal);
    private readonly Dictionary<string, DateTimeOffset> radarAlertBlinkUntil = new(StringComparer.Ordinal);
    private readonly Dictionary<string, AlertRuleActions> activeAlertActionsByResourceId = new(StringComparer.Ordinal);
    private readonly List<ActiveRadarAlertMatch> activeRadarAlertMatches = [];
    private readonly Dictionary<(string RuleId, string RowId), DateTimeOffset> alertDurationUntilByRuleResource = [];
    private readonly Dictionary<(string RuleId, string RowId), DateTimeOffset> alertColorUntilByRuleResource = [];
    private readonly Dictionary<(string RuleId, string RowId), DateTimeOffset> alertAnimationUntilByRuleResource = [];
    private readonly Dictionary<string, string> lastAlertSoundKeysByRuleId = new(StringComparer.Ordinal);
    private readonly Queue<string> priorityAlertSoundQueue = new();
    private readonly Queue<string> alertSoundQueue = new();
    private readonly HashSet<(string RuleId, string RowId)> previousAlertRuleMatches = [];
    private readonly Dictionary<(string RuleId, string RowId), string> previousAlertRuleRowStates = [];
    private readonly HashSet<string> previousVisibleResourceAlertIds = new(StringComparer.Ordinal);
    private readonly Dictionary<string, DateTimeOffset> resourceAlertBlinkUntil = new(StringComparer.Ordinal);

    public MainWindowViewModel(AppState state, KubernetesResourceService service, IAlertSoundPlayer? soundPlayer = null)
    {
        this.state = state;
        this.service = service;
        this.soundPlayer = soundPlayer ?? AlertSoundPlayerFactory.CreateDefault();
        portForwardStatusLine = T("status.portForwardLine");
        statusLine = T("status.appReady");
        detailYaml = T("status.selectResource");
        editableYaml = T("status.selectResource");
        yamlApplyStatus = T("status.yamlApply");
        yamlAssistStatus = T("status.yamlAssist");
        logText = T("status.selectPod");
        healthSummary = T("status.healthEmpty");
        AppThemeCatalog.Apply(state.Settings().Theme, state.Settings().PixelEffectIntensity, state.Settings().ThemeVariant);
        IssuePicker = new FilterPickerViewModel("Event", "Issue", OnLocalFilterChanged);
        IdPicker = new FilterPickerViewModel("Secret", "ID", OnLocalFilterChanged);
        KindPicker = new FilterPickerViewModel("CustomResourceDefinition", "Kind", OnRemoteFilterChanged);
        NamePicker = new FilterPickerViewModel("Pod", "Name", OnLocalFilterChanged);
        NamespacePicker = new FilterPickerViewModel("Namespace", "Namespace", OnRemoteFilterChanged);
        ClusterPicker = new FilterPickerViewModel("Cluster", "Cluster", OnLocalFilterChanged);
        StatusPicker = new FilterPickerViewModel("Event", "Status", OnLocalFilterChanged);
        AgePicker = new FilterPickerViewModel("CronJob", "Age", OnLocalFilterChanged);
        NodePicker = new FilterPickerViewModel("Node", "Node", OnLocalFilterChanged);
        ImagePicker = new FilterPickerViewModel("ConfigMap", "Image", OnLocalFilterChanged);
        ReadyPicker = new FilterPickerViewModel("Service", "Ready", OnLocalFilterChanged);
        RestartPicker = new FilterPickerViewModel("Event", "Restarts", OnLocalFilterChanged);
        CpuPicker = new FilterPickerViewModel("Node", "CPU", OnLocalFilterChanged);
        MemoryPicker = new FilterPickerViewModel("ConfigMap", "Memory", OnLocalFilterChanged);
        StoragePicker = new FilterPickerViewModel("PersistentVolume", "Storage", OnLocalFilterChanged);
        OwnerPicker = new FilterPickerViewModel("Deployment", "Owner", OnLocalFilterChanged);
        TextPickers =
        [
            ClusterPicker,
            NamespacePicker,
            KindPicker,
            NamePicker,
            StatusPicker,
            IssuePicker,
            AgePicker,
            ReadyPicker,
            RestartPicker,
            CpuPicker,
            MemoryPicker,
            StoragePicker,
            NodePicker,
            ImagePicker,
            OwnerPicker
        ];
        foreach (var preset in FilterPresetStore.Load())
        {
            SavedPresets.Add(preset);
        }

        foreach (var rule in AlertRuleStore.Load())
        {
            AlertRules.Add(new AlertRuleRowViewModel(rule));
        }

        selectedAlertRule = AlertRules.FirstOrDefault();

        var defaultPreset = SavedPresets.First(preset => preset.Name.Equals(FilterPresetStore.DefaultFilterName, StringComparison.OrdinalIgnoreCase));
        selectedPreset = defaultPreset;
        presetName = defaultPreset.Name;
        ApplyPreset(defaultPreset);
        OnPropertyChanged(nameof(SourceFilterOptions));

        UpdateHealthSegments([]);
        RenderRadarLife(reset: true);
        radarIdleTimer.Interval = TimeSpan.FromMilliseconds(320);
        radarIdleTimer.Tick += (_, _) => AdvanceRadarIdleLife();
        radarWaterPauseTimer.Interval = TimeSpan.FromMilliseconds(180);
        radarWaterPauseTimer.Tick += (_, _) =>
        {
            radarWaterPauseTimer.Stop();
            IsRadarWaterPaused = false;
        };
        radarAutoFollowTimer.Interval = TimeSpan.FromMilliseconds(24);
        radarAutoFollowTimer.Tick += (_, _) => StepRadarAutoFollow();
        alertSoundQueueTimer.Interval = TimeSpan.FromMilliseconds(650);
        alertSoundQueueTimer.Tick += (_, _) => PlayNextQueuedAlertSound();
        alertAnimationExpiryTimer.Interval = TimeSpan.FromMilliseconds(250);
        alertAnimationExpiryTimer.Tick += (_, _) => ExpireAlertAnimations();
        if (state.Settings().ScreensaverEnabled)
        {
            radarIdleTimer.Start();
        }

        footerTimer.Interval = TimeSpan.FromSeconds(1);
        footerTimer.Tick += (_, _) => RefreshTimeLabels();
        footerTimer.Start();

        backgroundRefreshTask = BackgroundRefreshLoop(lifetime.Token);
        UpdateRequestWorkLabel();
        PortForwards.CollectionChanged += (_, _) => RefreshPortForwardBadges();
        SavedPresets.CollectionChanged += (_, _) =>
        {
            OnPropertyChanged(nameof(VisibleSavedPresets));
            OnPropertyChanged(nameof(SourceFilterOptions));
        };
        Sessions.CollectionChanged += (_, _) =>
        {
            OnPropertyChanged(nameof(IsAppEmpty));
            NotifyResourceLogoStateChanged();
        };
        Resources.CollectionChanged += (_, _) => NotifyResourceLogoStateChanged();
    }

    public bool IsAppEmpty => Sessions.Count == 0;

    public bool IsResourceLogoVisible => !IsAppEmpty && IsResourcesWorkspace && Resources.Count == 0;

    public string ResourceLogoTitle => cachedRows.Count > 0
        ? T("resource.noMatchingTitle")
        : IsRefreshing ? T("resource.loadingTitle") : T("resource.emptyTitle");

    public string ResourceLogoMessage
    {
        get
        {
            if (cachedRows.Count > 0)
            {
                return T("resource.noMatchingMessage");
            }

            if (IsRefreshing)
            {
                return T("resource.loadingMessage");
            }

            return T("resource.emptyMessage");
        }
    }

    public ObservableCollection<PodlordSession> Sessions { get; } = [];

    public ObservableCollection<FlatResourceRow> Resources { get; } = [];

    public ObservableCollection<DetailItem> Summary { get; } = [];

    public ObservableCollection<FocusMetricRow> FocusMetrics { get; } = [];

    public ObservableCollection<ResourceValueRow> ResourceValues { get; } = [];

    public ObservableCollection<HealthSegmentViewModel> HealthSegments { get; } = [];

    public ObservableCollection<EventTimelineRow> FocusedEvents { get; } = [];

    public ObservableCollection<RelationshipRow> FocusedRelationships { get; } = [];

    public ObservableCollection<ResourceListFailure> Failures { get; } = [];

    public ObservableCollection<FilterPreset> SavedPresets { get; } = [];

    public ObservableCollection<AlertRuleRowViewModel> AlertRules { get; } = [];

    public ObservableCollection<ActiveAlertRow> ActiveAlerts { get; } = [];

    public ObservableCollection<SourceStatusRow> Sources { get; } = [];

    public ObservableCollection<ImportedContextRowViewModel> ImportedContextRows { get; } = [];

    public ObservableCollection<RequestAuditRow> RequestAuditRows { get; } = [];

    private string sourcePickerSearch = string.Empty;

    public string SourcePickerSearch
    {
        get => sourcePickerSearch;
        set
        {
            if (SetField(ref sourcePickerSearch, value))
            {
                OnPropertyChanged(nameof(VisibleImportedContexts));
            }
        }
    }

    public IEnumerable<ImportedContextRowViewModel> VisibleImportedContexts
    {
        get
        {
            var query = sourcePickerSearch.Trim();
            if (query.Length == 0)
            {
                return ImportedContextRows;
            }

            return ImportedContextRows.Where(row =>
                row.DisplayName.Contains(query, StringComparison.OrdinalIgnoreCase)
                || row.SourcePath.Contains(query, StringComparison.OrdinalIgnoreCase));
        }
    }

    public string ImportedSourcesLabel => $"SOURCES ({ImportedContextRows.Count})";

    public string RadarSourceLabel
    {
        get
        {
            if (SelectedSession is null)
            {
                return "NO SOURCE SELECTED";
            }

            var context = state.Snapshot().ImportedContexts.FirstOrDefault(candidate => candidate.ContextId == SelectedSession.ContextId);
            var source = context is null
                ? SelectedSession.ClusterName
                : string.IsNullOrWhiteSpace(context.SourceName) ? SourceName(context.SourcePath) : context.SourceName;
            var parts = new[] { source, SelectedSession.DisplayName, SelectedSession.ClusterName }
                .Where(value => !string.IsNullOrWhiteSpace(value))
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .ToList();
            return string.Join(" / ", parts);
        }
    }

    public string ActiveSessionChipLabel
    {
        get
        {
            if (SelectedSession is null)
            {
                return "NO SOURCE SELECTED";
            }

            var context = state.Snapshot().ImportedContexts.FirstOrDefault(candidate => candidate.ContextId == SelectedSession.ContextId);
            var source = context is null
                ? SelectedSession.ClusterName
                : string.IsNullOrWhiteSpace(context.SourceName) ? SourceName(context.SourcePath) : context.SourceName;
            return $"{source} / {SelectedSession.DisplayName} / {SelectedSession.NamespaceScope.Label}";
        }
    }

    public ObservableCollection<EventTimelineRow> Events { get; } = [];

    public ObservableCollection<RelationshipRow> Relationships { get; } = [];

    public ObservableCollection<GraphNodeViewModel> GraphNodes { get; } = [];

    public ObservableCollection<RadarBlockViewModel> RadarBlocks { get; } = [];

    public ObservableCollection<RadarIdleCellViewModel> RadarIdleCells { get; } = [];

    private bool isRadarIdle = true;

    public bool IsRadarIdle
    {
        get => isRadarIdle;
        private set
        {
            if (SetField(ref isRadarIdle, value))
            {
                OnPropertyChanged(nameof(IsRadarData));
                OnPropertyChanged(nameof(IsRadarWaterVisible));
            }
        }
    }

    public bool IsRadarData => !IsRadarIdle;

    internal void ForceRadarLiveForTesting() => IsRadarIdle = false;

    internal void SeedCachedRowsForTesting(IEnumerable<FlatResourceRow> rows)
    {
        cachedRows.Clear();
        cachedRows.AddRange(rows);
        restartOutlierThreshold = ResourceFilterMatcher.RestartOutlierThreshold(cachedRows);
        UpdateHealthSegments(cachedRows);
        ApplyLocalFilter();
    }

    public bool IsRadarWaterVisible => IsRadarData && state.Settings().RadarWaterEnabled && state.Settings().RadarWaterSpeed > 0;

    public int RadarIdleSeed => radarLifeSeed;

    public string RadarIdleRuleLabel => RadarLifeRules[radarLifeRuleIndex].Label;

    public double RadarZoom => radarZoom;

    public string RadarZoomLabel => $"{radarZoom * 100:0}%";

    public double RadarPanX => radarPanX;

    public double RadarPanY => radarPanY;

    public int RadarWaterActivityRate
    {
        get => radarWaterActivityRate;
        private set => SetField(ref radarWaterActivityRate, value);
    }

    public bool IsRadarWaterPaused
    {
        get => isRadarWaterPaused;
        private set => SetField(ref isRadarWaterPaused, value);
    }

    public bool IsAudioMuted
    {
        get => isAudioMuted;
        private set
        {
            if (SetField(ref isAudioMuted, value))
            {
                OnPropertyChanged(nameof(AudioMuteGlyph));
                OnPropertyChanged(nameof(AudioMuteText));
            }
        }
    }

    public string AudioMuteGlyph => IsAudioMuted ? "Hidden" : "Sound";

    public string AudioMuteText => IsAudioMuted ? T("audio.unmute") : T("audio.mute");

    public double RadarPanelHeight
    {
        get => radarPanelHeight;
        private set => SetField(ref radarPanelHeight, value);
    }

    public double RadarCanvasW
    {
        get => radarCanvasWidth;
        private set => SetField(ref radarCanvasWidth, value);
    }

    public double RadarCanvasH
    {
        get => radarCanvasHeight;
        private set => SetField(ref radarCanvasHeight, value);
    }

    public ObservableCollection<PortForwardTaskViewModel> PortForwards { get; } = [];

    public ObservableCollection<string> CommandSuggestions { get; } = [];

    public ObservableCollection<PulseMetricCard> ClusterPulseItems { get; } = [];

    public IReadOnlyList<string> ThemeOptions => AppThemeCatalog.ThemeNames;

    public IReadOnlyList<string> ThemeVariantOptions => AppThemeCatalog.ThemeVariantNames;

    public IReadOnlyList<string> GraphicsQualityOptions => AppThemeCatalog.ThemeIntensityNames;

    public IReadOnlyList<string> LanguageOptions => PodlordLocalizer.LanguageOptionLabels;

    public string NavSearchText => T("nav.search");

    public string NavResourcesText => T("nav.resources");

    public string NavGraphText => T("nav.graph");

    public string NavEventsText => T("nav.events");

    public string NavPortsText => T("nav.ports");

    public string NavSettingsText => T("nav.settings");

    public string SourcesTitleText => T("sources.title");

    public string ImportPlaceholderText => T("sources.importPlaceholder");

    public string ImportActionText => T("action.import");

    public string ImportFileTipText => T("sources.importFileTip");

    public string ManageActionText => T("action.manage");

    public string FiltersTitleText => T("filters.title");

    public string ProblemsText => T("filters.problems");

    public string ActivityText => T("filters.activity");

    public string SavedFiltersText => T("filters.savedFilters");

    public string SearchSavedFiltersText => T("filters.searchSaved");

    public string FilterNamePlaceholderText => T("filters.namePlaceholder");

    public string SaveActionText => T("action.save");

    public string DeleteActionText => T("action.delete");

    public string DuplicateActionText => T("action.duplicate");

    public string AddActionText => T("action.add");

    public string ClearActionText => T("action.clear");

    public string CloseActionText => T("action.close");

    public string PortActionText => T("action.port");

    public string ApplyServerSideActionText => T("action.applyServerSide");

    public string ResetActionText => T("action.reset");

    public string SettingsTitleText => T("settings.title");

    public string SettingsAlertsText => T("settings.alerts");

    public string SettingsSourcesText => T("settings.sources");

    public string SettingsAppearanceText => T("settings.appearance");

    public string SettingsGraphicsText => T("settings.graphics");

    public string SettingsSyncText => T("settings.sync");

    public string SettingsWorkspaceText => T("settings.workspace");

    public string SettingsPrivacyText => T("settings.privacy");

    public string SettingsDiagnosticsText => T("settings.diagnostics");

    public string SettingsAboutText => T("settings.about");

    public string AboutTaglineText => T("about.tagline");

    public string AboutSupportHeadingText => T("about.supportHeading");

    public string AboutProjectHeadingText => T("about.projectHeading");

    public string AboutStarRepoButtonText => T("about.starRepo");

    public string AboutGithubRepoButtonText => T("about.githubRepo");

    public string AboutCreateIssueButtonText => T("about.createIssue");

    public string AboutSponsorsButtonText => T("about.sponsors");

    public string AboutBuyMeACoffeeButtonText => T("about.bmc");

    public string AboutKoFiButtonText => T("about.kofi");

    public string AboutLiberapayButtonText => T("about.liberapay");

    public string LogContainerLabelText => T("logs.container");

    public string LogPauseTailText => T("logs.pauseTail");

    public string LogPauseTailHelpText => T("logs.pauseTailHelp");

    public string ResizeInspectorTooltipText => T("tooltip.resizeInspector");

    public string PreviousResourceTooltipText => T("tooltip.previousResource");

    public string NextResourceTooltipText => T("tooltip.nextResource");

    public string CloseSearchTooltipText => T("tooltip.closeSearch");

    public string PreviousMatchTooltipText => T("tooltip.previousMatch");

    public string NextMatchTooltipText => T("tooltip.nextMatch");

    public string RenameSourceTooltipText => T("tooltip.renameSource");

    public string DeleteSourceTooltipText => T("tooltip.deleteSource");

    public string FilterProblemsTooltipText => T("tooltip.filterProblems");

    public string FilterActivityTooltipText => T("tooltip.filterActivity");

    public string EditFilterNameTooltipText => T("tooltip.editFilterName");

    public string RenameFilterTooltipText => T("tooltip.renameFilter");

    public string DeleteFilterTooltipText => T("tooltip.deleteFilter");

    public string PreparePortForwardTooltipText => T("tooltip.preparePortForward");

    public string VariantTooltipText => T("tooltip.variantHelp");

    public string ThemeIntensityTooltipText => T("tooltip.themeIntensityHelp");

    public string RemoveSnapshotTooltipText => T("tooltip.removeSnapshot");

    public string PortForwardColumnTooltipText => T("tooltip.portForwardColumn");

    public string AboutRepoUrl => "https://github.com/YunaBraska/podlord";

    public string AboutIssueUrl => "https://github.com/YunaBraska/podlord/issues/new";

    public string AboutStarUrl => "https://github.com/YunaBraska/podlord/stargazers";

    public string AboutSponsorsUrl => "https://github.com/sponsors/YunaBraska";

    public string AboutBuyMeACoffeeUrl => "https://buymeacoffee.com/YunaBraska";

    public string AboutKoFiUrl => "https://ko-fi.com/YunaBraska";

    public string AboutLiberapayUrl => "https://liberapay.com/YunaBraska";

    private static readonly string[] AboutBlocks =
    {
        "kubectl shouts. etcd whispers. Podlord listens.\nBuilt with heart, not equity.\nStar the repo if it survived your Monday. Fuel me if it survived your week.",
        "Pods come and go. Your sanity should not.\nOne human built this between deploys and despair.\nStar it. Donate when it spares you another describe.",
        "YAML stands for Yet Another Misindented Line.\nPodlord stands for whatever you needed it to.\nHit the star if we agree. Coffee link is right there.",
        "There are 10 kinds of people. The other 2 wrote this.\nNo VC, no roadmap, just stubborn craftsmanship.\nUse it, star it. Love it, fuel it.",
        "Kubernetes has no developer experience.\nSo I built one. Open source, single maintainer, dangerously caffeinated.\nA star costs nothing. A coffee buys a feature.",
        "Sidecars exist because containers can't keep their lanes.\nThis UI exists because dashboards can't keep yours.\nIf it helped, leave a star. If it shipped, leave a tip.",
        "RBAC: Role Based Annoyance Constructs.\nPodlord: small joy in a heavy stack.\nStar it after use. Donate if it earned its rent on your dock.",
        "Helm chart. Helm fault. Same energy.\nMade late at night because the alternative was rage.\nStar it, fund it, file an issue. Whichever feels right.",
        "Liveness probes were named by an optimist.\nPodlord was named after stubbornness.\nIf this UI is in your week, drop a star. If it's in your day, drop a coffee.",
        "The cloud is somebody else's panic.\nThis console is mine, shared with you.\nA star lowers it by one bar. A donation by two.",
        "ConfigMap: a love letter from past you to future you, half redacted.\nPodlord just reads it back without the suffering.\nStar if useful, fuel if essential.",
        "There is no SRE. Only severely resigned engineers.\nThis was built by one of them, for the rest of you.\nStar the repo. Buy the coffee. Keep the lights on.",
        "Crashloop: a feature of consistency.\nPodlord: a feature of restraint.\nStar if you noticed the difference. Donate if you appreciate it.",
        "Operators wake up at 3 AM so you don't have to.\nThis UI wakes up at the speed of your click.\nLeave a star before the next page.",
        "The control plane is fine. It said so itself.\nPodlord checks anyway, then shows you the truth.\nIf the truth helped, send back a coffee.",
        "Service mesh: yet another layer of indirection.\nPodlord: one less layer between you and the pod.\nStar earned. Coffee earned. Trust earned.",
        "Day 1: hello world.\nDay 712: namespace not found.\nPodlord was built between those two days. A star says thanks.",
        "Init containers run first, finish first, are forgotten first.\nThis maintainer kind of relates.\nA star or a coffee fixes it both.",
        "Eventually consistent means eventually correct, possibly never.\nPodlord aims for now consistent, now visible.\nIf you saw the difference, leave a tip.",
        "Resource limits are a suggestion. So is sleep.\nThis app was built ignoring both.\nReturn the favor with a star or a small donation.",
        "Logs: 90% noise, 9% noise, 1% truth.\nPodlord finds the 1% faster.\nStar the repo. Buy the coffee. Skip the next outage.",
        "Pod disruption budget: your patience.\nMaintainer disruption budget: the donation jar.\nKeep both topped up.",
        "Stateful sets are stateful. Maintainers are tired.\nPodlord makes both more bearable.\nStar if you used it twice today.",
        "Annotations are post it notes nobody reads.\nPodlord reads them so you don't have to.\nA coffee says thanks.",
        "There is no cloud. Only computers you cry over.\nThis console makes the crying shorter.\nStar the repo, fuel the maintainer.",
        "Probes lie. Metrics lie. Pods occasionally tell the truth.\nPodlord lets you watch the truth happen.\nIf that mattered today, leave a star.",
        "kubectl get pods solves nothing.\nkubectl get pods on repeat solves less.\nPodlord solves the repeat. Coffee link below.",
        "Distributed systems: you knew the risks.\nDistributed sanity: nobody warned you.\nA donation keeps the second one online.",
        "Reconcile: a verb done by the system, a noun done by the maintainer.\nPodlord does the first. Your star does the second.",
        "Latency is a feeling.\nThis UI tries to feel quick.\nIf it did today, drop a star or a coffee on the way out.",
        "OOMKilled: out of memory, killed.\nMaintainer of Podlord: out of money, still alive.\nA donation keeps the second statement true.",
        "If Kubernetes were easy, you would not be reading this.\nThis console makes hard things visible.\nStar it. Fuel it. Send it to a friend."
    };

    private int aboutBlockIndex = -1;

    public string AboutBlockText
    {
        get
        {
            if (aboutBlockIndex < 0 || aboutBlockIndex >= AboutBlocks.Length)
            {
                aboutBlockIndex = PickAboutBlockIndex(aboutBlockIndex);
            }
            return AboutBlocks[aboutBlockIndex];
        }
    }

    public void PickAboutBlock()
    {
        aboutBlockIndex = PickAboutBlockIndex(aboutBlockIndex);
        OnPropertyChanged(nameof(AboutBlockText));
    }

    private static int PickAboutBlockIndex(int previous)
    {
        if (AboutBlocks.Length <= 1)
        {
            return 0;
        }
        var seed = unchecked((int)((uint)DateTime.Now.Ticks ^ (uint)Environment.TickCount));
        var pick = Math.Abs(seed) % AboutBlocks.Length;
        if (pick == previous)
        {
            pick = (pick + 1) % AboutBlocks.Length;
        }
        return pick;
    }

    public void OpenAboutUrl(string? url)
    {
        if (string.IsNullOrWhiteSpace(url))
        {
            return;
        }
        if (!Uri.TryCreate(url, UriKind.Absolute, out var uri))
        {
            return;
        }
        try
        {
            Process.Start(new ProcessStartInfo(uri.ToString()) { UseShellExecute = true });
            StatusLine = $"Opened {uri.Host}";
        }
        catch (InvalidOperationException) { }
        catch (System.ComponentModel.Win32Exception) { }
    }

    public static IReadOnlyList<string> AboutBlockCatalog => AboutBlocks;

    public string ThemeText => T("settings.theme");

    public string VariantText => T("settings.variant");

    public string ThemeIntensityText => T("settings.themeIntensity");

    public string ThemeHelpText => T("settings.themeHelp");

    public string VariantHelpText => T("settings.variantHelp");

    public string ThemeIntensityHelpText => T("settings.themeIntensityHelp");

    public string LanguageText => T("settings.language");

    public string LanguageHelpText => T("settings.languageHelp");

    public string RadarWaterText => T("settings.radarWater");

    public string RadarWaterHelpText => T("settings.radarWaterHelp");

    public string RadarWaterSpeedText => T("settings.radarWaterSpeed");

    public string RadarWaterSpeedHelpText => T("settings.radarWaterSpeedHelp");

    public string AnimationIntensityText => T("settings.animationIntensity");

    public string AnimationHelpText => T("settings.animationHelp");

    public string RadarAutoFollowText => T("settings.radarAutoFollow");

    public string RadarAutoFollowHelpText => T("settings.radarAutoFollowHelp");

    public string RadarScreensaverText => T("settings.radarScreensaver");

    public string GraphicsHelpText => T("settings.graphicsHelp");

    public string InactiveBackgroundSyncText => T("settings.inactiveBackgroundSync");

    public string RequestHardLimitText => T("settings.requestHardLimit");

    public string RequestHardLimitHelpText => T("settings.requestHardLimitHelp");

    public string WorkspaceRestoreText => T("settings.workspaceRestore");

    public string WorkspaceRestoreHelpText => T("settings.workspaceRestoreHelp");

    public string TelemetryText => T("settings.telemetry");

    public string TelemetryHelpText => T("settings.telemetryHelp");

    public string RequestAuditTitleText => T("settings.requestAuditTitle");

    public string AlertActiveText => T("alert.active");

    public string AlertTypeText => T("alert.type");

    public string AlertNameText => T("alert.name");

    public string AlertDescriptionText => T("alert.description");

    public string AlertWhenText => T("alert.when");

    public string AlertActionsText => T("alert.actions");

    public string AlertSoundText => T("alert.sound");

    public string AlertMatchersText => T("alert.matchers");

    public string AlertOrMatcherText => T("alert.orMatcher");

    public string AlertMatcherBlockHelpText => T("alert.matcherBlockHelp");

    public string AlertAndText => T("alert.and");

    public string AlertRemoveMatcherBlockText => T("alert.removeMatcherBlock");

    public string AlertRemoveMatcherText => T("alert.removeMatcher");

    public string AlertColorText => T("alert.color");

    public string AlertNoColorText => T("alert.noColor");

    public string AlertStatusColorText => T("alert.statusColor");

    public string AlertAnimationText => T("alert.animation");

    public string AlertZoomText => T("alert.zoom");

    public string AlertPreviewZoomText => T("alert.previewZoom");

    public string AlertSoundSearchText => T("alert.soundSearch");

    public string AlertPreviewSoundText => T("alert.previewSound");

    public string AlertAuthorText => T("alert.author");

    public string AlertSourceText => T("alert.source");

    public string AlertAssetText => T("alert.asset");

    public string FilterSearchOrCustomText => T("filters.searchOrCustom");

    public string CustomValuesText => T("filters.customValues");

    public string FilterSyntaxHelpText => T("filters.syntaxHelp");

    public string InspectorOverviewText => T("inspector.overview");

    public string InspectorYamlText => T("inspector.yaml");

    public string InspectorEventsText => T("inspector.events");

    public string InspectorLinksText => T("inspector.links");

    public string InspectorLogsText => T("inspector.logs");

    public string InspectorValuesText => T("inspector.values");

    public string YamlTabIndentText => T("yaml.tabIndent");

    public string PortForwardTitleText => T("port.title");

    public string PortContainerPortText => T("port.containerPort");

    public string PortLocalPortText => T("port.localPort");

    public string PortContainerPortTipText => T("port.containerPortTip");

    public string PortLocalPortTipText => T("port.localPortTip");

    public IReadOnlyList<string> InactiveSyncOptions { get; } =
    [
        "disabled",
        "1m",
        "5m",
        "10m",
        "20m",
        "30m",
        "60m"
    ];

    public IReadOnlyList<string> RequestHardLimitOptions { get; } =
    [
        "none",
        "30/min",
        "60/min",
        "120/min",
        "240/min",
        "600/min"
    ];

    public IReadOnlyList<FilterPickerViewModel> TextPickers { get; }

    public FilterPickerViewModel IssuePicker { get; }

    public FilterPickerViewModel IdPicker { get; }

    public FilterPickerViewModel KindPicker { get; }

    public FilterPickerViewModel NamePicker { get; }

    public FilterPickerViewModel NamespacePicker { get; }

    public FilterPickerViewModel ClusterPicker { get; }

    public FilterPickerViewModel StatusPicker { get; }

    public FilterPickerViewModel AgePicker { get; }

    public FilterPickerViewModel NodePicker { get; }

    public FilterPickerViewModel ImagePicker { get; }

    public FilterPickerViewModel ReadyPicker { get; }

    public FilterPickerViewModel RestartPicker { get; }

    public FilterPickerViewModel CpuPicker { get; }

    public FilterPickerViewModel MemoryPicker { get; }

    public FilterPickerViewModel StoragePicker { get; }

    public FilterPickerViewModel OwnerPicker { get; }

    public event PropertyChangedEventHandler? PropertyChanged;

    public PodlordSession? SelectedSession
    {
        get => selectedSession;
        set
        {
            if (Equals(selectedSession, value))
            {
                return;
            }

            selectedSession = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(IsInitialLoading));
            SessionDisplayName = value?.DisplayName ?? string.Empty;
            SessionNamespaceScope = value?.NamespaceScope.Label ?? string.Empty;
            OnPropertyChanged(nameof(ActiveSessionChipLabel));
            OnPropertyChanged(nameof(RadarSourceLabel));
            MarkUserActivity();
            CancelFocusLoad();
            StopLogTail();
            RestoreLastFilterForSession();
            RestoreSelectedSessionCache();
            ScheduleRefresh();
        }
    }

    private bool suppressFilterPersist;

    private void RestoreLastFilterForSession()
    {
        if (selectedSession is null)
        {
            return;
        }
        var context = state.Snapshot().ImportedContexts.FirstOrDefault(c => c.ContextId == selectedSession.ContextId);
        if (context is null)
        {
            return;
        }
        var preset = SavedPresets.FirstOrDefault(p => p.Name.Equals(context.FilterName, StringComparison.OrdinalIgnoreCase));
        if (preset is null || (selectedPreset?.Name.Equals(preset.Name, StringComparison.OrdinalIgnoreCase) == true))
        {
            return;
        }
        suppressFilterPersist = true;
        try
        {
            SelectedPreset = preset;
        }
        finally
        {
            suppressFilterPersist = false;
        }
    }

    private void PersistFilterForSession(string filterName)
    {
        if (suppressFilterPersist || selectedSession is null || string.IsNullOrWhiteSpace(filterName))
        {
            return;
        }
        try
        {
            state.SetImportedContextFilter(selectedSession.ContextId, filterName);
        }
        catch (PodlordException)
        {
        }
    }

    public FlatResourceRow? SelectedResource
    {
        get => selectedResource;
        set
        {
            if (value is null)
            {
                CloseInspector();
                return;
            }

            FocusResourceFromSurface(value, SelectionSurface.Resource);
        }
    }

    public FlatResourceRow? SelectedResourceRow
    {
        get => selectedResourceRow;
        set
        {
            if (suppressTableSelectionChanges)
            {
                if (value is null || selectedResource?.Id == value.Id)
                {
                    selectedResourceRow = value;
                    OnPropertyChanged();
                }

                return;
            }

            if (selectedResourceRow?.Id == value?.Id)
            {
                return;
            }

            selectedResourceRow = value;
            OnPropertyChanged();
            if (!selectingResource && value is not null)
            {
                FocusResourceFromSurface(value, SelectionSurface.Table);
            }
        }
    }

    public SourceStatusRow? SelectedSource
    {
        get => selectedSource;
        set
        {
            if (ReferenceEquals(selectedSource, value))
            {
                return;
            }

            selectedSource = value;
            OnPropertyChanged();
            if (value is not null)
            {
                FocusSource(value);
            }
        }
    }

    public string Search
    {
        get => search;
        set
        {
            if (SetField(ref search, value))
            {
                OnLocalFilterChanged();
            }
        }
    }

    public string RestartFilter
    {
        get => RestartPicker.Expression;
        set
        {
            var normalized = value ?? string.Empty;
            if (RestartPicker.Expression == normalized)
            {
                return;
            }

            restartFilter = normalized;
            RestartPicker.SetExpression(normalized);
            OnPropertyChanged();
        }
    }

    public string LimitText
    {
        get => limitText;
        set
        {
            if (SetField(ref limitText, value))
            {
                OnLocalFilterChanged();
            }
        }
    }

    public string PresetName
    {
        get => presetName;
        set => SetField(ref presetName, value);
    }

    public string PresetSearch
    {
        get => presetSearch;
        set
        {
            if (SetField(ref presetSearch, value))
            {
                OnPropertyChanged(nameof(VisibleSavedPresets));
            }
        }
    }

    public string SelectedPresetLabel => SelectedPreset?.Name ?? "saved filters";

    public IEnumerable<FilterPreset> VisibleSavedPresets
    {
        get
        {
            var query = presetSearch.Trim();
            if (query.Length == 0)
            {
                return SavedPresets;
            }

            return SavedPresets.Where(preset => preset.Name.Contains(query, StringComparison.OrdinalIgnoreCase));
        }
    }

    public FilterPreset? SelectedPreset
    {
        get => selectedPreset;
        set
        {
            value ??= SavedPresets.FirstOrDefault(preset => preset.Name.Equals(FilterPresetStore.DefaultFilterName, StringComparison.OrdinalIgnoreCase));
            if (selectedPreset?.Name == value?.Name)
            {
                return;
            }

            selectedPreset = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(SelectedPresetLabel));
            if (value is not null)
            {
                ApplyPreset(value);
                PersistFilterForSession(value.Name);
            }
        }
    }

    public AlertRuleRowViewModel? SelectedAlertRule
    {
        get => selectedAlertRule;
        set
        {
            if (ReferenceEquals(selectedAlertRule, value))
            {
                return;
            }

            selectedAlertRule = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(IsAlertRuleSelected));
            OnPropertyChanged(nameof(CanDeleteSelectedAlertRule));
        }
    }

    public bool IsAlertRuleSelected => SelectedAlertRule is not null;

    public bool CanDeleteSelectedAlertRule => SelectedAlertRule?.CanDelete == true;

    public IReadOnlyList<string> AlertUntilOptions { get; } = [AlertUntilModes.NoMatch, AlertUntilModes.Duration];

    public IReadOnlyList<AlertSoundDefinition> AlertSoundOptions => AlertSoundCatalog.BuiltIn;

    public IReadOnlyList<string> AlertSoundChoices => AlertSoundCatalog.BuiltIn.Select(sound => sound.Label).ToList();

    public bool ProblemsOnly
    {
        get => problemsOnly;
        set
        {
            if (problemsOnly == value)
            {
                return;
            }

            problemsOnly = value;
            if (value && activityOnly)
            {
                activityOnly = false;
                OnPropertyChanged(nameof(ActivityOnly));
            }

            OnPropertyChanged();
            OnLocalFilterChanged();
        }
    }

    public bool ActivityOnly
    {
        get => activityOnly;
        set
        {
            if (activityOnly == value)
            {
                return;
            }

            activityOnly = value;
            if (value && problemsOnly)
            {
                problemsOnly = false;
                OnPropertyChanged(nameof(ProblemsOnly));
            }

            OnPropertyChanged();
            OnLocalFilterChanged();
        }
    }

    public string ImportPath
    {
        get => importPath;
        set => SetField(ref importPath, value);
    }

    public string PasteName
    {
        get => pasteName;
        set => SetField(ref pasteName, value);
    }

    public string PasteKubeconfig
    {
        get => pasteKubeconfig;
        set => SetField(ref pasteKubeconfig, value);
    }

    public string SessionDisplayName
    {
        get => sessionDisplayName;
        set => SetField(ref sessionDisplayName, value);
    }

    public string SessionNamespaceScope
    {
        get => sessionNamespaceScope;
        set => SetField(ref sessionNamespaceScope, value);
    }

    public IReadOnlyList<string> SourceFilterOptions => SavedPresets
        .Select(preset => preset.Name)
        .Distinct(StringComparer.OrdinalIgnoreCase)
        .Order(StringComparer.OrdinalIgnoreCase)
        .ToList();

    public string SelectedWorkspace
    {
        get => selectedWorkspace;
        set
        {
            if (SetField(ref selectedWorkspace, value))
            {
                OnPropertyChanged(nameof(IsResourcesWorkspace));
                OnPropertyChanged(nameof(IsGraphWorkspace));
                OnPropertyChanged(nameof(IsEventsWorkspace));
                OnPropertyChanged(nameof(IsSourcesWorkspace));
                OnPropertyChanged(nameof(IsPortsWorkspace));
                OnPropertyChanged(nameof(IsSettingsWorkspace));
                OnPropertyChanged(nameof(IsResourcesNavActive));
                OnPropertyChanged(nameof(IsGraphNavActive));
                OnPropertyChanged(nameof(IsEventsNavActive));
                OnPropertyChanged(nameof(IsPortsNavActive));
                OnPropertyChanged(nameof(IsSourcesNavActive));
                OnPropertyChanged(nameof(IsSettingsNavActive));
                NotifyResourceLogoStateChanged();
            }
        }
    }

    public bool IsResourcesWorkspace => SelectedWorkspace == "resources";

    public bool IsGraphWorkspace => SelectedWorkspace == "graph";

    public bool IsEventsWorkspace => SelectedWorkspace == "events";

    public bool IsSourcesWorkspace => SelectedWorkspace == "sources";

    public bool IsPortsWorkspace => SelectedWorkspace == "ports";

    public bool IsSettingsWorkspace => SelectedWorkspace == "settings";

    public bool IsResourcesNavActive => IsResourcesWorkspace;

    public bool IsGraphNavActive => IsGraphWorkspace;

    public bool IsEventsNavActive => IsEventsWorkspace;

    public bool IsPortsNavActive => IsPortsWorkspace;

    public bool IsSourcesNavActive => IsSourcesWorkspace;

    public bool IsSettingsNavActive => IsSettingsWorkspace;

    public int SelectedSettingsTabIndex
    {
        get => selectedSettingsTabIndex;
        set => SetField(ref selectedSettingsTabIndex, value);
    }

    public void OpenSourcesSettings()
    {
        SelectedWorkspace = "settings";
        SelectedSettingsTabIndex = 5;
        IsCommandPaletteOpen = false;
        MarkUserActivity();
    }

    public IReadOnlyList<TableColumnLayout> TableColumnLayout(string tableId)
    {
        return state.Settings().TableColumnLayouts?
            .Where(layout => layout.TableId.Equals(tableId, StringComparison.Ordinal))
            .ToList() ?? [];
    }

    public void SaveTableColumnLayout(string tableId, IEnumerable<TableColumnLayout> layout)
    {
        var settings = state.Settings();
        var replacement = layout
            .Where(item => item.TableId.Equals(tableId, StringComparison.Ordinal) && !string.IsNullOrWhiteSpace(item.ColumnId))
            .GroupBy(item => item.ColumnId, StringComparer.Ordinal)
            .Select(group => group.OrderBy(item => item.DisplayIndex).First())
            .OrderBy(item => item.DisplayIndex)
            .ToList();
        var preserved = settings.TableColumnLayouts?
            .Where(item => !item.TableId.Equals(tableId, StringComparison.Ordinal))
            .ToList() ?? [];
        preserved.AddRange(replacement);
        SaveSettings(settings with { TableColumnLayouts = preserved });
    }

    public bool IsCommandPaletteOpen
    {
        get => isCommandPaletteOpen;
        set => SetField(ref isCommandPaletteOpen, value);
    }

    public bool IsResourceSearchOpen
    {
        get => isResourceSearchOpen;
        set => SetField(ref isResourceSearchOpen, value);
    }

    public bool IsGraphSearchOpen
    {
        get => isGraphSearchOpen;
        set => SetField(ref isGraphSearchOpen, value);
    }

    public bool IsEventSearchOpen
    {
        get => isEventSearchOpen;
        set => SetField(ref isEventSearchOpen, value);
    }

    public bool LogsPaused
    {
        get => logsPaused;
        set => SetField(ref logsPaused, value);
    }

    public bool IsPortForwardToolOpen
    {
        get => isPortForwardToolOpen;
        set => SetField(ref isPortForwardToolOpen, value);
    }

    public string CommandText
    {
        get => commandText;
        set
        {
            if (SetField(ref commandText, value))
            {
                UpdateCommandSuggestions();
            }
        }
    }

    public string ResourceQuickSearch
    {
        get => resourceQuickSearch;
        set
        {
            if (SetField(ref resourceQuickSearch, value))
            {
                OnLocalFilterChanged();
                UpdateResourceSearchMatches(resetToFirstMatch: true);
            }
        }
    }

    public string GraphSearch
    {
        get => graphSearch;
        set
        {
            if (SetField(ref graphSearch, value))
            {
                UpdateGraphSearchMatches(resetToFirstMatch: true);
            }
        }
    }

    public string EventQuickSearch
    {
        get => eventQuickSearch;
        set
        {
            if (SetField(ref eventQuickSearch, value))
            {
                ApplyLocalFilter();
                UpdateEventSearchMatches(resetToFirstMatch: true);
            }
        }
    }

    public string PortQuickSearch
    {
        get => portQuickSearch;
        set
        {
            if (SetField(ref portQuickSearch, value))
            {
                OnPropertyChanged(nameof(VisiblePortForwards));
            }
        }
    }

    public bool IsPortSearchOpen
    {
        get => isPortSearchOpen;
        set => SetField(ref isPortSearchOpen, value);
    }

    public IEnumerable<PortForwardTaskViewModel> VisiblePortForwards
    {
        get
        {
            var expression = portQuickSearch.Trim();
            var active = PortForwards.Where(IsActivePortForward).ToList();
            if (expression.Length == 0)
            {
                return active;
            }

            return active.Where(task =>
                ResourceFilterMatcher.MatchesText(task.Kind, expression)
                || ResourceFilterMatcher.MatchesText(task.Name, expression)
                || ResourceFilterMatcher.MatchesText(task.Namespace, expression)
                || ResourceFilterMatcher.MatchesText(task.Session, expression)
                || ResourceFilterMatcher.MatchesText(task.Command, expression)
                || ResourceFilterMatcher.MatchesText(task.Status, expression));
        }
    }

    public int PortForwardBadgeVersion => portForwardBadgeVersion;

    public GraphNodeViewModel? SelectedGraphNode
    {
        get => selectedGraphNode;
        set => SetField(ref selectedGraphNode, value);
    }

    public EventTimelineRow? SelectedEvent
    {
        get => selectedEvent;
        set
        {
            if (!SetField(ref selectedEvent, value) || value is null)
            {
                return;
            }

            FocusEvent(value);
        }
    }

    public FlatResourceRow? CurrentResourceSearchMatch
    {
        get => currentResourceSearchMatch;
        private set => SetField(ref currentResourceSearchMatch, value);
    }

    public EventTimelineRow? CurrentEventSearchMatch
    {
        get => currentEventSearchMatch;
        private set => SetField(ref currentEventSearchMatch, value);
    }

    public string ResourceMatchLabel => resourceSearchMatches.Count == 0
        ? "0/0"
        : $"{resourceSearchIndex + 1}/{resourceSearchMatches.Count}";

    public string GraphMatchLabel => graphSearchMatches.Count == 0
        ? "0/0"
        : $"{graphSearchIndex + 1}/{graphSearchMatches.Count}";

    public string EventMatchLabel => eventSearchMatches.Count == 0
        ? "0/0"
        : $"{eventSearchIndex + 1}/{eventSearchMatches.Count}";

    public string ResourceSortLabel => resourceSortDirection == ResourceSortDirection.None
        ? "SORT AGE NEWEST"
        : $"SORT {resourceSortColumn.ToUpperInvariant()} {resourceSortDirection.ToString().ToUpperInvariant()}";

    public string EventSortLabel => eventSortDirection == ResourceSortDirection.None
        ? "SORT AGE NEWEST"
        : $"SORT {eventSortColumn.ToUpperInvariant()} {eventSortDirection.ToString().ToUpperInvariant()}";

    public string ResourceSortGlyphFor(string column)
    {
        return SortGlyph(resourceSortColumn, resourceSortDirection, column);
    }

    public string EventSortGlyphFor(string column)
    {
        return SortGlyph(eventSortColumn, eventSortDirection, column);
    }

    public FlatResourceRow? PortForwardResource
    {
        get => portForwardResource;
        private set
        {
            if (portForwardResource?.Id == value?.Id)
            {
                return;
            }

            portForwardResource = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(PortForwardResourceLabel));
            OnPropertyChanged(nameof(PortForwardActionLabel));
        }
    }

    public string PortForwardResourceLabel => PortForwardResource is null
        ? "Select a Pod or Service from the table."
        : $"{PortForwardResource.Kind}/{PortForwardResource.Name} in {PortForwardResource.Namespace ?? "cluster"}";

    public string PortForwardStatusLine
    {
        get => portForwardStatusLine;
        private set => SetField(ref portForwardStatusLine, value);
    }

    public string PortDeclaredPortsLabel
    {
        get => portDeclaredPortsLabel;
        private set => SetField(ref portDeclaredPortsLabel, value);
    }

    public string PortContainerPort
    {
        get => portContainerPort;
        set => SetField(ref portContainerPort, value);
    }

    public string PortLocalPort
    {
        get => portLocalPort;
        set => SetField(ref portLocalPort, value);
    }

    public PortForwardTaskViewModel? SelectedPortForward
    {
        get => selectedPortForward;
        set
        {
            if (SetField(ref selectedPortForward, value))
            {
                OnPropertyChanged(nameof(IsPortForwardStopMode));
                OnPropertyChanged(nameof(PortForwardActionLabel));
            }
        }
    }

    public bool IsPortForwardStopMode => SelectedPortForward is not null && IsActivePortForward(SelectedPortForward);

    public string PortForwardActionLabel => IsPortForwardStopMode ? "STOP" : "START";

    public string StatusLine
    {
        get => statusLine;
        private set => SetField(ref statusLine, value);
    }

    public string RequestWorkLabel
    {
        get => requestWorkLabel;
        private set => SetField(ref requestWorkLabel, value);
    }

    public string HealthSummary
    {
        get => healthSummary;
        private set => SetField(ref healthSummary, value);
    }

    public string DetailYaml
    {
        get => detailYaml;
        private set
        {
            if (SetField(ref detailYaml, value))
            {
                EditableYaml = value;
            }
        }
    }

    public string EditableYaml
    {
        get => editableYaml;
        set
        {
            if (SetField(ref editableYaml, value))
            {
                ValidateEditableYaml();
            }
        }
    }

    public string YamlApplyStatus
    {
        get => yamlApplyStatus;
        private set => SetField(ref yamlApplyStatus, value);
    }

    public string YamlAssistStatus
    {
        get => yamlAssistStatus;
        private set => SetField(ref yamlAssistStatus, value);
    }

    public string LogText
    {
        get => logText;
        private set => SetField(ref logText, value);
    }

    public bool IsRefreshing
    {
        get => isRefreshing;
        private set
        {
            if (SetField(ref isRefreshing, value))
            {
                NotifyResourceLogoStateChanged();
                OnPropertyChanged(nameof(IsInitialLoading));
            }
        }
    }

    public bool IsInitialLoading => SelectedSession is not null && cachedRows.Count == 0 && IsRefreshing;

    public bool IsInspectorVisible
    {
        get => isInspectorVisible;
        private set
        {
            if (SetField(ref isInspectorVisible, value))
            {
                NotifyInspectorTabStateChanged();
            }
        }
    }

    public ObservableCollection<string> PodLogContainerOptions { get; } = [AllPodLogContainersOption];

    public string SelectedPodLogContainer
    {
        get => selectedPodLogContainer;
        set
        {
            if (!SetField(ref selectedPodLogContainer, value))
            {
                return;
            }

            MarkUserActivity();
            UpdateInspectorTabWork();
        }
    }

    public int SelectedInspectorTabIndex
    {
        get => selectedInspectorTabIndex;
        set
        {
            var wasYaml = IsInspectorYamlActive;
            if (!SetField(ref selectedInspectorTabIndex, value))
            {
                return;
            }

            MarkUserActivity();
            NotifyInspectorTabStateChanged();
            OnPropertyChanged(nameof(IsInspectorLogsActive));
            UpdateInspectorTabWork();
            if (!wasYaml && IsInspectorYamlActive)
            {
                _ = LoadFreshYamlAsync();
            }
        }
    }

    public bool IsDetailLoading
    {
        get => isDetailLoading;
        private set => SetField(ref isDetailLoading, value);
    }

    public bool IsSelectedSource => selectedSource is not null;

    public bool IsSelectedKubernetesResource => SelectedResource is not null && !IsSelectedSource;

    public string SelectedResourceLabel => SelectedResource is null
        ? "No resource selected"
        : $"{SelectedResource.Kind}/{SelectedResource.Name}";

    public bool IsSelectedResourceKeyValueResource => IsSelectedKubernetesResource
        && SelectedResource?.Kind is "ConfigMap" or "Secret";

    public bool IsSelectedResourceLoggable => IsSelectedKubernetesResource
        && SelectedResource is { Kind: "Pod" } pod
        && !IsFinishedPodStatus(pod.Status);

    private static bool IsFinishedPodStatus(string? status)
    {
        return status is "Succeeded" or "Failed" or "Completed" or "Evicted" or "OOMKilled";
    }

    public bool IsInspectorOverviewActive => SelectedInspectorTabIndex == 0;

    public bool IsInspectorYamlActive => SelectedInspectorTabIndex == 1;

    public bool IsInspectorEventsActive => IsSelectedKubernetesResource && SelectedInspectorTabIndex == 2;

    public bool IsInspectorLinksActive => IsSelectedKubernetesResource && SelectedInspectorTabIndex == 3;

    public bool IsInspectorLogsActive => IsInspectorVisible && IsSelectedResourceLoggable && SelectedInspectorTabIndex == 4;

    public bool IsInspectorValuesActive => IsSelectedResourceKeyValueResource && SelectedInspectorTabIndex == 5;

    public bool CanPortForwardSelectedResource => IsSelectedKubernetesResource && IsPortForwardableResource(SelectedResource);

    public bool CanDeleteSelectedResource => IsSelectedKubernetesResource
                                             && SelectedResource is { } row
                                             && !IsVirtualRadarResource(row)
                                             && !row.Kind.Equals("Cluster", StringComparison.Ordinal)
                                             && !row.Kind.Equals("Source", StringComparison.Ordinal);

    public string DeleteActionLabel => deleteConfirmationResourceId == SelectedResource?.Id ? "CONFIRM DELETE" : "DELETE";

    public string ResourceCountLabel => $"{Resources.Count} visible / {cachedRows.Count} cached";

    public string LastSyncedLabel => lastSyncedAt is null ? "not synced" : $"synced {HumanSince(lastSyncedAt.Value)} ago";

    public string FooterLine
    {
        get
        {
            var telemetry = service.RequestTelemetry();
            var synced = lastSyncedAt is null ? "never" : $"{HumanSince(lastSyncedAt.Value)} ago";
            return $"visible: {Resources.Count}/{cachedRows.Count}  API: {telemetry.RequestsLastMinute}/min  Synced: {synced}";
        }
    }

    public string ThemeSetting
    {
        get => AppThemeCatalog.Normalize(state.Settings().Theme);
        set => SaveSettings(state.Settings() with { Theme = AppThemeCatalog.Normalize(value) });
    }

    public string ThemeVariantSetting
    {
        get => AppThemeCatalog.NormalizeVariant(state.Settings().ThemeVariant);
        set => SaveSettings(state.Settings() with { ThemeVariant = AppThemeCatalog.NormalizeVariant(value) });
    }

    public string LanguageSetting
    {
        get => PodlordLocalizer.LanguageOptionLabel(state.Settings().Language);
        set => SaveSettings(state.Settings() with { Language = PodlordLocalizer.LanguageCodeFromLabel(value) });
    }

    public string GraphicsQualitySetting
    {
        get => AppThemeCatalog.IntensityName(state.Settings().PixelEffectIntensity);
        set
        {
            var normalized = AppThemeCatalog.IntensityName(AppThemeCatalog.PixelEffectIntensity(value));
            SaveSettings(state.Settings() with
            {
                PixelEffectIntensity = AppThemeCatalog.PixelEffectIntensity(normalized),
                AnimationIntensity = normalized switch
                {
                    "arcade" => 85,
                    "medium" => 55,
                    _ => 20
                }
            });
        }
    }

    public double AnimationIntensitySetting
    {
        get => state.Settings().AnimationIntensity;
        set => SaveSettings(state.Settings() with { AnimationIntensity = (byte)Math.Clamp((int)Math.Round(value), 0, 100) });
    }

    public string AnimationIntensityLabel => $"{state.Settings().AnimationIntensity}%";

    public bool RadarWaterEnabledSetting
    {
        get => state.Settings().RadarWaterEnabled;
        set => SaveSettings(state.Settings() with { RadarWaterEnabled = value });
    }

    public double RadarWaterSpeedSetting
    {
        get => state.Settings().RadarWaterSpeed;
        set => SaveSettings(state.Settings() with { RadarWaterSpeed = (byte)Math.Clamp((int)Math.Round(value), 0, 100) });
    }

    public int RadarWaterSpeedPercent => state.Settings().RadarWaterSpeed;

    public string RadarWaterSpeedLabel => $"{state.Settings().RadarWaterSpeed}%";

    public bool RadarAutoFollowAlertsSetting
    {
        get => state.Settings().RadarAutoFollowAlerts;
        set => SaveSettings(state.Settings() with { RadarAutoFollowAlerts = value });
    }

    public string InactiveSyncSetting
    {
        get => InactiveSyncLabel(state.Settings().InactiveSyncMinutes);
        set => SaveSettings(state.Settings() with { InactiveSyncMinutes = ParseInactiveSyncMinutes(value) });
    }

    public string InactiveSyncDescription => state.Settings().InactiveSyncMinutes <= 0
        ? "When the app is unfocused or idle, background resource sync pauses. Visible cached data remains available."
        : $"When the app is unfocused or idle, sync only after Synced reaches {InactiveSyncLabel(state.Settings().InactiveSyncMinutes)}.";

    public string RequestHardLimitSetting
    {
        get => RequestHardLimitLabel(state.Settings().RequestHardLimitPerMinute);
        set => SaveSettings(state.Settings() with { RequestHardLimitPerMinute = ParseRequestHardLimitPerMinute(value) });
    }

    public string RequestHardLimitDescription => state.Settings().RequestHardLimitPerMinute <= 0
        ? "No extra request ceiling is applied. Podlord still uses cache TTLs, backoff, priority, and idle sync rules."
        : $"Actual Kubernetes request starts are capped at {state.Settings().RequestHardLimitPerMinute}/min in addition to dynamic sync and backoff.";

    public bool WorkspaceRestoreSetting
    {
        get => state.Settings().WorkspaceRestore;
        set => SaveSettings(state.Settings() with { WorkspaceRestore = value });
    }

    public bool TelemetrySetting
    {
        get => state.Settings().TelemetryEnabled;
        set => SaveSettings(state.Settings() with { TelemetryEnabled = value });
    }

    public bool ScreensaverSetting
    {
        get => state.Settings().ScreensaverEnabled;
        set
        {
            SaveSettings(state.Settings() with { ScreensaverEnabled = value });
            UpdateRadarIdleTimer();
        }
    }

    public void LoadStartupKubeconfigs(IReadOnlyList<string> paths)
    {
        try
        {
            state.ImportHomeKubeconfig();
        }
        catch (PodlordException)
        {
            // Home kubeconfig is an automatic default import when present.
        }

        foreach (var path in paths.Where(File.Exists))
        {
            try
            {
                state.ImportKubeconfig(path);
            }
            catch (PodlordException ex)
            {
                StatusLine = ex.Message;
            }
        }

        try
        {
            state.RefreshImportedKubeconfigs();
        }
        catch (PodlordException ex)
        {
            StatusLine = ex.Message;
        }

        ReloadSessions();
    }

    public void ReloadSessions()
    {
        Sessions.Clear();
        foreach (var session in state.ListSessions())
        {
            Sessions.Add(session);
        }

        ReloadSources();
        SelectedSession = Sessions.FirstOrDefault(session => session.Active) ?? Sessions.FirstOrDefault();
        OnPropertyChanged(nameof(ActiveSessionChipLabel));
        OnPropertyChanged(nameof(RadarSourceLabel));
    }

    public void ImportHome()
    {
        try
        {
            var summary = state.ImportHomeKubeconfig();
            StatusLine = $"Imported {summary.Contexts.Count} context(s) from home kubeconfig.";
            ReloadSessions();
        }
        catch (PodlordException ex)
        {
            StatusLine = ex.Message;
        }
    }

    public async Task ImportK3dNowAsync()
    {
        try
        {
            var clusters = await ListK3dClustersAsync(lifetime.Token).ConfigureAwait(true);
            if (clusters.Count == 0)
            {
                StatusLine = "No k3d clusters found.";
                return;
            }

            var contexts = 0;
            foreach (var cluster in clusters)
            {
                var kubeconfig = await ReadK3dKubeconfigAsync(cluster, lifetime.Token).ConfigureAwait(true);
                var summary = state.ImportGeneratedKubeconfigText($"k3d/{cluster}", NormalizeLocalK3dEndpoint(kubeconfig));
                contexts += summary.Contexts.Count;
            }

            StatusLine = $"Imported {contexts} context(s) from {clusters.Count} k3d cluster(s).";
            ReloadSessions();
        }
        catch (Exception ex) when (ex is InvalidOperationException or IOException or PodlordException or JsonException)
        {
            StatusLine = ex.Message;
        }
    }

    public void ImportPathNow()
    {
        if (string.IsNullOrWhiteSpace(ImportPath))
        {
            StatusLine = "Choose a kubeconfig file or enter a path, directory, or YAML.";
            return;
        }

        try
        {
            var value = ImportPath.Trim();
            if (LooksLikeKubeconfigYaml(value))
            {
                var inlineSummary = state.ImportKubeconfigText("inline-import", value);
                ImportPath = string.Empty;
                ReloadSessions();
                StatusLine = $"Imported {inlineSummary.Contexts.Count} inline kubeconfig context(s).";
                return;
            }

            var resolved = ExpandUserPath(value);
            if (Directory.Exists(resolved))
            {
                var (files, contexts, failures) = ImportKubeconfigDirectory(resolved, maxDepth: 32);
                ImportPath = string.Empty;
                ReloadSessions();
                StatusLine = $"Imported {contexts} context(s) from {files} kubeconfig file(s); ignored {failures} non-kubeconfig YAML file(s).";
                return;
            }

            var fileSummary = state.ImportKubeconfig(resolved);
            ImportPath = string.Empty;
            ReloadSessions();
            StatusLine = $"Imported {fileSummary.Contexts.Count} context(s).";
        }
        catch (PodlordException ex) when (ex.Kind == PodlordErrorKind.EmptyKubeconfig)
        {
            StatusLine = ex.NextAction;
        }
        catch (PodlordException ex)
        {
            StatusLine = ex.Message;
        }
    }

    /// <summary>
    /// Imports one or more kubeconfig files or directories selected from the file/folder picker.
    /// Files and directories are accepted together; directories are scanned recursively.
    /// </summary>
    public void ImportPaths(IReadOnlyList<string> paths)
    {
        if (paths.Count == 0)
        {
            StatusLine = "No kubeconfig file or folder selected.";
            return;
        }

        var files = 0;
        var contexts = 0;
        var failures = 0;
        var errors = new List<string>();
        foreach (var raw in paths)
        {
            var resolved = ExpandUserPath(raw.Trim());
            try
            {
                if (Directory.Exists(resolved))
                {
                    var (dirFiles, dirContexts, dirFailures) = ImportKubeconfigDirectory(resolved, maxDepth: 32);
                    files += dirFiles;
                    contexts += dirContexts;
                    failures += dirFailures;
                    continue;
                }

                var summary = state.ImportKubeconfig(resolved);
                files += 1;
                contexts += summary.Contexts.Count;
            }
            catch (PodlordException ex)
            {
                failures += 1;
                errors.Add(ex.Message);
            }
        }

        ImportPath = string.Empty;
        ReloadSessions();
        StatusLine = errors.Count == 0
            ? $"Imported {contexts} context(s) from {files} kubeconfig file(s); ignored {failures} non-kubeconfig file(s)."
            : $"Imported {contexts} context(s) from {files} file(s); {failures} failed: {errors[0]}";
    }

    /// <summary>Expands a leading <c>~</c> and environment variables so typed paths like <c>~/.kube</c> resolve.</summary>
    internal static string ExpandUserPath(string path)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            return path;
        }

        var expanded = Environment.ExpandEnvironmentVariables(path);
        if (expanded == "~" || expanded.StartsWith("~/", StringComparison.Ordinal) || expanded.StartsWith("~\\", StringComparison.Ordinal))
        {
            var home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
            expanded = expanded.Length <= 1 ? home : Path.Combine(home, expanded[2..]);
        }

        return expanded;
    }

    public void ImportPasteNow()
    {
        if (string.IsNullOrWhiteSpace(PasteKubeconfig))
        {
            StatusLine = "Paste kubeconfig YAML first.";
            return;
        }

        try
        {
            var summary = state.ImportKubeconfigText(PasteName, PasteKubeconfig);
            PasteKubeconfig = string.Empty;
            StatusLine = $"Imported {summary.Contexts.Count} pasted context(s).";
            ReloadSessions();
        }
        catch (PodlordException ex)
        {
            StatusLine = ex.Message;
        }
    }

    public void RefreshSourcesNow()
    {
        try
        {
            var summaries = state.RefreshImportedKubeconfigs();
            StatusLine = $"Refreshed {summaries.Count} kubeconfig source(s).";
            ReloadSessions();
        }
        catch (PodlordException ex)
        {
            StatusLine = ex.Message;
        }
    }

    public void SaveSelectedSource()
    {
        if (selectedSource is null)
        {
            StatusLine = "Select a kubeconfig source first.";
            return;
        }

        try
        {
            var contextId = selectedSource.ContextId;
            var updatedContext = state.RenameImportedContext(contextId, selectedSource.Context);
            state.SetImportedContextFilter(contextId, selectedSource.FilterName);
            var session = state.ListSessions().FirstOrDefault(candidate => candidate.ContextId == contextId);
            if (session is not null)
            {
                state.SetSessionDisplayName(session.Id, updatedContext.DisplayName);
            }

            StatusLine = $"Saved source {updatedContext.DisplayName}.";
            ReloadSessions();
            SelectSourceByContextId(contextId);
        }
        catch (PodlordException ex)
        {
            StatusLine = ex.Message;
        }
    }

    public void SaveSelectedSession()
    {
        if (SelectedSession is null)
        {
            StatusLine = "Select a session first.";
            return;
        }

        try
        {
            state.SetSessionDisplayName(SelectedSession.Id, SessionDisplayName);
            state.SetSessionNamespaceScope(SelectedSession.Id, ScopeFromText(SessionNamespaceScope));
            StatusLine = $"Saved session {SessionDisplayName}.";
            ReloadSessions();
        }
        catch (PodlordException ex)
        {
            StatusLine = ex.Message;
        }
    }

    private (int Files, int Contexts, int Failures) ImportKubeconfigDirectory(string directory, int maxDepth)
    {
        var files = EnumerateKubeconfigFiles(directory, maxDepth).ToList();
        var importedFiles = 0;
        var importedContexts = 0;
        var failures = 0;

        foreach (var file in files)
        {
            try
            {
                var summary = state.ImportKubeconfig(file);
                importedFiles++;
                importedContexts += summary.Contexts.Count;
            }
            catch (PodlordException)
            {
                failures++;
            }
        }

        return (importedFiles, importedContexts, failures);
    }

    private static IEnumerable<string> EnumerateKubeconfigFiles(string directory, int maxDepth)
    {
        var root = Path.GetFullPath(directory);
        var pending = new Stack<(string Directory, int Depth)>();
        pending.Push((root, 0));

        while (pending.Count > 0)
        {
            var (current, depth) = pending.Pop();
            IEnumerable<string> files;
            try
            {
                files = Directory.EnumerateFiles(current);
            }
            catch (IOException)
            {
                continue;
            }
            catch (UnauthorizedAccessException)
            {
                continue;
            }

            foreach (var file in files)
            {
                if (LooksLikeKubeconfigFileName(file))
                {
                    yield return file;
                }
            }

            if (depth >= maxDepth)
            {
                continue;
            }

            IEnumerable<string> children;
            try
            {
                children = Directory.EnumerateDirectories(current);
            }
            catch (IOException)
            {
                continue;
            }
            catch (UnauthorizedAccessException)
            {
                continue;
            }

            foreach (var child in children)
            {
                pending.Push((child, depth + 1));
            }
        }
    }

    private static bool LooksLikeKubeconfigYaml(string value)
    {
        return value.Contains('\n')
               || value.StartsWith("apiVersion:", StringComparison.OrdinalIgnoreCase)
               || value.Contains("contexts:", StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>
    /// Matches files that are plausibly kubeconfigs by name when scanning a directory: common YAML/kubeconfig
    /// extensions, or the conventional extensionless <c>config</c>/<c>kubeconfig</c> file. Hidden files and
    /// non-config artifacts (scripts, notes) are skipped; content is still validated on import.
    /// </summary>
    internal static bool LooksLikeKubeconfigFileName(string path)
    {
        var name = Path.GetFileName(path);
        if (name.Length == 0 || name.StartsWith('.'))
        {
            return false;
        }

        var extension = Path.GetExtension(name).ToLowerInvariant();
        if (extension is ".yaml" or ".yml" or ".kubeconfig" or ".conf" or ".config" or ".cfg" or ".kube")
        {
            return true;
        }

        return extension.Length == 0
               && (name.Equals("config", StringComparison.OrdinalIgnoreCase)
                   || name.Equals("kubeconfig", StringComparison.OrdinalIgnoreCase));
    }

    private void ValidateEditableYaml()
    {
        if (string.IsNullOrWhiteSpace(EditableYaml))
        {
            YamlAssistStatus = "YAML syntax: empty.";
            return;
        }

        YamlAssistStatus = IsYamlSyntaxValid(EditableYaml, out var error)
            ? $"YAML syntax: ok; {EditableYaml.Split('\n').Length} line(s)."
            : $"YAML syntax: {error}";
    }

    private static bool IsYamlSyntaxValid(string yaml, out string error)
    {
        try
        {
            var stream = new YamlStream();
            stream.Load(new StringReader(yaml));
            error = string.Empty;
            return true;
        }
        catch (YamlException ex)
        {
            error = ex.Start.Line > 0
                ? $"line {ex.Start.Line}, column {ex.Start.Column}: {ex.Message}"
                : ex.Message;
            return false;
        }
    }

    private string ResolveFilterName(string? value)
    {
        var name = string.IsNullOrWhiteSpace(value) ? FilterPresetStore.DefaultFilterName : value.Trim();
        return SavedPresets.Any(preset => preset.Name.Equals(name, StringComparison.OrdinalIgnoreCase))
            ? name
            : FilterPresetStore.DefaultFilterName;
    }

    public void DuplicateSelectedSession()
    {
        if (SelectedSession is null)
        {
            StatusLine = "Select a session first.";
            return;
        }

        try
        {
            var copy = state.DuplicateSession(SelectedSession.Id);
            StatusLine = $"Duplicated session {copy.DisplayName}.";
            ReloadSessions();
        }
        catch (PodlordException ex)
        {
            StatusLine = ex.Message;
        }
    }

    public void SelectWorkspace(string workspace)
    {
        SelectedWorkspace = workspace;
        IsCommandPaletteOpen = false;
        MarkUserActivity();
    }

    public void SaveCurrentFilter()
    {
        var name = string.IsNullOrWhiteSpace(PresetName)
            ? SelectedPreset?.Name ?? $"Filter {DateTimeOffset.Now:HHmmss}"
            : PresetName.Trim();
        var preset = new FilterPreset(
            name,
            ProblemsOnly,
            Search,
            string.Empty,
            IssuePicker.Expression,
            KindPicker.Expression,
            NamePicker.Expression,
            NamespacePicker.Expression,
            ClusterPicker.Expression,
            StatusPicker.Expression,
            AgePicker.Expression,
            NodePicker.Expression,
            ImagePicker.Expression,
            ReadyPicker.Expression,
            RestartFilter,
            OwnerPicker.Expression,
            LimitText,
            ActivityOnly,
            CpuPicker.Expression,
            MemoryPicker.Expression,
            StoragePicker.Expression);
        var existing = SavedPresets.FirstOrDefault(item => item.Name.Equals(name, StringComparison.Ordinal));
        if (existing is not null)
        {
            SavedPresets.Remove(existing);
        }

        SavedPresets.Add(preset);
        FilterPresetStore.Save(SavedPresets);
        SelectedPreset = preset;
        PresetName = name;
        OnPropertyChanged(nameof(SourceFilterOptions));
        StatusLine = $"Saved filter '{name}'.";
    }

    public void RemoveSelectedFilter()
    {
        if (SelectedPreset is null)
        {
            StatusLine = "Select a saved filter first.";
            return;
        }

        DeleteSavedFilter(SelectedPreset);
    }

    public void BeginRenameSavedFilter(FilterPreset preset)
    {
        SelectedPreset = preset;
        PresetName = preset.Name;
        PresetSearch = string.Empty;
        StatusLine = $"Editing filter name '{preset.Name}'. Change the name and press save.";
    }

    public void RenameSavedFilter(FilterPreset preset, string requestedName)
    {
        var oldName = preset.Name.Trim();
        var newName = requestedName.Trim();
        if (newName.Length == 0)
        {
            StatusLine = "Filter name cannot be empty.";
            return;
        }

        if (!preset.CanRename)
        {
            StatusLine = "Default filter cannot be renamed. Save over it to update it.";
            return;
        }

        if (oldName.Equals(newName, StringComparison.Ordinal))
        {
            PresetName = newName;
            return;
        }

        var index = SavedPresets.IndexOf(preset);
        if (index < 0)
        {
            index = SavedPresets
                .Select((item, itemIndex) => new { item, itemIndex })
                .FirstOrDefault(candidate => candidate.item.Name.Equals(oldName, StringComparison.OrdinalIgnoreCase))
                ?.itemIndex ?? -1;
        }

        if (index < 0)
        {
            var alreadyRenamed = SavedPresets.FirstOrDefault(item => item.Name.Equals(newName, StringComparison.OrdinalIgnoreCase));
            if (alreadyRenamed is not null)
            {
                selectedPreset = alreadyRenamed;
                OnPropertyChanged(nameof(SelectedPreset));
                OnPropertyChanged(nameof(SelectedPresetLabel));
                PresetName = alreadyRenamed.Name;
                StatusLine = $"Filter '{alreadyRenamed.Name}' is already renamed.";
                return;
            }

            StatusLine = $"Filter '{oldName}' no longer exists.";
            return;
        }

        if (SavedPresets
            .Select((item, itemIndex) => new { item, itemIndex })
            .Any(candidate => candidate.itemIndex != index && candidate.item.Name.Equals(newName, StringComparison.OrdinalIgnoreCase)))
        {
            StatusLine = $"Filter '{newName}' already exists.";
            return;
        }

        preset = SavedPresets[index];
        oldName = preset.Name.Trim();
        var renamed = preset with { Name = newName };
        SavedPresets[index] = renamed;
        UpdateSourceFilterAssignments(oldName, newName);
        FilterPresetStore.Save(SavedPresets);
        if (SelectedPreset?.Name.Equals(oldName, StringComparison.Ordinal) == true)
        {
            selectedPreset = renamed;
            OnPropertyChanged(nameof(SelectedPreset));
            OnPropertyChanged(nameof(SelectedPresetLabel));
        }

        PresetName = SelectedPreset?.Name.Equals(newName, StringComparison.Ordinal) == true ? newName : PresetName;
        PresetSearch = string.Empty;
        OnPropertyChanged(nameof(VisibleSavedPresets));
        OnPropertyChanged(nameof(SourceFilterOptions));
        ReloadSources();
        StatusLine = $"Renamed filter '{oldName}' to '{newName}'.";
    }

    public void DeleteSavedFilter(FilterPreset preset)
    {
        if (preset.Name.Equals(FilterPresetStore.DefaultFilterName, StringComparison.OrdinalIgnoreCase))
        {
            StatusLine = "Default filter cannot be deleted. Save over it to update it.";
            return;
        }

        if (!SavedPresets.Remove(preset))
        {
            return;
        }

        if (SelectedPreset?.Name == preset.Name)
        {
            SelectedPreset = SavedPresets.First(item => item.Name.Equals(FilterPresetStore.DefaultFilterName, StringComparison.OrdinalIgnoreCase));
        }

        FilterPresetStore.Save(SavedPresets);
        OnPropertyChanged(nameof(SourceFilterOptions));
        StatusLine = $"Removed filter '{preset.Name}'.";
    }

    public void AddAlertRule()
    {
        var rule = new AlertRuleRowViewModel(new AlertRule(
            $"custom-{Guid.NewGuid():N}",
            "New alert",
            "Custom alert rule.",
            true,
            false,
            string.Empty,
            new AlertRuleMatchers(Kind: "\"Pod\""),
            new AlertRuleActions(RadarFocus: false, RadarZoom: false, RadarBlink: false, RadarColor: false, PlaySound: false),
            new AlertRuleUntil("none"),
            "none"));
        AlertRules.Add(rule);
        SelectedAlertRule = rule;
        StatusLine = T("alert.added");
    }

    public void DuplicateSelectedAlertRule()
    {
        if (SelectedAlertRule is null)
        {
            StatusLine = T("alert.selectFirst");
            return;
        }

        var source = SelectedAlertRule.ToRule();
        var copy = new AlertRuleRowViewModel(source with
        {
            Id = $"custom-{Guid.NewGuid():N}",
            Name = $"{source.Name} copy",
            BuiltIn = false
        });
        AlertRules.Add(copy);
        SelectedAlertRule = copy;
        StatusLine = TF("alert.duplicated", source.Name);
    }

    public void DeleteSelectedAlertRule()
    {
        if (SelectedAlertRule is null)
        {
            StatusLine = T("alert.selectFirst");
            return;
        }

        if (!SelectedAlertRule.CanDelete)
        {
            StatusLine = T("alert.builtinNoDelete");
            return;
        }

        var removed = SelectedAlertRule;
        if (AlertRules.Remove(removed))
        {
            SelectedAlertRule = AlertRules.FirstOrDefault();
            SaveAlertRules();
            StatusLine = TF("alert.deleted", removed.Name);
        }
    }

    public void ToggleAlertRule(AlertRuleRowViewModel rule)
    {
        rule.Enabled = !rule.Enabled;
        SaveAlertRules();
        StatusLine = rule.Enabled ? TF("alert.enabled", rule.Name) : TF("alert.disabled", rule.Name);
    }

    public void RemoveAlertMatcherGroup(AlertMatcherGroupViewModel group)
    {
        SelectedAlertRule?.RemoveGroup(group);
        SaveAlertRules();
    }

    public void AddAlertMatcherGroup()
    {
        if (SelectedAlertRule is null)
        {
            StatusLine = T("alert.selectFirst");
            return;
        }

        SelectedAlertRule.AddMatcherGroup();
        SaveAlertRules();
    }

    public void AddAlertMatcherCriterion(AlertMatcherGroupViewModel group)
    {
        SelectedAlertRule?.AddCriterion(group);
        SaveAlertRules();
    }

    public void RemoveAlertMatcherCriterion(AlertMatcherCriterionViewModel criterion)
    {
        SelectedAlertRule?.RemoveCriterion(criterion);
        SaveAlertRules();
    }

    public void SetSelectedAlertColorToStatus()
    {
        if (SelectedAlertRule is null)
        {
            StatusLine = T("alert.selectFirst");
            return;
        }

        SelectedAlertRule.UseStatusColor();
        SaveAlertRules();
    }

    public void SetSelectedAlertColorToNone()
    {
        if (SelectedAlertRule is null)
        {
            StatusLine = T("alert.selectFirst");
            return;
        }

        SelectedAlertRule.UseNoColor();
        SaveAlertRules();
    }

    public void PreviewSelectedAlertSound()
    {
        if (SelectedAlertRule is null)
        {
            StatusLine = T("alert.selectFirst");
            return;
        }

        var sound = AlertSoundCatalog.Resolve(SelectedAlertRule.SoundId);
        if (sound.Id == "none")
        {
            StatusLine = T("alert.noSoundSelected");
            return;
        }

        var path = ResolveAlertSoundAssetPath(sound.Asset);
        if (path is null)
        {
            StatusLine = TF("alert.soundMissing", sound.Asset);
            return;
        }

        if (soundPlayer.Play(path, out var error))
        {
            StatusLine = TF("alert.previewingSound", sound.Name);
        }
        else
        {
            StatusLine = TF("alert.soundPreviewFailed", sound.Name, error);
        }
    }

    public void SelectSelectedAlertSound(string soundId)
    {
        if (SelectedAlertRule is null)
        {
            StatusLine = T("alert.selectFirst");
            return;
        }

        SelectedAlertRule.SoundId = soundId;
        SelectedAlertRule.SoundSearch = string.Empty;
    }

    public void ToggleAudioMute()
    {
        IsAudioMuted = !IsAudioMuted;
        StatusLine = IsAudioMuted ? T("audio.muted") : T("audio.enabled");
    }

    public void OpenSelectedAlertSoundSource()
    {
        if (SelectedAlertRule is null)
        {
            StatusLine = T("alert.selectFirst");
            return;
        }

        var source = AlertSoundCatalog.Resolve(SelectedAlertRule.SoundId).SourceUrl;
        if (!Uri.TryCreate(source, UriKind.Absolute, out var uri))
        {
            StatusLine = T("alert.noSoundSelected");
            return;
        }

        try
        {
            Process.Start(new ProcessStartInfo(uri.ToString()) { UseShellExecute = true });
            StatusLine = TF("alert.openedSoundSource", uri.Host);
        }
        catch (InvalidOperationException)
        {
            StatusLine = TF("alert.openSoundSourceFailed", source);
        }
        catch (System.ComponentModel.Win32Exception)
        {
            StatusLine = TF("alert.openSoundSourceFailed", source);
        }
    }

    public void PreviewSelectedAlertZoom()
    {
        if (SelectedAlertRule is null)
        {
            StatusLine = T("alert.selectFirst");
            return;
        }

        var rule = SelectedAlertRule.ToRule();
        var match = AlertRuleEvaluator.EvaluateRule(cachedRows, rule)
            .Matches
            .FirstOrDefault(row => RadarBlocks.Any(block => block.Resource.Id.Equals(row.Id, StringComparison.Ordinal)));
        var target = match is null
            ? RadarBlocks.FirstOrDefault(block => block.IsClickable && !block.IsPlaceholder)?.Resource
            : match;
        if (target is null)
        {
            StatusLine = T("alert.noZoomTarget");
            return;
        }

        var block = RadarBlocks.FirstOrDefault(item => item.Resource.Id.Equals(target.Id, StringComparison.Ordinal));
        if (block is null)
        {
            StatusLine = T("alert.noZoomTarget");
            return;
        }

        var zoomPercent = Math.Max(100, rule.Actions.RadarZoomPercent);
        var screenCenterX = block.X + block.Width / 2d;
        var screenCenterY = block.Y + block.Height / 2d;
        var worldCenter = new RadarPoint(
            (screenCenterX - radarCanvasWidth / 2d) / radarZoom - radarPanX,
            (screenCenterY - radarCanvasHeight / 2d) / radarZoom - radarPanY);
        StartRadarAutoFollow(worldCenter, zoomPercent / 100d);
        StatusLine = TF("alert.previewingZoom", target.Kind, target.Name);
    }

    public void SaveAlertRules()
    {
        AlertRuleStore.Save(AlertRules.Select(rule => rule.ToRule()));
        EvaluateAlertRules();
        UpdateRadarFromCache(BuildLocalQuery());
        StatusLine = T("alert.saved");
    }

    private static string? ResolveAlertSoundAssetPath(string asset)
    {
        if (asset.Equals("none", StringComparison.OrdinalIgnoreCase))
        {
            return null;
        }

        var candidates = new[]
        {
            Path.Combine(AppContext.BaseDirectory, asset),
            Path.Combine(Directory.GetCurrentDirectory(), "src", "Podlord.App", asset),
            Path.Combine(Directory.GetCurrentDirectory(), asset)
        };
        return candidates.FirstOrDefault(File.Exists);
    }

    private bool TryPlayAutomaticSound(string soundId, bool priority = false)
    {
        if (IsAudioMuted)
        {
            priorityAlertSoundQueue.Clear();
            alertSoundQueue.Clear();
            alertSoundQueueTimer.Stop();
            return false;
        }

        if (alertSoundQueueTimer.IsEnabled || priorityAlertSoundQueue.Count > 0 || alertSoundQueue.Count > 0)
        {
            if (CanResolveAlertSound(soundId))
            {
                if (priority)
                {
                    priorityAlertSoundQueue.Enqueue(soundId);
                }
                else
                {
                    alertSoundQueue.Enqueue(soundId);
                }

                if (!alertSoundQueueTimer.IsEnabled)
                {
                    alertSoundQueueTimer.Start();
                }
                return true;
            }

            return false;
        }

        var played = TryPlayAlertSoundNow(soundId);
        if (played)
        {
            alertSoundQueueTimer.Start();
        }

        return played;
    }

    private bool CanResolveAlertSound(string soundId)
    {
        var sound = AlertSoundCatalog.Resolve(soundId);
        return !sound.Id.Equals("none", StringComparison.OrdinalIgnoreCase)
               && ResolveAlertSoundAssetPath(sound.Asset) is not null;
    }

    private bool TryPlayAlertSoundNow(string soundId)
    {
        var sound = AlertSoundCatalog.Resolve(soundId);
        if (sound.Id.Equals("none", StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        var path = ResolveAlertSoundAssetPath(sound.Asset);
        if (path is null)
        {
            return false;
        }

        try
        {
            return soundPlayer.Play(path, out _);
        }
        catch (Exception)
        {
            return false;
        }
    }

    private void PlayNextQueuedAlertSound()
    {
        if (disposed || IsAudioMuted)
        {
            priorityAlertSoundQueue.Clear();
            alertSoundQueue.Clear();
            alertSoundQueueTimer.Stop();
            return;
        }

        string soundId;
        if (priorityAlertSoundQueue.TryDequeue(out var prioritySoundId))
        {
            soundId = prioritySoundId;
        }
        else if (alertSoundQueue.TryDequeue(out var normalSoundId))
        {
            soundId = normalSoundId;
        }
        else
        {
            alertSoundQueueTimer.Stop();
            return;
        }

        TryPlayAlertSoundNow(soundId);
        if (priorityAlertSoundQueue.Count == 0 && alertSoundQueue.Count == 0)
        {
            alertSoundQueueTimer.Stop();
        }
    }

    private void UpdateSourceFilterAssignments(string oldName, string newName)
    {
        foreach (var context in state.Snapshot().ImportedContexts
                     .Where(context => context.FilterName.Equals(oldName, StringComparison.OrdinalIgnoreCase)))
        {
            try
            {
                state.SetImportedContextFilter(context.ContextId, newName);
            }
            catch (PodlordException ex)
            {
                StatusLine = ex.Message;
            }
        }
    }

    public void OpenCommandPalette()
    {
        CommandText = string.Empty;
        IsCommandPaletteOpen = true;
        UpdateCommandSuggestions();
    }

    public void CloseCommandPalette()
    {
        IsCommandPaletteOpen = false;
    }

    public void ExecuteCommandText()
    {
        var command = CommandText.Trim();
        if (command.Length == 0 && CommandSuggestions.Count > 0)
        {
            command = CommandSuggestions[0];
        }

        ExecuteCommand(command);
    }

    public void PreparePortForward(FlatResourceRow? row = null)
    {
        var target = row ?? SelectedResource;
        if (!IsPortForwardableResource(target))
        {
            StatusLine = "Port forward is available for Running pods and namespaced services.";
            return;
        }

        var forwardTarget = target ?? throw new InvalidOperationException("Port forward target vanished.");
        PortForwardResource = forwardTarget;
        SelectedPortForward = ActivePortForwardFor(forwardTarget);
        PortContainerPort = GuessPort(forwardTarget).ToString();
        PortLocalPort = FreePort(GuessPort(forwardTarget)).ToString();
        if (SelectedPortForward is not null)
        {
            PortContainerPort = SelectedPortForward.ContainerPort.ToString(CultureInfo.InvariantCulture);
            PortLocalPort = SelectedPortForward.LocalPort.ToString(CultureInfo.InvariantCulture);
            PortForwardStatusLine = $"Running: local computer 127.0.0.1:{SelectedPortForward.LocalPort} -> cluster {SelectedPortForward.Kind.ToLowerInvariant()}/{SelectedPortForward.Name}:{SelectedPortForward.ContainerPort}.";
        }
        portDeclaredPorts = [];
        portDeclaredPortsResourceId = forwardTarget.Id;
        PortDeclaredPortsLabel = "Checking declared target ports...";
        _ = LoadPortForwardPortHintsAsync(forwardTarget);
        IsPortForwardToolOpen = true;
    }

    public void PrepareSelectedResourcePortForward()
    {
        PreparePortForward(SelectedResource);
    }

    public void OpenPortForwardTask(PortForwardTaskViewModel task)
    {
        SelectedPortForward = task;
        var row = cachedRows.FirstOrDefault(candidate =>
            candidate.Kind.Equals(task.Kind, StringComparison.Ordinal)
            && candidate.Name.Equals(task.Name, StringComparison.Ordinal)
            && string.Equals(candidate.Namespace, task.Namespace, StringComparison.Ordinal));
        PortForwardResource = row ?? new FlatResourceRow(
            $"port-forward:{task.Session}:{task.Kind}:{task.Namespace}:{task.Name}",
            task.Status,
            task.Kind,
            task.Name,
            task.Namespace,
            task.Session,
            "-",
            "-",
            0,
            null,
            "-",
            null,
            "-",
            FreshnessState.Unknown);
        PortContainerPort = task.ContainerPort.ToString(CultureInfo.InvariantCulture);
        PortLocalPort = task.LocalPort.ToString(CultureInfo.InvariantCulture);
        PortForwardStatusLine = $"Running: local computer 127.0.0.1:{task.LocalPort} -> cluster {task.Kind.ToLowerInvariant()}/{task.Name}:{task.ContainerPort}.";
        PortDeclaredPortsLabel = "Opened from active port list.";
        IsPortForwardToolOpen = true;
    }

    public void RunPreparedPortForwardAction()
    {
        if (IsPortForwardStopMode)
        {
            StopSelectedPortForward();
            return;
        }

        StartPreparedPortForward();
    }

    private static bool IsPortForwardableResource(FlatResourceRow? row)
    {
        if (row is null || string.IsNullOrWhiteSpace(row.Namespace))
        {
            return false;
        }

        return row.Kind switch
        {
            "Pod" => row.Status.Equals("Running", StringComparison.OrdinalIgnoreCase),
            "Service" => true,
            _ => false
        };
    }

    private static bool IsActivePortForward(PortForwardTaskViewModel task)
    {
        return task.IsRunning
               || task.Status.Equals("starting", StringComparison.OrdinalIgnoreCase)
               || task.Status.Equals("running", StringComparison.OrdinalIgnoreCase);
    }

    private PortForwardTaskViewModel? ActivePortForwardFor(FlatResourceRow row)
    {
        return PortForwards.FirstOrDefault(candidate =>
            candidate.Kind.Equals(row.Kind, StringComparison.Ordinal)
            && candidate.Name.Equals(row.Name, StringComparison.Ordinal)
            && candidate.Namespace.Equals(row.Namespace ?? string.Empty, StringComparison.Ordinal)
            && IsActivePortForward(candidate));
    }

    public void SetAppFocus(bool focused)
    {
        if (isAppFocused == focused)
        {
            return;
        }

        isAppFocused = focused;
        if (focused)
        {
            MarkUserActivity();
            ScheduleRefresh();
        }

        UpdateRadarIdleTimer();
        UpdateRequestWorkLabel();
    }

    /// <summary>
    /// Tracks whether the window is actually on screen (not minimized). The radar screensaver keeps running while
    /// the window is merely inactive, but there is no point animating — and no point repainting — while minimized.
    /// </summary>
    public void SetWindowVisible(bool visible)
    {
        if (isWindowVisible == visible)
        {
            return;
        }

        isWindowVisible = visible;
        UpdateRadarIdleTimer();
    }

    public void ClosePortForwardTool()
    {
        IsPortForwardToolOpen = false;
    }

    private async Task LoadPortForwardPortHintsAsync(FlatResourceRow target)
    {
        try
        {
            var detail = await service.GetResourceDetailAsync(
                new ResourceIdentity(SelectedSession?.Id, target.Kind, target.Namespace, target.Name),
                false,
                KubernetesRequestPriority.Background,
                lifetime.Token).ConfigureAwait(true);
            var ports = DeclaredPortsFromYaml(target.Kind, detail.Yaml);
            if (PortForwardResource?.Id != target.Id)
            {
                return;
            }

            portDeclaredPorts = ports;
            portDeclaredPortsResourceId = target.Id;
            if (ports.Count == 0)
            {
                PortDeclaredPortsLabel = string.Empty;
                return;
            }

            PortDeclaredPortsLabel = $"Ports: {string.Join(", ", ports)}.";
            if (int.TryParse(PortContainerPort, out var current) && !ports.Contains(current))
            {
                PortContainerPort = ports[0].ToString();
                PortLocalPort = FreePort(ports[0]).ToString();
            }
        }
        catch (OperationCanceledException)
        {
        }
        catch (PodlordException ex)
        {
            if (PortForwardResource?.Id == target.Id)
            {
                PortDeclaredPortsLabel = $"Port check failed: {ex.Message}";
            }
        }
    }

    public void StartPreparedPortForward()
    {
        _ = StartPreparedPortForwardAsync();
    }

    internal async Task StartPreparedPortForwardAsync()
    {
        if (PortForwardResource is null || SelectedSession is null)
        {
            StatusLine = "Select a Pod or Service first.";
            return;
        }

        if (!IsPortForwardableResource(PortForwardResource))
        {
            StatusLine = "Port forward is available for Running pods and namespaced services.";
            PortForwardStatusLine = StatusLine;
            return;
        }

        if (!int.TryParse(PortContainerPort, out var containerPort) || containerPort <= 0)
        {
            StatusLine = "Container Port must be a positive number.";
            PortForwardStatusLine = StatusLine;
            return;
        }

        if (!int.TryParse(PortLocalPort, out var localPort) || localPort <= 0)
        {
            StatusLine = "Local Port must be a positive number.";
            PortForwardStatusLine = StatusLine;
            return;
        }

        if (!IsPortFree(localPort))
        {
            StatusLine = $"Reachable port {localPort} is already in use on this machine.";
            PortForwardStatusLine = StatusLine;
            return;
        }

        if (portDeclaredPortsResourceId == PortForwardResource.Id
            && portDeclaredPorts.Count > 0
            && !portDeclaredPorts.Contains(containerPort))
        {
            StatusLine = $"Target port {containerPort} is not declared. Known target ports: {string.Join(", ", portDeclaredPorts)}.";
            PortForwardStatusLine = StatusLine;
            return;
        }

        try
        {
            var connection = state.SessionConnection(SelectedSession.Id);
            var target = $"{PortForwardResource.Kind.ToLowerInvariant()}/{PortForwardResource.Name}";
            var command = $"native websocket port-forward {target} {localPort}:{containerPort}";
            var task = new PortForwardTaskViewModel(
                $"pf-{Guid.NewGuid():N}",
                connection.Session.DisplayName,
                PortForwardResource.Kind,
                PortForwardResource.Name,
                PortForwardResource.Namespace!,
                containerPort,
                localPort,
                command,
                "starting");
            PortForwards.Add(task);
            SelectedPortForward = task;
            RefreshPortForwardBadges();
            PortForwardStatusLine = $"Starting: local computer 127.0.0.1:{localPort} -> cluster {target}:{containerPort}.";
            var forwarder = await service.StartPortForwardAsync(
                new PortForwardRequest(
                    SelectedSession.Id,
                    PortForwardResource.Kind,
                    PortForwardResource.Namespace!,
                    PortForwardResource.Name,
                    localPort,
                    containerPort)).ConfigureAwait(true);
            forwarder.StatusChanged += (_, args) => Dispatcher.UIThread.Post(() => UpdateNativePortForwardStatus(task, args.Status));
            task.Forwarder = forwarder;
            task.Status = "running";
            PortForwardStatusLine = $"Running: local computer 127.0.0.1:{localPort} -> cluster {target}:{containerPort}.";
            StatusLine = PortForwardStatusLine;
            RefreshPortForwardBadges();
        }
        catch (Exception ex) when (ex is InvalidOperationException or PodlordException or IOException or SocketException)
        {
            StatusLine = ex.Message;
            PortForwardStatusLine = ex.Message;
        }
    }

    private void UpdateNativePortForwardStatus(PortForwardTaskViewModel task, string status)
    {
        if (status.StartsWith("error:", StringComparison.OrdinalIgnoreCase))
        {
            task.Status = "error";
            PortForwardStatusLine = status;
            StatusLine = status;
        }
        else if (status.Equals("stopped", StringComparison.OrdinalIgnoreCase))
        {
            task.Status = "stopped";
            PortForwardStatusLine = $"Stopped local computer 127.0.0.1:{task.LocalPort} -> cluster {task.Kind.ToLowerInvariant()}/{task.Name}:{task.ContainerPort}.";
            StatusLine = PortForwardStatusLine;
        }
        else if (status.Equals("running", StringComparison.OrdinalIgnoreCase))
        {
            task.Status = "running";
        }

        RefreshPortForwardBadges();
    }

    public void StopSelectedPortForward()
    {
        var task = SelectedPortForward;
        if (task is null)
        {
            StatusLine = "Select a port forward first.";
            return;
        }

        try
        {
            task.Stop();
            task.Status = "stopped";
            PortForwardStatusLine = $"Stopped local computer 127.0.0.1:{task.LocalPort} -> cluster {task.Kind.ToLowerInvariant()}/{task.Name}:{task.ContainerPort}.";
            StatusLine = $"Stopped port forward on local computer port {task.LocalPort}.";
            RefreshPortForwardBadges();
            OnPropertyChanged(nameof(IsPortForwardStopMode));
            OnPropertyChanged(nameof(PortForwardActionLabel));
        }
        catch (InvalidOperationException ex)
        {
            StatusLine = ex.Message;
        }
    }

    public async Task RefreshResourcesAsync(bool force = false)
    {
        await RefreshResourcesAsync(force, false).ConfigureAwait(true);
    }

    private async Task RefreshResourcesAsync(bool force, bool background)
    {
        if (refreshInFlight)
        {
            refreshAgainRequested = true;
            if (!background)
            {
                IsRefreshing = true;
                StatusLine = SelectedSession is null
                    ? T("source.waitingQueue")
                    : TF("source.waitingRefresh", SelectedSession.DisplayName);
            }

            return;
        }

        var sessionId = SelectedSession?.Id;
        var showRefreshing = !background || cachedRows.Count == 0;
        try
        {
            refreshInFlight = true;
            if (showRefreshing)
            {
                IsRefreshing = true;
            }

            if (SelectedSession is not null)
            {
                state.SwitchActiveSession(SelectedSession.Id);
            }

            var displayQuery = BuildDisplayCacheQuery();
            var warmQuery = background ? BuildBackgroundWarmQuery(force) : BuildRemoteQuery(force);
            var cachedBeforeWarm = service.GetCachedResourceSnapshot(displayQuery, applyFilters: false);
            if (Resources.Count == 0 && cachedBeforeWarm.Rows.Count > 0)
            {
                RenderSnapshot(cachedBeforeWarm);
            }

            initialLoadStartedAt = DateTimeOffset.UtcNow;
            initialLoadExpectedTotal = Math.Max(1, service.EstimateListRequestCount(warmQuery));
            var priority = background ? KubernetesRequestPriority.Background : KubernetesRequestPriority.UserVisible;
            var warm = await service.WarmResourceCacheAsync(warmQuery, priority).ConfigureAwait(true);
            if (!string.Equals(SelectedSession?.Id, sessionId, StringComparison.Ordinal))
            {
                refreshAgainRequested = true;
                return;
            }

            var cached = service.GetCachedResourceSnapshot(displayQuery, applyFilters: false);
            var rendered = RenderSnapshot(cached with { Failures = warm.Failures, Freshness = warm.Freshness });

            lastSyncedAt = DateTimeOffset.Now;
            if (sessionId is { Length: > 0 })
            {
                sessionSyncedAt[sessionId] = lastSyncedAt.Value;
            }

            OnPropertyChanged(nameof(LastSyncedLabel));
            OnPropertyChanged(nameof(FooterLine));
            if (!background || rendered)
            {
                StatusLine = $"{ResourceCountLabel}; {Failures.Count} warning(s); {LastSyncedLabel}.";
            }
        }
        catch (PodlordException ex)
        {
            StatusLine = ex.Message;
        }
        finally
        {
            refreshInFlight = false;
            if (showRefreshing)
            {
                IsRefreshing = false;
            }

            UpdateRequestWorkLabel();
            if (refreshAgainRequested && !lifetime.IsCancellationRequested)
            {
                refreshAgainRequested = false;
                ScheduleRefresh();
            }
        }
    }

    public async Task OpenSelectedResourceAsync()
    {
        var focusedResource = SelectedResource;
        if (focusedResource is null)
        {
            return;
        }

        var focusLoadSource = BeginFocusLoad();
        var cancellationToken = focusLoadSource.Token;
        try
        {
            IsInspectorVisible = true;
            IsDetailLoading = true;
            var identity = new ResourceIdentity(
                SelectedSession?.Id,
                focusedResource.Kind,
                focusedResource.Namespace,
                focusedResource.Name);
            var cached = service.GetCachedResourceDetail(identity);
            if (cached is not null)
            {
                RenderDetail(cached);
            }
            else
            {
                RenderCachedResourceSummary(focusedResource);
            }

            var hasUserEdits = isYamlLoaded
                && IsInspectorYamlActive
                && !string.Equals(EditableYaml, DetailYaml, StringComparison.Ordinal);
            if (cached is not null && hasUserEdits)
            {
                StatusLine = "YAML tab has unsaved edits; fresh detail refresh paused.";
                return;
            }

            var detail = await service.GetResourceDetailAsync(identity, true, KubernetesRequestPriority.Foreground, cancellationToken).ConfigureAwait(true);
            if (cancellationToken.IsCancellationRequested || SelectedResource?.Id != focusedResource.Id)
            {
                return;
            }

            RenderDetail(detail);
            StatusLine = $"Focused {focusedResource.Kind}/{focusedResource.Name}; {LastSyncedLabel}.";
            if (!IsSelectedResourceLoggable)
            {
                LogText = "Logs are available for pods.";
            }

            UpdateInspectorTabWork();
        }
        catch (OperationCanceledException)
        {
        }
        catch (PodlordException ex)
        {
            StatusLine = ex.Message;
        }
        finally
        {
            if (ReferenceEquals(focusLoad, focusLoadSource))
            {
                focusLoad?.Dispose();
                focusLoad = null;
                IsDetailLoading = false;
            }

            UpdateRequestWorkLabel();
        }
    }

    public async Task FocusResourceAsync(FlatResourceRow row)
    {
        FocusResourceFromSurface(row, SelectionSurface.Resource);
        focusDebounce?.Cancel();
        await OpenSelectedResourceAsync().ConfigureAwait(true);
    }

    public bool HasKnownResourceReference(string value)
    {
        return ResolveKnownResourceReference(value) is not null;
    }

    public FlatResourceRow? ResolveResourceReferenceForPreview(string value)
    {
        return ResolveKnownResourceReference(value);
    }

    public bool OpenKnownResourceReference(string value)
    {
        var row = ResolveKnownResourceReference(value);
        if (row is null)
        {
            StatusLine = $"No cached resource matches '{value}'.";
            return false;
        }

        FocusResourceFromSurface(row, SelectionSurface.Resource);
        return true;
    }

    public async Task FocusRadarResourceAsync(FlatResourceRow row)
    {
        var loadFresh = !IsVirtualRadarResource(row);
        FocusResourceFromSurface(row, SelectionSurface.Radar, loadFresh);
        focusDebounce?.Cancel();
        if (!loadFresh)
        {
            StatusLine = $"Focused radar {row.Kind}/{row.Name} from cache.";
            return;
        }

        await OpenSelectedResourceAsync().ConfigureAwait(true);
    }

    public void SetRadarPanelWidth(double width)
    {
        if (double.IsNaN(width) || width <= 0)
        {
            return;
        }

        RadarPanelHeight = Math.Clamp(width * 0.47, 156, 340);
    }

    public void SetRadarViewport(double width, double height)
    {
        if (double.IsNaN(width) || double.IsNaN(height) || width < 80 || height < 80)
        {
            return;
        }

        var nextWidth = Math.Round(width);
        var nextHeight = Math.Round(height);
        if (Math.Abs(nextWidth - radarCanvasWidth) < 1 && Math.Abs(nextHeight - radarCanvasHeight) < 1)
        {
            return;
        }

        RadarCanvasW = nextWidth;
        RadarCanvasH = nextHeight;
        UpdateRadarFromCache();
    }

    public void ZoomRadar(double wheelDelta)
    {
        if (wheelDelta == 0)
        {
            return;
        }

        ZoomRadarByFactor(wheelDelta > 0 ? 1.18 : 1 / 1.18);
    }

    public void ZoomRadarIn()
    {
        ZoomRadarByFactor(1.18);
    }

    public void ZoomRadarOut()
    {
        ZoomRadarByFactor(1 / 1.18);
    }

    public void ResetRadarView()
    {
        if (Math.Abs(radarZoom - 1d) < 0.001
            && Math.Abs(radarPanX) < 0.001
            && Math.Abs(radarPanY) < 0.001)
        {
            return;
        }

        radarZoom = 1d;
        radarPanX = 0;
        radarPanY = 0;
        OnPropertyChanged(nameof(RadarZoom));
        OnPropertyChanged(nameof(RadarZoomLabel));
        OnPropertyChanged(nameof(RadarPanX));
        OnPropertyChanged(nameof(RadarPanY));
        UpdateRadarFromCache();
        PauseRadarWaterDuringInteraction();
        MarkUserActivity();
    }

    private void ZoomRadarByFactor(double factor)
    {
        var next = Math.Clamp(radarZoom * factor, 0.55, 3.4);
        if (Math.Abs(next - radarZoom) < 0.001)
        {
            return;
        }

        radarZoom = next;
        OnPropertyChanged(nameof(RadarZoom));
        OnPropertyChanged(nameof(RadarZoomLabel));
        UpdateRadarFromCache();
        PauseRadarWaterDuringInteraction();
        MarkUserActivity();
    }

    public void PanRadar(double screenDeltaX, double screenDeltaY)
    {
        if (Math.Abs(screenDeltaX) < 0.1 && Math.Abs(screenDeltaY) < 0.1)
        {
            return;
        }

        radarPanX += screenDeltaX / radarZoom;
        radarPanY += screenDeltaY / radarZoom;
        OnPropertyChanged(nameof(RadarPanX));
        OnPropertyChanged(nameof(RadarPanY));
        UpdateRadarFromCache();
        PauseRadarWaterDuringInteraction();
        MarkUserActivity();
    }

    private void PauseRadarWaterDuringInteraction()
    {
        if (!state.Settings().RadarWaterEnabled)
        {
            return;
        }

        IsRadarWaterPaused = true;
        radarWaterPauseTimer.Stop();
        radarWaterPauseTimer.Start();
    }

    public async Task ApplyEditedYamlAsync()
    {
        if (selectedSource is not null)
        {
            ApplySelectedSourceYaml();
            return;
        }

        if (SelectedResource is null)
        {
            YamlApplyStatus = "Select a resource before applying YAML.";
            return;
        }

        if (string.IsNullOrWhiteSpace(EditableYaml))
        {
            YamlApplyStatus = "YAML is empty. Apply refused.";
            return;
        }

        if (!IsYamlSyntaxValid(EditableYaml, out var yamlError))
        {
            YamlApplyStatus = $"YAML syntax error: {yamlError}";
            StatusLine = YamlApplyStatus;
            return;
        }

        if (!isYamlLoaded)
        {
            YamlApplyStatus = "Fresh YAML has not loaded yet. Apply refused.";
            StatusLine = YamlApplyStatus;
            return;
        }

        try
        {
            IsDetailLoading = true;
            YamlApplyStatus = "Applying YAML through Kubernetes server-side apply...";
            var identity = new ResourceIdentity(
                SelectedSession?.Id,
                SelectedResource.Kind,
                SelectedResource.Namespace,
                SelectedResource.Name);
            var detail = await service.ApplyResourceYamlAsync(identity, EditableYaml, lifetime.Token).ConfigureAwait(true);
            RenderDetail(detail, forceYamlRefresh: true);
            YamlApplyStatus = $"Applied {identity.Kind}/{identity.Name}; cache refreshed.";
            StatusLine = YamlApplyStatus;
            ScheduleRefresh();
        }
        catch (OperationCanceledException)
        {
            YamlApplyStatus = "YAML apply cancelled.";
        }
        catch (PodlordException ex)
        {
            YamlApplyStatus = ex.Message;
            StatusLine = ex.Message;
        }
        finally
        {
            IsDetailLoading = false;
            UpdateRequestWorkLabel();
        }
    }

    public async Task DeleteSelectedResourceAsync()
    {
        if (!CanDeleteSelectedResource || SelectedResource is null)
        {
            StatusLine = "Select a Kubernetes resource that can be deleted.";
            return;
        }

        var resource = SelectedResource;
        if (!string.Equals(deleteConfirmationResourceId, resource.Id, StringComparison.Ordinal))
        {
            deleteConfirmationResourceId = resource.Id;
            OnPropertyChanged(nameof(DeleteActionLabel));
            StatusLine = $"Press delete again to delete {resource.Kind}/{resource.Name}.";
            return;
        }

        try
        {
            IsDetailLoading = true;
            var identity = new ResourceIdentity(SelectedSession?.Id, resource.Kind, resource.Namespace, resource.Name);
            await service.DeleteResourceAsync(identity, lifetime.Token).ConfigureAwait(true);
            cachedRows.RemoveAll(row => row.Id.Equals(resource.Id, StringComparison.Ordinal));
            ApplyLocalFilter();
            CloseInspector();
            StatusLine = $"Deleted {resource.Kind}/{resource.Name}.";
            ScheduleRefresh();
        }
        catch (OperationCanceledException)
        {
            StatusLine = "Delete cancelled.";
        }
        catch (PodlordException ex)
        {
            ResetDeleteConfirmation();
            StatusLine = ex.Message;
        }
        finally
        {
            IsDetailLoading = false;
            UpdateRequestWorkLabel();
        }
    }

    private void ApplySelectedSourceYaml()
    {
        if (selectedSource is null)
        {
            YamlApplyStatus = "Select a source before applying YAML.";
            return;
        }

        if (string.IsNullOrWhiteSpace(EditableYaml))
        {
            YamlApplyStatus = "Source YAML is empty. Apply refused.";
            return;
        }

        if (!IsYamlSyntaxValid(EditableYaml, out var yamlError))
        {
            YamlApplyStatus = $"YAML syntax error: {yamlError}";
            StatusLine = YamlApplyStatus;
            return;
        }

        try
        {
            IsDetailLoading = true;
            var oldContextId = selectedSource.ContextId;
            var summary = state.ImportKubeconfigText(selectedSource.Context, EditableYaml);
            if (summary.Contexts.All(context => context.ContextId != oldContextId))
            {
                state.RemoveImportedContext(oldContextId);
            }

            var nextContextId = summary.Contexts.FirstOrDefault()?.ContextId;
            var message = $"Imported edited source YAML with {summary.Contexts.Count} context(s); original path was not modified.";
            ReloadSessions();
            if (nextContextId is not null)
            {
                SelectSourceByContextId(nextContextId);
            }

            StatusLine = message;
            YamlApplyStatus = message;
        }
        catch (PodlordException ex)
        {
            YamlApplyStatus = ex.Message;
            StatusLine = ex.Message;
        }
        finally
        {
            IsDetailLoading = false;
        }
    }

    public void ResetEditedYaml()
    {
        EditableYaml = DetailYaml;
        YamlApplyStatus = "YAML editor reset to last loaded data.";
    }

    private void FocusSource(SourceStatusRow source)
    {
        CancelFocusLoad();
        StopLogTail();
        selectedSource = source;
        selectingResource = true;
        try
        {
            selectedResource = new FlatResourceRow(
                $"source:{source.ContextId}",
                source.Status,
                "Source",
                source.Context,
                null,
                source.Cluster,
                source.ImportedAt,
                "-",
                0,
                null,
                source.AuthType,
                source.User,
                source.ImportedAt,
                source.Status.Equals("error", StringComparison.OrdinalIgnoreCase) ? FreshnessState.Stale : FreshnessState.Fresh);
            selectedResourceRow = null;
            selectedRadarResourceId = null;
            SelectedGraphNode = null;
            IsInspectorVisible = true;
            IsDetailLoading = false;
            selectedInspectorTabIndex = 0;
            RenderSourceSummary(source);
            NotifyInspectorTargetChanged();
            UpdateRadarSelection();
            StatusLine = $"Focused source {source.Context}.";
        }
        finally
        {
            selectingResource = false;
        }
    }

    private void FocusEvent(EventTimelineRow eventRow)
    {
        var row = cachedRows.FirstOrDefault(candidate => candidate.Id == eventRow.ResourceId)
                  ?? cachedRows.FirstOrDefault(candidate =>
                      candidate.Kind == "Event"
                      && candidate.Name == eventRow.Name
                      && string.Equals(candidate.Namespace ?? "cluster", eventRow.Namespace, StringComparison.Ordinal));
        if (row is null)
        {
            StatusLine = $"No cached event resource matches {eventRow.Name}.";
            return;
        }

        FocusResourceFromSurface(row, SelectionSurface.Resource);
        StatusLine = $"Focused event {eventRow.Name}: {eventRow.Reason}.";
    }

    public void CloseInspector()
    {
        CancelFocusLoad();
        focusDebounce?.Cancel();
        IsInspectorVisible = false;
        IsDetailLoading = false;
        isYamlLoaded = false;
        selectedInspectorTabIndex = 0;
        selectedResource = null;
        selectedResourceRow = null;
        selectedSource = null;
        selectedRadarResourceId = null;
        SelectedGraphNode = null;
        ResetPodLogContainers();
        ResetDeleteConfirmation();
        SyncCollection(ResourceValues, Array.Empty<ResourceValueRow>());
        NotifyInspectorTargetChanged();
        UpdateRadarSelection();
        StopLogTail();
    }

    private void FocusResourceFromSurface(FlatResourceRow row, SelectionSurface surface, bool loadFresh = true)
    {
        var resourceChanged = selectedResource?.Id != row.Id;
        selectingResource = true;
        try
        {
            selectedSource = null;
            selectedResource = row;
            selectedResourceRow = surface == SelectionSurface.Table ? row : null;
            selectedRadarResourceId = row.Id;
            ResetDeleteConfirmation();
            if (surface != SelectionSurface.Graph)
            {
                SelectedGraphNode = null;
            }

            NotifyInspectorTargetChanged();
            MarkUserActivity();
            IsInspectorVisible = true;
            IsDetailLoading = loadFresh;
            ResourceDetail? cachedDetail = null;
            if (SelectedSession is not null)
            {
                try
                {
                    var cachedIdentity = new ResourceIdentity(SelectedSession.Id, row.Kind, row.Namespace, row.Name);
                    cachedDetail = service.GetCachedResourceDetail(cachedIdentity);
                }
                catch (PodlordException)
                {
                }
            }
            if (cachedDetail is not null)
            {
                RenderDetail(cachedDetail);
            }
            else
            {
                RenderCachedResourceSummary(row);
            }
            UpdateInspectorTabWork();
            UpdateRadarSelection();
            if (loadFresh)
            {
                ScheduleFocusLoad();
            }
            else
            {
                CancelFocusLoad();
                YamlApplyStatus = "Radar grouping node selected from cache; no Kubernetes YAML apply target.";
            }
            if (!suppressInspectorHistory)
            {
                PushInspectorHistory(row.Id);
            }
            if (resourceChanged && IsInspectorYamlActive && loadFresh)
            {
                _ = LoadFreshYamlAsync();
            }
        }
        finally
        {
            selectingResource = false;
        }
    }

    internal IReadOnlyList<string> InspectorHistoryIdsForTesting => inspectorHistoryIds;

    internal int InspectorHistoryCursorForTesting => inspectorHistoryCursor;

    internal void PushInspectorHistoryForTesting(string id) => PushInspectorHistory(id);

    private void PushInspectorHistory(string id)
    {
        if (string.IsNullOrEmpty(id))
        {
            return;
        }
        if (inspectorHistoryCursor >= 0
            && inspectorHistoryCursor < inspectorHistoryIds.Count
            && string.Equals(inspectorHistoryIds[inspectorHistoryCursor], id, StringComparison.Ordinal))
        {
            return;
        }
        if (inspectorHistoryCursor < 0 || inspectorHistoryCursor >= inspectorHistoryIds.Count - 1)
        {
            inspectorHistoryIds.Add(id);
            inspectorHistoryCursor = inspectorHistoryIds.Count - 1;
        }
        else
        {
            var insertAt = inspectorHistoryCursor + 1;
            inspectorHistoryIds.Insert(insertAt, id);
            inspectorHistoryCursor = insertAt;
        }
        while (inspectorHistoryIds.Count > InspectorHistoryMax)
        {
            inspectorHistoryIds.RemoveAt(0);
            if (inspectorHistoryCursor > 0)
            {
                inspectorHistoryCursor--;
            }
        }
        NotifyInspectorHistoryChanged();
    }

    private void NotifyInspectorHistoryChanged()
    {
        OnPropertyChanged(nameof(CanGoBackInspector));
        OnPropertyChanged(nameof(CanGoForwardInspector));
    }

    public bool CanGoBackInspector => FindPriorReachable(-1) >= 0;

    public bool CanGoForwardInspector => FindPriorReachable(+1) >= 0;

    private int FindPriorReachable(int step)
    {
        for (var i = inspectorHistoryCursor + step; i >= 0 && i < inspectorHistoryIds.Count; i += step)
        {
            if (ResolveCachedRowById(inspectorHistoryIds[i]) is not null)
            {
                return i;
            }
        }
        return -1;
    }

    private FlatResourceRow? ResolveCachedRowById(string id)
    {
        return cachedRows.FirstOrDefault(row => string.Equals(row.Id, id, StringComparison.Ordinal));
    }

    public async Task GoBackInspectorAsync()
    {
        await StepInspectorHistoryAsync(-1).ConfigureAwait(true);
    }

    public async Task GoForwardInspectorAsync()
    {
        await StepInspectorHistoryAsync(+1).ConfigureAwait(true);
    }

    private Task StepInspectorHistoryAsync(int step)
    {
        var target = FindPriorReachable(step);
        if (target < 0)
        {
            return Task.CompletedTask;
        }
        var row = ResolveCachedRowById(inspectorHistoryIds[target]);
        if (row is null)
        {
            return Task.CompletedTask;
        }
        inspectorHistoryCursor = target;
        suppressInspectorHistory = true;
        try
        {
            FocusResourceFromSurface(row, SelectionSurface.Resource);
            focusDebounce?.Cancel();
            _ = OpenSelectedResourceAsync();
        }
        finally
        {
            suppressInspectorHistory = false;
            NotifyInspectorHistoryChanged();
        }
        return Task.CompletedTask;
    }

    private void NotifyInspectorTargetChanged()
    {
        OnPropertyChanged(nameof(SelectedSource));
        OnPropertyChanged(nameof(SelectedResource));
        OnPropertyChanged(nameof(SelectedResourceRow));
        OnPropertyChanged(nameof(SelectedResourceLabel));
        OnPropertyChanged(nameof(IsSelectedSource));
        OnPropertyChanged(nameof(IsSelectedKubernetesResource));
        OnPropertyChanged(nameof(IsSelectedResourceLoggable));
        OnPropertyChanged(nameof(IsSelectedResourceKeyValueResource));
        NotifyInspectorTabStateChanged();
        OnPropertyChanged(nameof(CanPortForwardSelectedResource));
        OnPropertyChanged(nameof(CanDeleteSelectedResource));
        OnPropertyChanged(nameof(DeleteActionLabel));
        OnPropertyChanged(nameof(SelectedInspectorTabIndex));
    }

    private void NotifyInspectorTabStateChanged()
    {
        OnPropertyChanged(nameof(IsInspectorOverviewActive));
        OnPropertyChanged(nameof(IsInspectorYamlActive));
        OnPropertyChanged(nameof(IsInspectorEventsActive));
        OnPropertyChanged(nameof(IsInspectorLinksActive));
        OnPropertyChanged(nameof(IsInspectorLogsActive));
        OnPropertyChanged(nameof(IsInspectorValuesActive));
    }

    private void ResetDeleteConfirmation()
    {
        if (deleteConfirmationResourceId is null)
        {
            return;
        }

        deleteConfirmationResourceId = null;
        OnPropertyChanged(nameof(DeleteActionLabel));
    }

    private void SelectSourceByContextId(string contextId)
    {
        var source = Sources.FirstOrDefault(candidate => candidate.ContextId == contextId);
        if (source is null)
        {
            return;
        }

        selectedSource = null;
        SelectedSource = source;
    }

    private static bool IsVirtualRadarResource(FlatResourceRow row)
    {
        return row.Id.StartsWith("radar:", StringComparison.Ordinal);
    }

    private FlatResourceRow? ResolveKnownResourceReference(string value)
    {
        var candidates = ReferenceCandidates(value);
        if (candidates.Count == 0)
        {
            return null;
        }

        var currentNamespace = SelectedResource?.Namespace;
        var currentKind = SelectedResource?.Kind;
        return cachedRows
            .Where(row => candidates.Any(candidate => MatchesResourceReference(row, candidate)))
            .OrderBy(row => string.Equals(row.Namespace, currentNamespace, StringComparison.Ordinal) ? 0 : 1)
            .ThenBy(row => string.Equals(row.Kind, currentKind, StringComparison.OrdinalIgnoreCase) ? 0 : 1)
            .ThenBy(row => row.Kind, StringComparer.OrdinalIgnoreCase)
            .ThenBy(row => row.Name, StringComparer.Ordinal)
            .FirstOrDefault();
    }

    private static IReadOnlyList<string> ReferenceCandidates(string value)
    {
        var trimmed = CleanReferenceToken(value);
        if (trimmed.Length == 0)
        {
            return [];
        }

        var candidates = new List<string> { trimmed };
        foreach (var token in trimmed.Split([' ', '\t', '\r', '\n'], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            var clean = CleanReferenceToken(token);
            if (clean.Length > 0)
            {
                candidates.Add(clean);
            }
        }

        return candidates.Distinct(StringComparer.Ordinal).ToList();
    }

    private static string CleanReferenceToken(string value)
    {
        return value
            .Trim()
            .Trim('"', '\'', '`')
            .Trim('.', ',', ';', ':', '(', ')', '[', ']', '{', '}', '<', '>');
    }

    private static bool MatchesResourceReference(FlatResourceRow row, string candidate)
    {
        return string.Equals(row.Id, candidate, StringComparison.Ordinal)
            || string.Equals(row.Name, candidate, StringComparison.Ordinal)
            || string.Equals($"{row.Kind}/{row.Name}", candidate, StringComparison.OrdinalIgnoreCase)
            || string.Equals($"{row.Kind}/{row.Namespace ?? "cluster"}/{row.Name}", candidate, StringComparison.OrdinalIgnoreCase)
            || MatchesSlashReference(row, candidate);
    }

    private static bool MatchesSlashReference(FlatResourceRow row, string candidate)
    {
        var parts = candidate.Split('/', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        return parts.Length switch
        {
            2 => (KindMatches(parts[0], row.Kind) || string.Equals(parts[0], row.Namespace, StringComparison.Ordinal))
                && string.Equals(parts[1], row.Name, StringComparison.Ordinal),
            3 => KindMatches(parts[0], row.Kind)
                && string.Equals(parts[1], row.Namespace ?? "cluster", StringComparison.Ordinal)
                && string.Equals(parts[2], row.Name, StringComparison.Ordinal),
            _ => false
        };
    }

    private static bool KindMatches(string referenceKind, string rowKind)
    {
        return string.Equals(NormalizeKind(referenceKind), NormalizeKind(rowKind), StringComparison.OrdinalIgnoreCase);
    }

    private static string NormalizeKind(string kind)
    {
        var lower = kind.Trim().Split('.', 2)[0].ToLowerInvariant();
        return lower switch
        {
            "pods" => "pod",
            "deployments" => "deployment",
            "replicasets" => "replicaset",
            "statefulsets" => "statefulset",
            "daemonsets" => "daemonset",
            "jobs" => "job",
            "cronjobs" => "cronjob",
            "services" => "service",
            "ingresses" => "ingress",
            "namespaces" => "namespace",
            "nodes" => "node",
            "configmaps" => "configmap",
            "secrets" => "secret",
            "persistentvolumes" => "persistentvolume",
            "persistentvolumeclaims" => "persistentvolumeclaim",
            "serviceaccounts" => "serviceaccount",
            "events" => "event",
            _ => lower
        };
    }

    public async Task FocusRelationshipEndpointAsync(RelationshipRow relationship, bool focusTarget)
    {
        var kind = focusTarget ? relationship.ToKind : relationship.FromKind;
        var name = focusTarget ? relationship.ToName : relationship.FromName;
        var ns = relationship.Namespace == "cluster" ? null : relationship.Namespace;
        var row = cachedRows.FirstOrDefault(candidate =>
            candidate.Kind.Equals(kind, StringComparison.OrdinalIgnoreCase)
            && candidate.Name.Equals(name, StringComparison.Ordinal)
            && string.Equals(candidate.Namespace, ns, StringComparison.Ordinal));
        if (row is null)
        {
            StatusLine = $"{kind}/{name} is not a cached Kubernetes resource.";
            return;
        }

        await FocusResourceAsync(row).ConfigureAwait(true);
    }

    public async Task FocusGraphNodeAsync(GraphNodeViewModel node)
    {
        SelectedGraphNode = node;
        if (node.Resource is null)
        {
            selectedResource = null;
            selectedResourceRow = null;
            selectedRadarResourceId = null;
            NotifyInspectorTargetChanged();
            UpdateRadarSelection();
            StatusLine = $"{node.Kind}/{node.Name} is a graph grouping node.";
            return;
        }

        FocusResourceFromSurface(node.Resource, SelectionSurface.Graph);
        focusDebounce?.Cancel();
        await OpenSelectedResourceAsync().ConfigureAwait(true);
    }

    public void NextGraphMatch()
    {
        if (graphSearchMatches.Count == 0)
        {
            StatusLine = "No graph search matches.";
            return;
        }

        graphSearchIndex = (graphSearchIndex + 1) % graphSearchMatches.Count;
        SelectCurrentGraphMatch();
    }

    public void NextResourceMatch()
    {
        if (resourceSearchMatches.Count == 0)
        {
            StatusLine = "No resource search matches.";
            return;
        }

        resourceSearchIndex = (resourceSearchIndex + 1) % resourceSearchMatches.Count;
        SelectCurrentResourceMatch();
    }

    public void PreviousResourceMatch()
    {
        if (resourceSearchMatches.Count == 0)
        {
            StatusLine = "No resource search matches.";
            return;
        }

        resourceSearchIndex = resourceSearchIndex <= 0 ? resourceSearchMatches.Count - 1 : resourceSearchIndex - 1;
        SelectCurrentResourceMatch();
    }

    public void PreviousGraphMatch()
    {
        if (graphSearchMatches.Count == 0)
        {
            StatusLine = "No graph search matches.";
            return;
        }

        graphSearchIndex = graphSearchIndex <= 0 ? graphSearchMatches.Count - 1 : graphSearchIndex - 1;
        SelectCurrentGraphMatch();
    }

    public void NextEventMatch()
    {
        if (eventSearchMatches.Count == 0)
        {
            StatusLine = "No event search matches.";
            return;
        }

        eventSearchIndex = (eventSearchIndex + 1) % eventSearchMatches.Count;
        SelectCurrentEventMatch();
    }

    public void PreviousEventMatch()
    {
        if (eventSearchMatches.Count == 0)
        {
            StatusLine = "No event search matches.";
            return;
        }

        eventSearchIndex = eventSearchIndex <= 0 ? eventSearchMatches.Count - 1 : eventSearchIndex - 1;
        SelectCurrentEventMatch();
    }

    public void OpenSearchForCurrentWorkspace()
    {
        if (IsResourcesWorkspace)
        {
            IsResourceSearchOpen = true;
        }
        else if (IsGraphWorkspace)
        {
            IsGraphSearchOpen = true;
        }
        else if (IsEventsWorkspace)
        {
            IsEventSearchOpen = true;
        }
        else if (IsPortsWorkspace)
        {
            IsPortSearchOpen = true;
        }
    }

    public void TogglePortSearch()
    {
        IsPortSearchOpen = !IsPortSearchOpen;
    }

    public void ToggleResourceSearch()
    {
        IsResourceSearchOpen = !IsResourceSearchOpen;
        UpdateResourceSearchMatches(resetToFirstMatch: true);
    }

    public void ToggleGraphSearch()
    {
        IsGraphSearchOpen = !IsGraphSearchOpen;
        UpdateGraphSearchMatches(resetToFirstMatch: true);
    }

    public void ToggleEventSearch()
    {
        IsEventSearchOpen = !IsEventSearchOpen;
        UpdateEventSearchMatches(resetToFirstMatch: true);
    }

    public void ToggleSearchForCurrentWorkspace()
    {
        if (IsResourcesWorkspace)
        {
            ToggleResourceSearch();
        }
        else if (IsGraphWorkspace)
        {
            ToggleGraphSearch();
        }
        else if (IsPortsWorkspace)
        {
            TogglePortSearch();
        }
        else if (IsEventsWorkspace)
        {
            ToggleEventSearch();
        }
    }

    public void CloseSearchForCurrentWorkspace()
    {
        if (IsResourcesWorkspace)
        {
            IsResourceSearchOpen = false;
            ResourceQuickSearch = string.Empty;
            Search = string.Empty;
        }
        else if (IsGraphWorkspace)
        {
            IsGraphSearchOpen = false;
            GraphSearch = string.Empty;
        }
        else if (IsEventsWorkspace)
        {
            IsEventSearchOpen = false;
            EventQuickSearch = string.Empty;
        }
        else if (IsPortsWorkspace)
        {
            IsPortSearchOpen = false;
            PortQuickSearch = string.Empty;
        }
    }

    public void SortResourcesBy(string column)
    {
        (resourceSortColumn, resourceSortDirection) = AdvanceSortState(resourceSortColumn, resourceSortDirection, column);
        OnPropertyChanged(nameof(ResourceSortLabel));
        ApplyLocalFilter();
    }

    public void SortEventsBy(string column)
    {
        (eventSortColumn, eventSortDirection) = AdvanceSortState(eventSortColumn, eventSortDirection, column);
        OnPropertyChanged(nameof(EventSortLabel));
        ApplyLocalFilter();
    }

    private static (string Column, ResourceSortDirection Direction) AdvanceSortState(string currentColumn, ResourceSortDirection currentDirection, string requestedColumn)
    {
        if (!currentColumn.Equals(requestedColumn, StringComparison.Ordinal))
        {
            return (requestedColumn, ResourceSortDirection.Descending);
        }
        var next = currentDirection switch
        {
            ResourceSortDirection.None => ResourceSortDirection.Descending,
            ResourceSortDirection.Descending => ResourceSortDirection.Ascending,
            _ => ResourceSortDirection.None
        };
        return (currentColumn, next);
    }

    private bool disposed;

    public void Dispose()
    {
        if (disposed)
        {
            return;
        }
        disposed = true;
        refreshDebounce?.Cancel();
        filterDebounce?.Cancel();
        focusDebounce?.Cancel();
        focusLoad?.Cancel();
        logTail?.Cancel();
        sourceRefreshDebounce?.Cancel();
        radarIdleTimer.Stop();
        radarWaterPauseTimer.Stop();
        radarAutoFollowTimer.Stop();
        alertSoundQueueTimer.Stop();
        alertAnimationExpiryTimer.Stop();
        footerTimer.Stop();
        radarAutoFollowQueue.Clear();
        priorityAlertSoundQueue.Clear();
        alertSoundQueue.Clear();
        lifetime.Cancel();
        refreshDebounce?.Dispose();
        filterDebounce?.Dispose();
        focusDebounce?.Dispose();
        focusLoad?.Dispose();
        logTail?.Dispose();
        sourceRefreshDebounce?.Dispose();
        refreshDebounce = null;
        filterDebounce = null;
        focusDebounce = null;
        focusLoad = null;
        logTail = null;
        sourceRefreshDebounce = null;
        DisposeSourceWatchers();
        lifetime.Dispose();
        soundPlayer.Dispose();
    }

    private void RefreshTimeLabels()
    {
        OnPropertyChanged(nameof(LastSyncedLabel));
        OnPropertyChanged(nameof(FooterLine));
        UpdateRequestWorkLabel();
    }

    private void RestoreSelectedSessionCache()
    {
        if (SelectedSession is null)
        {
            lastSyncedAt = null;
            RenderSnapshot(EmptySnapshot());
            IsRefreshing = false;
            StatusLine = T("source.noSession");
            OnPropertyChanged(nameof(LastSyncedLabel));
            OnPropertyChanged(nameof(FooterLine));
            return;
        }

        try
        {
            state.SwitchActiveSession(SelectedSession.Id);
            lastSyncedAt = sessionSyncedAt.TryGetValue(SelectedSession.Id, out var syncedAt)
                ? syncedAt
                : null;
            var snapshot = service.GetCachedResourceSnapshot(BuildDisplayCacheQuery(), applyFilters: false);
            var rendered = RenderSnapshot(snapshot);
            IsRefreshing = true;
            StatusLine = snapshot.Rows.Count > 0
                ? TF("source.showingCached", SelectedSession.DisplayName)
                : TF("source.loadingResources", SelectedSession.DisplayName);
            if (!rendered)
            {
                NotifyResourceLogoStateChanged();
            }

            OnPropertyChanged(nameof(LastSyncedLabel));
            OnPropertyChanged(nameof(FooterLine));
        }
        catch (PodlordException ex)
        {
            RenderSnapshot(EmptySnapshot());
            IsRefreshing = false;
            StatusLine = ex.Message;
        }
    }

    private ResourceExplorerSnapshot EmptySnapshot()
    {
        return new ResourceExplorerSnapshot(
            SelectedSession?.Id ?? string.Empty,
            string.Empty,
            SelectedSession?.ClusterName ?? string.Empty,
            SelectedSession?.DisplayName ?? string.Empty,
            SelectedSession?.NamespaceScope ?? NamespaceScope.All,
            PodlordText.NowUtcString(new SystemPodlordClock()),
            FreshnessState.Unknown,
            [],
            [],
            [],
            [],
            [],
            [],
            [],
            [],
            []);
    }

    private bool RenderSnapshot(ResourceExplorerSnapshot snapshot)
    {
        var rows = ApplyEventLifecycle(snapshot.Rows);
        if (RenderedSnapshotMatches(rows, snapshot.Failures))
        {
            return false;
        }

        cachedRows.Clear();
        cachedRows.AddRange(rows);
        RefreshFocusedResourceReference(rows);
        restartOutlierThreshold = ResourceFilterMatcher.RestartOutlierThreshold(cachedRows);
        UpdateRadarIdleTimer();
        UpdateFilterOptions(snapshot with { Rows = rows });
        Failures.Clear();
        foreach (var failure in snapshot.Failures)
        {
            Failures.Add(failure);
        }

        UpdateHealthSegments(cachedRows);
        ApplyLocalFilter();
        return true;
    }

    private void RefreshFocusedResourceReference(IReadOnlyList<FlatResourceRow> rows)
    {
        if (selectedResource is null)
        {
            return;
        }

        var replacement = rows.FirstOrDefault(row => row.Id.Equals(selectedResource.Id, StringComparison.Ordinal));
        if (replacement is null)
        {
            return;
        }

        selectedResource = replacement;
        if (selectedResourceRow?.Id == replacement.Id)
        {
            selectedResourceRow = replacement;
        }

        NotifyInspectorTargetChanged();
        if (IsInspectorVisible && IsInspectorOverviewActive)
        {
            RenderCachedResourceSummary(replacement);
        }
    }

    private bool RenderedSnapshotMatches(
        IReadOnlyList<FlatResourceRow> rows,
        IReadOnlyList<ResourceListFailure> failures)
    {
        return cachedRows.SequenceEqual(rows)
               && Failures.SequenceEqual(failures);
    }

    private IReadOnlyList<FlatResourceRow> ApplyEventLifecycle(IReadOnlyList<FlatResourceRow> rows)
    {
        var lookup = rows
            .Where(row => row.Kind != "Event")
            .GroupBy(ResourceLookupKey, StringComparer.Ordinal)
            .ToDictionary(group => group.Key, group => group.First(), StringComparer.Ordinal);
        return rows
            .Select(row => row.Kind == "Event" ? ApplyEventLifecycle(row, lookup) : row)
            .ToList();
    }

    private FlatResourceRow ApplyEventLifecycle(
        FlatResourceRow row,
        IReadOnlyDictionary<string, FlatResourceRow> resourcesByKey)
    {
        var status = EventLifecycleStatus(row, resourcesByKey);
        return status.Equals(row.Status, StringComparison.Ordinal) ? row : row with { Status = status };
    }

    private string EventLifecycleStatus(
        FlatResourceRow row,
        IReadOnlyDictionary<string, FlatResourceRow> resourcesByKey)
    {
        var related = RelatedResource(row, resourcesByKey);
        if (IsWarningEvent(row.Status))
        {
            if (related is not null)
            {
                return ResourceFilterMatcher.ProblemReason(related, restartOutlierThreshold).Length == 0
                    ? "Succeeded"
                    : row.Status;
            }

            return EventRecent(row, TimeSpan.FromMinutes(30)) ? row.Status : "Observed";
        }

        return EventRecent(row, TimeSpan.FromMinutes(5)) ? row.Status : "Observed";
    }

    private static FlatResourceRow? RelatedResource(
        FlatResourceRow row,
        IReadOnlyDictionary<string, FlatResourceRow> resourcesByKey)
    {
        if (string.IsNullOrWhiteSpace(row.EventObject))
        {
            return null;
        }

        var parts = row.EventObject.Split('/', 2, StringSplitOptions.TrimEntries);
        if (parts.Length != 2 || parts[0].Length == 0 || parts[1].Length == 0)
        {
            return null;
        }

        var key = ResourceLookupKey(row.Cluster, row.Namespace, parts[0], parts[1]);
        return resourcesByKey.TryGetValue(key, out var related) ? related : null;
    }

    private static string ResourceLookupKey(FlatResourceRow row)
    {
        return ResourceLookupKey(row.Cluster, row.Namespace, row.Kind, row.Name);
    }

    private static string ResourceLookupKey(string cluster, string? ns, string kind, string name)
    {
        return $"{cluster}:{ns ?? "cluster"}:{kind}:{name}";
    }

    private static bool IsWarningEvent(string status)
    {
        return status is "Warning" or "Error" or "Failed" or "Critical" or "Unavailable";
    }

    private static bool EventRecent(FlatResourceRow row, TimeSpan ttl)
    {
        return ResourceFilterMatcher.ParseHumanDuration(row.LastChange) is { } changed
            ? changed <= ttl
            : ResourceFilterMatcher.ParseHumanDuration(row.Age) is { } age && age <= ttl;
    }

    public async Task LoadFreshYamlAsync()
    {
        var focusedResource = SelectedResource;
        if (focusedResource is null)
        {
            return;
        }
        var identity = new ResourceIdentity(
            SelectedSession?.Id,
            focusedResource.Kind,
            focusedResource.Namespace,
            focusedResource.Name);
        if (string.IsNullOrWhiteSpace(DetailYaml) || DetailYaml.StartsWith("Loading", StringComparison.OrdinalIgnoreCase))
        {
            DetailYaml = "Loading fresh YAML through the Kubernetes request queue...";
        }
        YamlApplyStatus = "Fetching fresh YAML from the cluster...";
        try
        {
            var detail = await service.GetResourceDetailAsync(identity, true, KubernetesRequestPriority.Foreground, lifetime.Token).ConfigureAwait(true);
            if (SelectedResource?.Id != focusedResource.Id)
            {
                return;
            }
            DetailYaml = detail.Yaml;
            isYamlLoaded = true;
            YamlApplyStatus = "Fresh YAML loaded. Edit carefully; server-side apply uses field manager podlord.";
        }
        catch (OperationCanceledException)
        {
        }
        catch (PodlordException ex)
        {
            if (SelectedResource?.Id != focusedResource.Id)
            {
                return;
            }
            DetailYaml = $"# Could not load YAML: {ex.Message}";
            YamlApplyStatus = ex.Message;
        }
    }

    internal void RenderDetailForTesting(ResourceDetail detail, bool forceYamlRefresh = false) => RenderDetail(detail, forceYamlRefresh);

    private void RenderDetail(ResourceDetail detail, bool forceYamlRefresh = false)
    {
        var detailItems = detail.Summary
            .Concat(detail.Conditions.Select(condition => new DetailItem($"Condition: {condition.Label}", condition.Value)))
            .ToList();
        if (selectedResource is not null && SameResource(detail.Identity, selectedResource))
        {
            detailItems = MergeCachedMetricItems(detailItems, selectedResource);
        }

        SetDetailItems(detailItems);
        UpdatePodLogContainers(detailItems);

        SyncCollection(FocusedEvents, detail.Events.Select(item => new EventTimelineRow(
                item.EventType,
                detail.Identity.Name,
                item.Reason,
                $"{detail.Identity.Kind}/{detail.Identity.Name}",
                detail.Identity.Namespace ?? "cluster",
                item.LastSeen,
                item.Message)).ToList());

        SyncCollection(FocusedRelationships, Relationships.Where(row =>
                     (row.ToKind.Equals(detail.Identity.Kind, StringComparison.OrdinalIgnoreCase)
                      && row.ToName.Equals(detail.Identity.Name, StringComparison.Ordinal)
                      && row.Namespace.Equals(detail.Identity.Namespace ?? "cluster", StringComparison.Ordinal))
                     || row.FromName.Equals(detail.Identity.Name, StringComparison.Ordinal)).Take(256).ToList());
        SyncCollection(ResourceValues, detail.Values
            .Select(item => new ResourceValueRow(item.Key, item.Value, item.Sensitive, item.Base64Encoded))
            .ToList());

        if (forceYamlRefresh || !IsInspectorYamlActive || !isYamlLoaded)
        {
            DetailYaml = detail.Yaml;
            isYamlLoaded = true;
            YamlApplyStatus = "Fresh YAML loaded. Edit carefully; server-side apply uses field manager podlord.";
        }
        else
        {
            YamlApplyStatus = "Fresh detail loaded; YAML editor preserved while YAML tab is active.";
        }
    }

    private void RenderCachedResourceSummary(FlatResourceRow row)
    {
        var items = new List<DetailItem>
        {
            new DetailItem("Kind", row.Kind),
            new DetailItem("Name", row.Name),
            new DetailItem("Namespace", row.Namespace ?? "cluster"),
            new DetailItem("Cluster", row.Cluster),
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
            new DetailItem("Issue", ResourceFilterMatcher.ProblemReason(row, restartOutlierThreshold).Length == 0 ? "none" : ResourceFilterMatcher.ProblemReason(row, restartOutlierThreshold)),
            new DetailItem("ID", row.Id)
        };
        if (row.Kind == "Event")
        {
            items.AddRange(
            [
                new DetailItem("Reason", string.IsNullOrWhiteSpace(row.EventReason) ? "-" : row.EventReason),
                new DetailItem("Message", string.IsNullOrWhiteSpace(row.EventMessage) ? "-" : row.EventMessage),
                new DetailItem("Involved object", string.IsNullOrWhiteSpace(row.EventObject) ? "-" : row.EventObject)
            ]);
        }

        if (row.Pulse.CpuMillicores is not null)
        {
            items.Add(new DetailItem("CPU limit suggestion", row.Pulse.CpuLimitSuggestion));
        }

        if (row.Pulse.MemoryBytes is not null)
        {
            items.Add(new DetailItem("Memory limit suggestion", row.Pulse.MemoryLimitSuggestion));
        }

        if (row.Kind == "ReplicaSet")
        {
            items.Add(new DetailItem("Replica insight", row.Ready == "-" ? "fresh detail loading" : $"ready {row.Ready}"));
        }

        SetDetailItems(items);
        UpdatePodLogContainers(items);

        SyncCollection(FocusedEvents, Array.Empty<EventTimelineRow>());
        SyncCollection(FocusedRelationships, Relationships.Where(candidate =>
                     candidate.ToKind.Equals(row.Kind, StringComparison.OrdinalIgnoreCase)
                     && candidate.ToName.Equals(row.Name, StringComparison.Ordinal)
                     && candidate.Namespace.Equals(row.Namespace ?? "cluster", StringComparison.Ordinal)).Take(128).ToList());
        SyncCollection(ResourceValues, Array.Empty<ResourceValueRow>());

        DetailYaml = "Loading fresh YAML through the Kubernetes request queue...";
        isYamlLoaded = false;
        YamlApplyStatus = "Showing cached resource identity while fresh YAML loads.";
        LogText = row.Kind == "Pod"
            ? IsInspectorLogsActive ? "Loading pod log tail..." : "Open the Logs tab to start pod log tail."
            : "Logs are available for pods.";
    }

    private void RenderSourceSummary(SourceStatusRow source)
    {
        var items = new[]
        {
            new DetailItem("Name", source.Context),
            new DetailItem("Source", source.Name),
            new DetailItem("Status", source.Status),
            new DetailItem("Path", source.Source),
            new DetailItem("Owned copy", string.IsNullOrWhiteSpace(source.OwnedKubeconfigPath) ? "-" : source.OwnedKubeconfigPath),
            new DetailItem("Cluster", source.Cluster),
            new DetailItem("Server", string.IsNullOrWhiteSpace(source.Server) ? "-" : source.Server),
            new DetailItem("User", source.User),
            new DetailItem("Auth", source.AuthType),
            new DetailItem("Filter", source.FilterName),
            new DetailItem("Imported", source.ImportedAt),
            new DetailItem("Hash", source.Hash),
            new DetailItem("Detail", source.Detail),
            new DetailItem("Context ID", source.ContextId)
        };

        SetDetailItems(items);
        ResetPodLogContainers();
        SyncCollection(FocusedEvents, Array.Empty<EventTimelineRow>());
        SyncCollection(FocusedRelationships, Array.Empty<RelationshipRow>());
        SyncCollection(ResourceValues, Array.Empty<ResourceValueRow>());
        DetailYaml = LoadSourceYaml(source);
        isYamlLoaded = true;
        YamlApplyStatus = "Source YAML loaded from Podlord storage. Applying imports edited config without modifying the original path.";
        LogText = "Logs are not available for kubeconfig sources.";
    }

    public void ToggleResourceValueReveal(ResourceValueRow row)
    {
        if (!row.IsSensitive)
        {
            return;
        }

        if (row.IsRevealed)
        {
            row.Hide();
            return;
        }

        row.RevealTemporarily();
        _ = HideResourceValueLaterAsync(row, TimeSpan.FromSeconds(20));
    }

    private async Task HideResourceValueLaterAsync(ResourceValueRow row, TimeSpan delay)
    {
        try
        {
            await Task.Delay(delay, lifetime.Token).ConfigureAwait(true);
            if (ResourceValues.Contains(row))
            {
                row.Hide();
            }
        }
        catch (OperationCanceledException)
        {
        }
    }

    private void SetDetailItems(IEnumerable<DetailItem> items)
    {
        var detailItems = DetailItemFilter.Available(items);
        SyncCollection(Summary, detailItems);
        SyncCollection(FocusMetrics, MetricRowsFromDetails(detailItems));
    }

    private static bool SameResource(ResourceIdentity identity, FlatResourceRow row)
    {
        return row.Kind.Equals(identity.Kind, StringComparison.Ordinal)
               && row.Name.Equals(identity.Name, StringComparison.Ordinal)
               && string.Equals(row.Namespace, identity.Namespace, StringComparison.Ordinal)
               && (identity.SessionId is null || row.Id.StartsWith($"{identity.SessionId}:", StringComparison.Ordinal));
    }

    private static List<DetailItem> MergeCachedMetricItems(IReadOnlyList<DetailItem> detailItems, FlatResourceRow row)
    {
        var metricLabels = new HashSet<string>(StringComparer.Ordinal)
        {
            "CPU",
            "CPU %",
            "CPU limit suggestion",
            "Memory",
            "Memory %",
            "Memory limit suggestion",
            "Network",
            "Storage",
            "Metric source"
        };
        var merged = detailItems.Where(item => !metricLabels.Contains(item.Label)).ToList();
        merged.AddRange(CachedMetricItems(row));
        return merged;
    }

    private static IReadOnlyList<DetailItem> CachedMetricItems(FlatResourceRow row)
    {
        var items = new List<DetailItem>();
        if (row.Pulse.CpuMillicores is not null || row.Pulse.CpuLimitMillicores is > 0)
        {
            items.Add(new DetailItem("CPU", row.CpuSummaryDisplay));
            if (row.Pulse.CpuMillicores is not null)
            {
                items.Add(new DetailItem("CPU limit suggestion", row.Pulse.CpuLimitSuggestion));
            }
        }

        if (row.Pulse.MemoryBytes is not null || row.Pulse.MemoryLimitBytes is > 0)
        {
            items.Add(new DetailItem("Memory", row.MemorySummaryDisplay));
            if (row.Pulse.MemoryBytes is not null)
            {
                items.Add(new DetailItem("Memory limit suggestion", row.Pulse.MemoryLimitSuggestion));
            }
        }

        if (row.HasNetworkMetricInfo)
        {
            items.Add(new DetailItem("Network", row.NetworkDisplay));
        }

        if (row.Pulse.StorageUsedBytes is not null || row.Pulse.StorageLimitBytes is > 0)
        {
            items.Add(new DetailItem("Storage", row.StorageDisplay));
        }

        if (!row.MetricSourceBadge.Equals("API", StringComparison.OrdinalIgnoreCase))
        {
            items.Add(new DetailItem("Metric source", row.MetricSourceBadge));
        }

        return items;
    }

    private static List<FocusMetricRow> MetricRowsFromDetails(IReadOnlyList<DetailItem> detailItems)
    {
        var suggestions = detailItems
            .Where(item => item.Label is "CPU limit suggestion" or "Memory limit suggestion")
            .ToDictionary(item => item.Label, item => item.Value, StringComparer.Ordinal);
        var hiddenLabels = new HashSet<string>(StringComparer.Ordinal)
        {
            "CPU %",
            "CPU limit suggestion",
            "Memory %",
            "Memory limit suggestion"
        };

        return detailItems
            .Where(item => !hiddenLabels.Contains(item.Label))
            .Select(item => MetricFromDetail(item, suggestions))
            .ToList();
    }

    private static string LoadSourceYaml(SourceStatusRow source)
    {
        var candidates = new[] { source.OwnedKubeconfigPath, source.Source }
            .Where(path => !string.IsNullOrWhiteSpace(path))
            .Where(path => !IsVirtualSource(path))
            .Distinct(StringComparer.Ordinal)
            .ToList();

        foreach (var path in candidates)
        {
            try
            {
                if (File.Exists(path))
                {
                    return File.ReadAllText(path);
                }
            }
            catch (IOException ex)
            {
                return $"# Could not read kubeconfig YAML: {ex.Message}";
            }
            catch (UnauthorizedAccessException ex)
            {
                return $"# Could not read kubeconfig YAML: {ex.Message}";
            }
        }

        return "# No kubeconfig YAML snapshot is available for this source.";
    }

    private static FocusMetricRow MetricFromDetail(DetailItem item, IReadOnlyDictionary<string, string> suggestions)
    {
        var suggestion = item.Label switch
        {
            "CPU" => suggestions.GetValueOrDefault("CPU limit suggestion", string.Empty),
            "Memory" => suggestions.GetValueOrDefault("Memory limit suggestion", string.Empty),
            _ => string.Empty
        };

        if (LimitOnlyRatio(item.Value))
        {
            return new FocusMetricRow(
                item.Label,
                item.Value,
                0,
                false,
                CleanSuggestion(suggestion),
                0);
        }

        if (UnitRatioPercent(item.Label, item.Value) is { } unitRatio)
        {
            return new FocusMetricRow(
                item.Label,
                item.Value,
                unitRatio,
                true,
                CleanSuggestion(suggestion),
                SuggestionRatioPercent(item.Label, item.Value, suggestion) ?? 0);
        }

        if (RatioPercent(item.Value) is { } ratio)
        {
            return new FocusMetricRow(
                item.Label,
                item.Value,
                ratio,
                true,
                CleanSuggestion(suggestion),
                SuggestionRatioPercent(item.Label, item.Value, suggestion) ?? 0,
                IsReadinessLabel(item.Label));
        }

        if (item.Label == "Restarts" && int.TryParse(item.Value, out var restarts))
        {
            return new FocusMetricRow(item.Label, item.Value, Math.Clamp(restarts * 10, 0, 100), restarts > 0);
        }

        if (item.Value.EndsWith('%') && double.TryParse(item.Value.TrimEnd('%'), out var percent))
        {
            return new FocusMetricRow(item.Label, item.Value, Math.Clamp(percent, 0, 100), true);
        }

        return new FocusMetricRow(item.Label, item.Value, 0, false);
    }

    private static string CleanSuggestion(string suggestion)
    {
        return suggestion == "-" ? string.Empty : suggestion;
    }

    /// <summary>Readiness/availability ratios are healthy when full, unlike utilization ratios.</summary>
    internal static bool IsReadinessLabel(string label)
    {
        return label is "Ready" or "Available" or "Up-to-date" or "Availability" or "Readiness";
    }

    internal static double? SuggestionRatioPercent(string label, string value, string suggestion)
    {
        if (string.IsNullOrWhiteSpace(suggestion) || suggestion == "-")
        {
            return null;
        }

        Func<string, double?>? parser = label switch
        {
            "CPU" => ParseCpuMetricQuantity,
            "Memory" or "Storage" => ParseByteMetricQuantity,
            _ => null
        };
        if (parser is null)
        {
            return null;
        }

        var total = RatioDenominator(value, parser);
        var suggestedLimit = LastQuantity(suggestion, parser);
        return total is > 0 && suggestedLimit is > 0
            ? Math.Clamp(suggestedLimit.Value / total.Value * 100d, 0, 100)
            : null;
    }

    private static double? RatioDenominator(string value, Func<string, double?> parser)
    {
        var parts = value.Split('/', StringSplitOptions.TrimEntries);
        return parts.Length == 2 ? parser(parts[1]) : null;
    }

    private static double? LastQuantity(string value, Func<string, double?> parser)
    {
        double? quantity = null;
        foreach (var token in value.Split([' ', '/', '\t', '\r', '\n'], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            quantity = parser(token.Trim(',', ';', '.', ':', '(', ')')) ?? quantity;
        }

        return quantity;
    }

    private static double? UnitRatioPercent(string label, string value)
    {
        return label switch
        {
            "CPU" => QuantityRatioPercent(value, ParseCpuMetricQuantity),
            "Memory" or "Storage" => QuantityRatioPercent(value, ParseByteMetricQuantity),
            _ => null
        };
    }

    private static double? QuantityRatioPercent(string value, Func<string, double?> parser)
    {
        var parts = value.Split('/', StringSplitOptions.TrimEntries);
        if (parts.Length != 2)
        {
            return null;
        }

        var current = parser(parts[0]);
        var total = parser(parts[1]);
        if (current is null || total is null || total <= 0)
        {
            return null;
        }

        return Math.Clamp(current.Value / total.Value * 100d, 0, 100);
    }

    private static double? ParseCpuMetricQuantity(string value)
    {
        var trimmed = value.Trim();
        if (trimmed.EndsWith('m') && double.TryParse(trimmed[..^1], NumberStyles.Float, CultureInfo.InvariantCulture, out var milli))
        {
            return milli;
        }

        if (trimmed.EndsWith('c') && double.TryParse(trimmed[..^1], NumberStyles.Float, CultureInfo.InvariantCulture, out var cores))
        {
            return cores * 1000d;
        }

        return double.TryParse(trimmed, NumberStyles.Float, CultureInfo.InvariantCulture, out var number)
            ? number
            : null;
    }

    private static double? ParseByteMetricQuantity(string value)
    {
        var trimmed = value.Trim();
        var units = new (string Suffix, double Scale)[]
        {
            ("Ki", 1024d),
            ("Mi", 1024d * 1024d),
            ("Gi", 1024d * 1024d * 1024d),
            ("Ti", 1024d * 1024d * 1024d * 1024d),
            ("B", 1d)
        };
        foreach (var (suffix, scale) in units)
        {
            if (trimmed.EndsWith(suffix, StringComparison.Ordinal)
                && double.TryParse(trimmed[..^suffix.Length], NumberStyles.Float, CultureInfo.InvariantCulture, out var number))
            {
                return number * scale;
            }
        }

        return double.TryParse(trimmed, NumberStyles.Float, CultureInfo.InvariantCulture, out var raw)
            ? raw
            : null;
    }

    private static bool LimitOnlyRatio(string value)
    {
        var parts = value.Split('/', StringSplitOptions.TrimEntries);
        return parts.Length == 2
            && parts[0] == "-"
            && parts[1].Length > 0;
    }

    private static double? RatioPercent(string value)
    {
        var parts = value.Split('/', StringSplitOptions.TrimEntries);
        if (parts.Length != 2
            || !double.TryParse(parts[0], out var current)
            || !double.TryParse(parts[1], out var total)
            || total <= 0)
        {
            return null;
        }

        return Math.Clamp(current / total * 100, 0, 100);
    }

    private void StartLogTail(string ns, string pod)
    {
        var key = $"{SelectedSession?.Id ?? "default"}:{ns}:{pod}";
        if (activeLogTailKey == key && logTail is not null)
        {
            return;
        }

        StopLogTail();
        activeLogTailKey = key;
        logTail = CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);
        _ = TailSelectedPodLoop(ns, pod, logTail.Token);
    }

    private void StopLogTail()
    {
        logTail?.Cancel();
        logTail?.Dispose();
        logTail = null;
        activeLogTailKey = null;
    }

    private CancellationTokenSource BeginFocusLoad()
    {
        CancelFocusLoad();
        focusLoad = CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);
        return focusLoad;
    }

    private void CancelFocusLoad()
    {
        focusLoad?.Cancel();
        focusLoad?.Dispose();
        focusLoad = null;
    }

    private void UpdateInspectorTabWork()
    {
        if (!IsInspectorVisible)
        {
            StopLogTail();
            return;
        }

        if (!IsSelectedResourceLoggable)
        {
            StopLogTail();
            if (selectedSource is not null)
            {
                LogText = "Logs are not available for kubeconfig sources.";
            }
            else if (SelectedResource is not null)
            {
                LogText = "Logs are available for pods.";
            }

            return;
        }

        if (SelectedResource is not { Namespace: { Length: > 0 } ns })
        {
            StopLogTail();
            LogText = "Pod log tail requires a namespace.";
            return;
        }

        if (!IsInspectorLogsActive)
        {
            StopLogTail();
            return;
        }

        StartLogTail(ns, SelectedResource.Name);
    }

    private async Task TailSelectedPodLoop(string ns, string pod, CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                if (!LogsPaused)
                {
                    var container = SelectedPodLogContainer == AllPodLogContainersOption ? null : SelectedPodLogContainer;
                    var request = new PodLogRequest(SelectedSession?.Id, ns, pod, container, 100, false);
                    var cached = service.GetCachedPodLogs(request);
                    if (cached is not null)
                    {
                        LogText = cached.Text.Length == 0 ? "No log lines in the selected tail window." : cached.Text;
                    }

                    var priority = isAppFocused ? KubernetesRequestPriority.Foreground : KubernetesRequestPriority.Background;
                    var logs = await service.GetPodLogsAsync(request, true, priority, cancellationToken).ConfigureAwait(true);
                    LogText = logs.Text.Length == 0 ? "No log lines in the selected tail window." : logs.Text;
                }
            }
            catch (OperationCanceledException)
            {
                return;
            }
            catch (PodlordException ex)
            {
                LogText = ex.Message;
            }

            await Task.Delay(LogTailInterval(), cancellationToken).ConfigureAwait(true);
        }
    }

    private void UpdatePodLogContainers(IEnumerable<DetailItem> items)
    {
        var options = items
            .Where(item => item.Label.Equals("Containers", StringComparison.Ordinal))
            .SelectMany(item => ParsePodLogContainers(item.Value))
            .Distinct(StringComparer.Ordinal)
            .Prepend(AllPodLogContainersOption)
            .ToList();
        SyncCollection(PodLogContainerOptions, options);
        if (!options.Contains(SelectedPodLogContainer, StringComparer.Ordinal))
        {
            SelectedPodLogContainer = AllPodLogContainersOption;
        }
    }

    private void ResetPodLogContainers()
    {
        SyncCollection(PodLogContainerOptions, [AllPodLogContainersOption]);
        if (!string.Equals(SelectedPodLogContainer, AllPodLogContainersOption, StringComparison.Ordinal))
        {
            SelectedPodLogContainer = AllPodLogContainersOption;
        }
    }

    private static IEnumerable<string> ParsePodLogContainers(string value)
    {
        return value.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Where(name => name.Length > 0 && name != "-");
    }

    private ResourceQuery BuildRemoteQuery(bool force)
    {
        return new ResourceQuery(
            SessionId: SelectedSession?.Id,
            Kind: EmptyToNull(RemoteKindExpression(KindPicker.Expression)),
            Namespace: EmptyToNull(NamespacePicker.Expression),
            ProblemsOnly: false,
            Limit: 5_000,
            ForceRefresh: force);
    }

    private static string RemoteKindExpression(string expression)
    {
        var trimmed = expression.Trim();
        if (trimmed.Length == 0 || ResourceFilterMatcher.MatchesText("Event", trimmed))
        {
            return trimmed;
        }

        return $"{trimmed} \"Event\"";
    }

    private ResourceQuery BuildDisplayCacheQuery()
    {
        return new ResourceQuery(
            SessionId: SelectedSession?.Id,
            Limit: 5_000,
            ForceRefresh: false);
    }

    private ResourceQuery BuildBackgroundWarmQuery(bool force)
    {
        var now = DateTimeOffset.Now;
        if (HasRemoteScopeFilter() && now - lastBroadRefreshAt > TimeSpan.FromMinutes(2))
        {
            lastBroadRefreshAt = now;
            return new ResourceQuery(
                SessionId: SelectedSession?.Id,
                Limit: 5_000,
                ForceRefresh: force);
        }

        return BuildRemoteQuery(force) with { ProblemsOnly = ProblemsOnly };
    }

    private bool HasRemoteScopeFilter()
    {
        return !string.IsNullOrWhiteSpace(KindPicker.Expression)
               || !string.IsNullOrWhiteSpace(NamespacePicker.Expression)
               || ProblemsOnly;
    }

    private ResourceQuery BuildLocalQuery()
    {
        // Top-nav search is the single source of row filtering for the Resources workspace.
        // Fall back to the preset-saved Search so loading a preset still narrows rows even when the search bar is closed.
        var effectiveSearch = string.IsNullOrWhiteSpace(ResourceQuickSearch) ? Search : ResourceQuickSearch;
        return new ResourceQuery(
            SessionId: SelectedSession?.Id,
            Search: EmptyToNull(effectiveSearch),
            Id: null,
            Issue: EmptyToNull(IssuePicker.Expression),
            Kind: EmptyToNull(KindPicker.Expression),
            Name: EmptyToNull(NamePicker.Expression),
            Namespace: EmptyToNull(NamespacePicker.Expression),
            Cluster: EmptyToNull(ClusterPicker.Expression),
            Status: EmptyToNull(StatusPicker.Expression),
            Age: EmptyToNull(AgePicker.Expression),
            Node: EmptyToNull(NodePicker.Expression),
            Image: EmptyToNull(ImagePicker.Expression),
            Ready: EmptyToNull(ReadyPicker.Expression),
            Restarts: EmptyToNull(RestartFilter),
            Owner: EmptyToNull(OwnerPicker.Expression),
            ProblemsOnly: ProblemsOnly,
            ActivityOnly: ActivityOnly,
            Limit: ParseLimit(),
            ForceRefresh: false,
            Cpu: EmptyToNull(CpuPicker.Expression),
            Memory: EmptyToNull(MemoryPicker.Expression),
            Storage: EmptyToNull(StoragePicker.Expression));
    }

    private void UpdateFilterOptions(ResourceExplorerSnapshot snapshot)
    {
        IssuePicker.ReplaceOptions(snapshot.Rows.Select(row => ResourceFilterMatcher.ProblemReason(row, restartOutlierThreshold)).Where(value => value.Length > 0));
        KindPicker.ReplaceOptions(snapshot.Kinds);
        NamePicker.ReplaceOptions(snapshot.Rows.Select(row => row.Name));
        NamespacePicker.ReplaceOptions(snapshot.Namespaces);
        ClusterPicker.ReplaceOptions(snapshot.Rows.Select(row => row.Cluster));
        StatusPicker.ReplaceOptions(snapshot.Statuses);
        AgePicker.ReplaceOptions(snapshot.Rows.Select(row => row.Age));
        NodePicker.ReplaceOptions(snapshot.Nodes);
        ImagePicker.ReplaceOptions(snapshot.Rows
            .Where(CanHaveImages)
            .SelectMany(row => ImageOptions(row.ImageSummary)));
        ReadyPicker.ReplaceOptions(snapshot.ReadyValues);
        RestartPicker.ReplaceOptions(snapshot.Rows.Select(row => row.Restarts.ToString()));
        CpuPicker.ReplaceOptions(snapshot.Rows.Select(row => row.CpuSummaryDisplay).Where(value => value != "-"));
        MemoryPicker.ReplaceOptions(snapshot.Rows.Select(row => row.MemorySummaryDisplay).Where(value => value != "-"));
        StoragePicker.ReplaceOptions(snapshot.Rows.Select(row => row.StorageDisplay).Where(value => value != "-"));
        OwnerPicker.ReplaceOptions(snapshot.Owners);
    }

    private static bool CanHaveImages(FlatResourceRow row)
    {
        return row.Kind is "Pod" or "Deployment" or "ReplicaSet" or "StatefulSet" or "DaemonSet" or "Job" or "CronJob";
    }

    private static IEnumerable<string> ImageOptions(string imageSummary)
    {
        return imageSummary
            .Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries)
            .Where(value => value.Length > 0 && value != "-");
    }

    private void ReloadSources()
    {
        foreach (var previous in Sources)
        {
            previous.PropertyChanged -= SourceRowPropertyChanged;
        }
        Sources.Clear();
        ImportedContextRows.Clear();
        foreach (var context in DisplayImportedContexts(state.Snapshot().ImportedContexts)
                     .OrderByDescending(context => context.ImportedAt, StringComparer.Ordinal)
                     .ThenBy(context => string.IsNullOrWhiteSpace(context.SourceName) ? context.SourcePath : context.SourceName, StringComparer.Ordinal)
                     .ThenBy(context => context.Name, StringComparer.Ordinal))
        {
            var fileStatus = SourceStatus(context.SourcePath);
            var broken = context.BrokenReferences.Count == 0
                ? string.Empty
                : string.Join("; ", context.BrokenReferences);
            var sourceName = string.IsNullOrWhiteSpace(context.SourceName) ? SourceName(context.SourcePath) : context.SourceName;
            var hash = string.IsNullOrWhiteSpace(context.SourceContentHash) ? "-" : context.SourceContentHash;
            var detail = broken.Length == 0 ? context.Server ?? "-" : broken;
            var owned = context.OwnedKubeconfigPath is { Length: > 0 } ? $"copy: {Path.GetFileName(context.OwnedKubeconfigPath)}" : "copy: memory";
            var sourceRow = new SourceStatusRow(
                sourceName,
                hash,
                context.ImportedAt,
                context.SourcePath,
                context.DisplayName,
                context.ClusterName,
                context.UserName,
                context.AuthType,
                context.BrokenReferences.Count == 0 ? fileStatus : "error",
                $"{detail}; {owned}",
                context.ContextId,
                context.OwnedKubeconfigPath ?? string.Empty,
                context.Server ?? string.Empty,
                string.IsNullOrWhiteSpace(context.FilterName) ? FilterPresetStore.DefaultFilterName : context.FilterName.Trim(),
                RenameSourceRow,
                AssignSourceRowFilter);
            sourceRow.PropertyChanged += SourceRowPropertyChanged;
            Sources.Add(sourceRow);
            ImportedContextRows.Add(new ImportedContextRowViewModel(
                context.ContextId,
                context.SourcePath,
                context.DisplayName,
                RemoveImportedContextRow,
                RenameImportedContextRow,
                ActivateImportedContextRow,
                Sessions.Any(session => session.ContextId == context.ContextId && session.Id == state.Snapshot().ActiveSessionId),
                sourceName,
                hash));
        }

        OnPropertyChanged(nameof(VisibleImportedContexts));
        OnPropertyChanged(nameof(ImportedSourcesLabel));
        OnPropertyChanged(nameof(ActiveSessionChipLabel));
        OnPropertyChanged(nameof(RadarSourceLabel));
        if (selectedSource is { ContextId.Length: > 0 } current)
        {
            var replacement = Sources.FirstOrDefault(source => source.ContextId == current.ContextId);
            if (replacement is not null)
            {
                selectedSource = replacement;
                OnPropertyChanged(nameof(SelectedSource));
                if (IsInspectorVisible && SelectedResource?.Kind == "Source")
                {
                    FocusSource(replacement);
                }
            }
        }

        RebuildSourceWatchers();
    }

    private void SourceRowPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (sender is not SourceStatusRow row
            || !ReferenceEquals(row, selectedSource)
            || !IsInspectorVisible
            || selectingResource
            || e.PropertyName is not (nameof(SourceStatusRow.Context) or nameof(SourceStatusRow.FilterName)))
        {
            return;
        }

        FocusSource(row);
    }

    private static IReadOnlyList<ImportedContext> DisplayImportedContexts(IEnumerable<ImportedContext> contexts)
    {
        var visible = new List<ImportedContext>();
        foreach (var context in contexts.OrderByDescending(context => context.ImportedAt, StringComparer.Ordinal))
        {
            if (visible.Any(existing => SameDisplaySource(existing, context)))
            {
                continue;
            }

            visible.Add(context);
        }

        return visible;
    }

    private static bool SameDisplaySource(ImportedContext left, ImportedContext right)
    {
        if (!string.Equals(left.Name, right.Name, StringComparison.Ordinal)
            || !string.Equals(left.ClusterName, right.ClusterName, StringComparison.Ordinal)
            || !string.Equals(left.UserName, right.UserName, StringComparison.Ordinal)
            || !string.Equals(left.Server ?? string.Empty, right.Server ?? string.Empty, StringComparison.Ordinal))
        {
            return false;
        }

        if (string.Equals(PathIdentity(left.SourcePath), PathIdentity(right.SourcePath), StringComparison.Ordinal))
        {
            return true;
        }

        if (!string.IsNullOrWhiteSpace(left.SourceContentHash) && !string.IsNullOrWhiteSpace(right.SourceContentHash))
        {
            return string.Equals(left.SourceContentHash, right.SourceContentHash, StringComparison.Ordinal);
        }

        return false;
    }

    private static string PathIdentity(string sourcePath)
    {
        return IsVirtualSource(sourcePath) ? sourcePath : Path.GetFullPath(sourcePath);
    }

    private void ActivateImportedContextRow(ImportedContextRowViewModel row)
    {
        var session = state.ListSessions().FirstOrDefault(candidate => candidate.ContextId == row.ContextId);
        if (session is null)
        {
            StatusLine = $"No session exists for {row.DisplayName}.";
            return;
        }

        try
        {
            state.SwitchActiveSession(session.Id);
            StatusLine = $"Activated {session.DisplayName}.";
            ReloadSessions();
            ScheduleRefresh();
        }
        catch (PodlordException ex)
        {
            StatusLine = ex.Message;
        }
    }

    private void RemoveImportedContextRow(ImportedContextRowViewModel row)
    {
        state.RemoveImportedContext(row.ContextId);
        ReloadSources();
        ReloadSessions();
    }

    public void RemoveSource(SourceStatusRow row)
    {
        state.RemoveImportedContext(row.ContextId);
        if (selectedSource?.ContextId == row.ContextId)
        {
            selectedSource = null;
            selectedResource = null;
            selectedResourceRow = null;
            IsInspectorVisible = false;
            NotifyInspectorTargetChanged();
        }

        ReloadSources();
        ReloadSessions();
        StatusLine = $"Removed source snapshot {row.Context}.";
    }

    private void RenameImportedContextRow(ImportedContextRowViewModel row, string newName)
    {
        try
        {
            state.RenameImportedContext(row.ContextId, newName);
        }
        catch (PodlordException ex)
        {
            StatusLine = ex.Message;
            return;
        }

        ReloadSources();
        ReloadSessions();
    }

    private string RenameSourceRow(SourceStatusRow row, string newName)
    {
        try
        {
            var updated = state.RenameImportedContext(row.ContextId, newName);
            var session = state.ListSessions().FirstOrDefault(candidate => candidate.ContextId == row.ContextId);
            if (session is not null)
            {
                state.SetSessionDisplayName(session.Id, updated.DisplayName);
            }

            ReloadSessionRowsOnly();
            StatusLine = $"Renamed source to {updated.DisplayName}.";
            return updated.DisplayName;
        }
        catch (PodlordException ex)
        {
            StatusLine = ex.Message;
            return row.Context;
        }
    }

    private string AssignSourceRowFilter(SourceStatusRow row, string filterName)
    {
        var resolved = ResolveFilterName(filterName);
        try
        {
            state.SetImportedContextFilter(row.ContextId, resolved);
            StatusLine = $"Assigned filter '{resolved}' to {row.Context}.";
            return resolved;
        }
        catch (PodlordException ex)
        {
            StatusLine = ex.Message;
            return row.FilterName;
        }
    }

    private void ReloadSessionRowsOnly()
    {
        var selectedId = SelectedSession?.Id;
        Sessions.Clear();
        foreach (var session in state.ListSessions())
        {
            Sessions.Add(session);
        }

        selectedSession = Sessions.FirstOrDefault(session => session.Id == selectedId)
                          ?? Sessions.FirstOrDefault(session => session.Active)
                          ?? Sessions.FirstOrDefault();
        OnPropertyChanged(nameof(SelectedSession));
        OnPropertyChanged(nameof(ActiveSessionChipLabel));
        OnPropertyChanged(nameof(RadarSourceLabel));
    }

    private void RebuildSourceWatchers()
    {
        DisposeSourceWatchers();
        var paths = state.Snapshot()
            .ImportedContexts
            .Select(context => context.SourcePath)
            .Distinct(StringComparer.Ordinal)
            .Where(path => !IsVirtualSource(path))
            .ToList();
        foreach (var path in paths)
        {
            var directory = Path.GetDirectoryName(path);
            var fileName = Path.GetFileName(path);
            if (string.IsNullOrWhiteSpace(directory) || string.IsNullOrWhiteSpace(fileName) || !Directory.Exists(directory))
            {
                continue;
            }

            try
            {
                var watcher = new FileSystemWatcher(directory, fileName)
                {
                    NotifyFilter = NotifyFilters.FileName | NotifyFilters.LastWrite | NotifyFilters.Size | NotifyFilters.CreationTime,
                    IncludeSubdirectories = false,
                    EnableRaisingEvents = true
                };
                watcher.Changed += SourceFileChanged;
                watcher.Created += SourceFileChanged;
                watcher.Renamed += SourceFileChanged;
                sourceWatchers.Add(watcher);
            }
            catch (IOException)
            {
            }
            catch (UnauthorizedAccessException)
            {
            }
        }
    }

    private void DisposeSourceWatchers()
    {
        foreach (var watcher in sourceWatchers)
        {
            watcher.Changed -= SourceFileChanged;
            watcher.Created -= SourceFileChanged;
            watcher.Renamed -= SourceFileChanged;
            watcher.Dispose();
        }

        sourceWatchers.Clear();
    }

    private void SourceFileChanged(object sender, FileSystemEventArgs args)
    {
        ScheduleSourceRefresh();
    }

    private static string SourceStatus(string sourcePath)
    {
        if (sourcePath.StartsWith("podlord-paste://", StringComparison.Ordinal))
        {
            return "pasted";
        }

        if (sourcePath.StartsWith("podlord-generated://", StringComparison.Ordinal))
        {
            return "generated";
        }

        return File.Exists(sourcePath) ? "file ok" : "file missing";
    }

    private static bool IsVirtualSource(string sourcePath)
    {
        return sourcePath.StartsWith("podlord-paste://", StringComparison.Ordinal)
               || sourcePath.StartsWith("podlord-generated://", StringComparison.Ordinal);
    }

    private static string SourceName(string sourcePath)
    {
        if (sourcePath.StartsWith("podlord-paste://", StringComparison.Ordinal))
        {
            return sourcePath["podlord-paste://".Length..];
        }

        if (sourcePath.StartsWith("podlord-generated://", StringComparison.Ordinal))
        {
            return sourcePath["podlord-generated://".Length..];
        }

        var fileName = Path.GetFileName(sourcePath);
        return string.IsNullOrWhiteSpace(fileName) ? sourcePath : fileName;
    }

    private static async Task<IReadOnlyList<string>> ListK3dClustersAsync(CancellationToken cancellationToken)
    {
        var k3d = ResolveTool("k3d", ["/opt/homebrew/bin/k3d", "/usr/local/bin/k3d"]);
        var result = await CaptureProcessAsync(k3d, ["cluster", "list", "-o", "json"], cancellationToken).ConfigureAwait(false);
        if (result.ExitCode == 0)
        {
            var clusters = ParseK3dClusterJson(result.Output);
            if (clusters.Count > 0)
            {
                return clusters;
            }
        }

        result = await CaptureProcessAsync(k3d, ["cluster", "list", "--no-headers"], cancellationToken).ConfigureAwait(false);
        if (result.ExitCode != 0)
        {
            throw new InvalidOperationException(result.Error.Trim().Length == 0
                ? "Could not list k3d clusters."
                : result.Error.Trim());
        }

        return result.Output
            .Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Select(line => line.Split(' ', StringSplitOptions.RemoveEmptyEntries).FirstOrDefault() ?? string.Empty)
            .Where(name => name.Length > 0)
            .Distinct(StringComparer.Ordinal)
            .Order(StringComparer.Ordinal)
            .ToList();
    }

    private static async Task<string> ReadK3dKubeconfigAsync(string cluster, CancellationToken cancellationToken)
    {
        var k3d = ResolveTool("k3d", ["/opt/homebrew/bin/k3d", "/usr/local/bin/k3d"]);
        var result = await CaptureProcessAsync(k3d, ["kubeconfig", "get", cluster], cancellationToken).ConfigureAwait(false);
        if (result.ExitCode != 0)
        {
            throw new InvalidOperationException(result.Error.Trim().Length == 0
                ? $"Could not read kubeconfig for k3d cluster {cluster}."
                : result.Error.Trim());
        }

        return result.Output;
    }

    private static IReadOnlyList<string> ParseK3dClusterJson(string raw)
    {
        using var document = JsonDocument.Parse(raw);
        if (document.RootElement.ValueKind != JsonValueKind.Array)
        {
            return Array.Empty<string>();
        }

        return document.RootElement
            .EnumerateArray()
            .Select(ClusterNameFromJson)
            .Where(name => !string.IsNullOrWhiteSpace(name))
            .Select(name => name!)
            .Distinct(StringComparer.Ordinal)
            .Order(StringComparer.Ordinal)
            .ToList();
    }

    private static string? ClusterNameFromJson(JsonElement element)
    {
        foreach (var property in new[] { "name", "Name", "clusterName", "ClusterName" })
        {
            if (element.TryGetProperty(property, out var value) && value.ValueKind == JsonValueKind.String)
            {
                return value.GetString();
            }
        }

        return null;
    }

    private static string NormalizeLocalK3dEndpoint(string kubeconfig)
    {
        return kubeconfig
            .Replace("https://0.0.0.0:", "https://127.0.0.1:", StringComparison.Ordinal)
            .Replace("server: https://localhost:", "server: https://127.0.0.1:", StringComparison.Ordinal);
    }

    private static async Task<ProcessResult> CaptureProcessAsync(string fileName, IReadOnlyList<string> arguments, CancellationToken cancellationToken)
    {
        var process = new Process
        {
            StartInfo =
            {
                FileName = fileName,
                RedirectStandardError = true,
                RedirectStandardOutput = true,
                UseShellExecute = false,
                CreateNoWindow = true
            }
        };
        foreach (var argument in arguments)
        {
            process.StartInfo.ArgumentList.Add(argument);
        }

        if (!process.Start())
        {
            throw new InvalidOperationException($"Could not start {Path.GetFileName(fileName)}.");
        }

        var outputTask = process.StandardOutput.ReadToEndAsync(cancellationToken);
        var errorTask = process.StandardError.ReadToEndAsync(cancellationToken);
        await process.WaitForExitAsync(cancellationToken).ConfigureAwait(false);
        return new ProcessResult(process.ExitCode, await outputTask.ConfigureAwait(false), await errorTask.ConfigureAwait(false));
    }

    private void ScheduleSourceRefresh()
    {
        sourceRefreshDebounce?.Cancel();
        sourceRefreshDebounce?.Dispose();
        sourceRefreshDebounce = CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);
        _ = DebouncedSourceRefresh(sourceRefreshDebounce.Token);
    }

    private async Task DebouncedSourceRefresh(CancellationToken cancellationToken)
    {
        try
        {
            await Task.Delay(TimeSpan.FromMilliseconds(1_500), cancellationToken).ConfigureAwait(false);
            await Dispatcher.UIThread.InvokeAsync(() =>
            {
                try
                {
                    var summaries = state.RefreshImportedKubeconfigs();
                    ReloadSessions();
                    StatusLine = $"Auto-refreshed {summaries.Count} kubeconfig source(s).";
                    ScheduleRefresh();
                }
                catch (PodlordException ex)
                {
                    StatusLine = ex.Message;
                    ReloadSources();
                }
            });
        }
        catch (OperationCanceledException)
        {
        }
    }

    private IEnumerable<FlatResourceRow> EventRowsForCurrentFilter(ResourceQuery localQuery)
    {
        var eventQuery = localQuery with
        {
            Kind = null,
            Search = null,
            Limit = 5_000
        };
        return ResourceFilterMatcher.FilterRows(cachedRows.Where(row => row.Kind == "Event"), eventQuery);
    }

    private void UpdateEvents(IEnumerable<FlatResourceRow> rows)
    {
        var expression = eventQuickSearch.Trim();
        var desired = SortEventRows(rows.Where(row => row.Kind == "Event"))
            .Take(512)
            .Select(row => new EventTimelineRow(
                row.Status,
                string.IsNullOrWhiteSpace(row.EventName) ? row.Name : row.EventName,
                string.IsNullOrWhiteSpace(row.EventReason) ? row.Ready : row.EventReason,
                string.IsNullOrWhiteSpace(row.EventObject) ? row.Owner ?? "-" : row.EventObject,
                row.Namespace ?? "cluster",
                row.Age,
                string.IsNullOrWhiteSpace(row.EventMessage) ? row.ImageSummary : row.EventMessage,
                row.Id))
            .Where(timelineRow => expression.Length == 0 || EventMatches(timelineRow, expression))
            .ToList();
        SyncCollection(Events, desired);
    }

    private void UpdateRelationships(IEnumerable<FlatResourceRow> rows)
    {
        var scopedRows = rows.ToList();
        var desired = new List<RelationshipRow>();
        foreach (var cluster in scopedRows.Select(row => row.Cluster).Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal))
        {
            desired.Add(new RelationshipRow("Session", SelectedSession?.DisplayName ?? "active", "=>", "Cluster", cluster, "cluster", "Observed"));
        }

        foreach (var ns in scopedRows.Select(row => row.Namespace).OfType<string>().Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal))
        {
            desired.Add(new RelationshipRow("Cluster", SelectedSession?.ClusterName ?? "-", "=>", "Namespace", ns, ns, "Observed"));
        }

        foreach (var row in scopedRows.Where(row => row.Kind != "Event").OrderBy(row => row.Namespace ?? string.Empty, StringComparer.Ordinal).ThenBy(row => row.Kind, StringComparer.Ordinal).ThenBy(row => row.Name, StringComparer.Ordinal).Take(1_000))
        {
            var ns = row.Namespace ?? "cluster";
            desired.Add(new RelationshipRow(
                row.Owner is { Length: > 0 } owner ? owner.Split('/')[0] : "Namespace",
                row.Owner is { Length: > 0 } ownerName ? ownerName.Split('/').Last() : ns,
                "=>",
                row.Kind,
                row.Name,
                ns,
                row.Status));
        }

        SyncCollection(Relationships, desired);
    }

    private void UpdateGraphNodes(IReadOnlyList<FlatResourceRow> rows)
    {
        GraphNodes.Clear();
        var session = new GraphNodeViewModel("Session", SelectedSession?.DisplayName ?? "active", "cluster", "Observed");
        GraphNodes.Add(session);

        var sessionLookup = state.Snapshot().Sessions
            .ToLookup(s => s.ClusterName, StringComparer.Ordinal);

        foreach (var clusterGroup in rows.GroupBy(row => row.Cluster).OrderBy(group => group.Key, StringComparer.Ordinal))
        {
            var friendly = sessionLookup[clusterGroup.Key].FirstOrDefault()?.DisplayName;
            var clusterLabel = !string.IsNullOrWhiteSpace(friendly) ? friendly! : clusterGroup.Key;
            var clusterNode = new GraphNodeViewModel("Cluster", clusterLabel, "cluster", $"{clusterGroup.Count()} resources");
            session.Children.Add(clusterNode);

            foreach (var namespaceGroup in clusterGroup.GroupBy(row => row.Namespace ?? "cluster").OrderBy(group => group.Key, StringComparer.Ordinal))
            {
                var namespaceNode = new GraphNodeViewModel(
                    namespaceGroup.Key == "cluster" ? "Cluster scope" : "Namespace",
                    namespaceGroup.Key,
                    namespaceGroup.Key,
                    $"{namespaceGroup.Count()} resources");
                clusterNode.Children.Add(namespaceNode);
                AddResourceTree(namespaceNode, namespaceGroup.Where(row => row.Kind != "Event").ToList());
            }
        }

        UpdateGraphSearchMatches(resetToFirstMatch: true);
    }

    private void UpdateGraphSearchMatches(bool resetToFirstMatch)
    {
        foreach (var node in FlattenGraph(GraphNodes))
        {
            node.IsSearchMatch = false;
            node.IsCurrentSearchMatch = false;
        }

        graphSearchMatches.Clear();
        var expression = GraphSearch.Trim();
        if (expression.Length == 0)
        {
            graphSearchIndex = -1;
            SelectedGraphNode = null;
            OnPropertyChanged(nameof(GraphMatchLabel));
            return;
        }

        foreach (var node in FlattenGraph(GraphNodes).Where(node => GraphNodeMatches(node, expression)))
        {
            node.IsSearchMatch = true;
            graphSearchMatches.Add(node);
        }

        graphSearchIndex = graphSearchMatches.Count == 0
            ? -1
            : resetToFirstMatch ? 0 : Math.Clamp(graphSearchIndex, 0, graphSearchMatches.Count - 1);
        SelectCurrentGraphMatch();
    }

    private void SelectCurrentGraphMatch()
    {
        foreach (var node in graphSearchMatches)
        {
            node.IsCurrentSearchMatch = false;
        }

        if (graphSearchIndex >= 0 && graphSearchIndex < graphSearchMatches.Count)
        {
            var node = graphSearchMatches[graphSearchIndex];
            node.IsCurrentSearchMatch = true;
            SelectedGraphNode = node;
            StatusLine = $"Graph match {GraphMatchLabel}: {node.Kind}/{node.Name}.";
        }
        else
        {
            SelectedGraphNode = null;
        }

        OnPropertyChanged(nameof(GraphMatchLabel));
    }

    private void UpdateResourceSearchMatches(bool resetToFirstMatch)
    {
        resourceSearchMatches.Clear();
        var expression = ResourceQuickSearch.Trim();
        if (expression.Length > 0)
        {
            resourceSearchMatches.AddRange(Resources.Where(row => RowMatches(row, expression)));
        }

        resourceSearchIndex = resourceSearchMatches.Count == 0
            ? -1
            : resetToFirstMatch ? 0 : Math.Clamp(resourceSearchIndex, 0, resourceSearchMatches.Count - 1);
        CurrentResourceSearchMatch = resourceSearchIndex >= 0 ? resourceSearchMatches[resourceSearchIndex] : null;
        OnPropertyChanged(nameof(ResourceMatchLabel));
    }

    private void SelectCurrentResourceMatch()
    {
        if (resourceSearchIndex >= 0 && resourceSearchIndex < resourceSearchMatches.Count)
        {
            var row = resourceSearchMatches[resourceSearchIndex];
            CurrentResourceSearchMatch = row;
            SelectedResourceRow = row;
            StatusLine = $"Resource match {ResourceMatchLabel}: {row.Kind}/{row.Name}.";
        }

        OnPropertyChanged(nameof(ResourceMatchLabel));
    }

    private void UpdateEventSearchMatches(bool resetToFirstMatch)
    {
        eventSearchMatches.Clear();
        var expression = EventQuickSearch.Trim();
        if (expression.Length > 0)
        {
            eventSearchMatches.AddRange(Events.Where(row => EventMatches(row, expression)));
        }

        eventSearchIndex = eventSearchMatches.Count == 0
            ? -1
            : resetToFirstMatch ? 0 : Math.Clamp(eventSearchIndex, 0, eventSearchMatches.Count - 1);
        CurrentEventSearchMatch = eventSearchIndex >= 0 ? eventSearchMatches[eventSearchIndex] : null;
        OnPropertyChanged(nameof(EventMatchLabel));
    }

    private void SelectCurrentEventMatch()
    {
        if (eventSearchIndex >= 0 && eventSearchIndex < eventSearchMatches.Count)
        {
            var row = eventSearchMatches[eventSearchIndex];
            CurrentEventSearchMatch = row;
            SelectedEvent = row;
            StatusLine = $"Event match {EventMatchLabel}: {row.Type}/{row.Reason}.";
        }

        OnPropertyChanged(nameof(EventMatchLabel));
    }

    private static IEnumerable<GraphNodeViewModel> FlattenGraph(IEnumerable<GraphNodeViewModel> nodes)
    {
        foreach (var node in nodes)
        {
            yield return node;
            foreach (var child in FlattenGraph(node.Children))
            {
                yield return child;
            }
        }
    }

    private bool GraphNodeMatches(GraphNodeViewModel node, string expression)
    {
        var resource = node.Resource;
        var values = new[]
        {
            node.Kind,
            node.Name,
            node.Namespace,
            node.Status,
            resource?.Cluster ?? string.Empty,
            resource?.ImageSummary ?? string.Empty,
            resource?.Node ?? string.Empty,
            resource?.Owner ?? string.Empty,
            resource is null ? string.Empty : ResourceFilterMatcher.ProblemReason(resource, restartOutlierThreshold)
        };
        return values.Any(value => ResourceFilterMatcher.MatchesText(value, expression));
    }

    private bool RowMatches(FlatResourceRow row, string expression)
    {
        var values = new[]
        {
            row.Id,
            row.Kind,
            row.Name,
            row.Namespace ?? "cluster",
            row.Cluster,
            row.Status,
            row.Ready,
            row.Restarts.ToString(),
            row.Node ?? string.Empty,
            row.ImageSummary,
            row.Owner ?? string.Empty,
            row.Age,
            ResourceFilterMatcher.ProblemReason(row, restartOutlierThreshold)
        };
        return values.Any(value => ResourceFilterMatcher.MatchesText(value, expression));
    }

    private static bool EventMatches(EventTimelineRow row, string expression)
    {
        var values = new[] { row.Type, row.Name, row.Reason, row.Object, row.Namespace, row.Age, row.Message };
        return values.Any(value => ResourceFilterMatcher.MatchesText(value, expression));
    }

    private static void AddResourceTree(GraphNodeViewModel parent, IReadOnlyList<FlatResourceRow> rows)
    {
        var nodes = rows
            .OrderBy(row => row.Kind, StringComparer.Ordinal)
            .ThenBy(row => row.Name, StringComparer.Ordinal)
            .ToDictionary(
                row => $"{row.Kind}/{row.Name}",
                row => new GraphNodeViewModel(row.Kind, row.Name, row.Namespace ?? "cluster", row.Status, row),
                StringComparer.Ordinal);
        var childKeys = new HashSet<string>(StringComparer.Ordinal);

        foreach (var row in rows)
        {
            if (row.Owner is not { Length: > 0 } owner || !nodes.TryGetValue($"{row.Kind}/{row.Name}", out var child))
            {
                continue;
            }

            if (!nodes.TryGetValue(owner, out var ownerNode))
            {
                ownerNode = new GraphNodeViewModel(owner.Split('/')[0], owner.Split('/').Last(), row.Namespace ?? "cluster", "Owner");
                nodes[owner] = ownerNode;
            }

            ownerNode.Children.Add(child);
            childKeys.Add($"{row.Kind}/{row.Name}");
        }

        foreach (var node in nodes
                     .Where(pair => !childKeys.Contains(pair.Key))
                     .Select(pair => pair.Value)
                     .OrderBy(node => node.Kind, StringComparer.Ordinal)
                     .ThenBy(node => node.Name, StringComparer.Ordinal))
        {
            parent.Children.Add(node);
        }
    }

    private static IBrush UnitProblemSevere => AppThemeCatalog.StatusBrush("CRITICAL");
    private static IBrush UnitProblemWarning => AppThemeCatalog.StatusBrush("WARNING");
    private static IBrush UnitFreshChange => AppThemeCatalog.StatusBrush("HEALTHY");
    private static readonly IBrush RadarTerrainStone = SolidColorBrush.Parse("#6B7378");
    private static readonly IBrush RadarTerrainForest = SolidColorBrush.Parse("#2E5941");
    private static readonly IBrush RadarTerrainGrass = SolidColorBrush.Parse("#4E6A43");
    private static readonly IBrush RadarTerrainDirt = SolidColorBrush.Parse("#665A3F");
    private static readonly IBrush RadarTerrainSand = SolidColorBrush.Parse("#7D7048");
    private static readonly IBrush RadarTerrainShallowWater = SolidColorBrush.Parse("#286473");
    private static readonly IBrush RadarTerrainDeepWater = SolidColorBrush.Parse("#1B4357");
    private static readonly IBrush RadarCliffEdge = SolidColorBrush.Parse("#8E8450");
    private static readonly IBrush RadarShoreEdge = SolidColorBrush.Parse("#0A2A37");
    private static readonly IBrush RadarFilteredBrush = SolidColorBrush.Parse("#37454A");
    private static readonly IBrush RadarFilteredBorderBrush = SolidColorBrush.Parse("#60727A");
    private static readonly IBrush RadarAnnounceSuccess = SolidColorBrush.Parse("#7DFFC3");
    private static readonly IBrush RadarAnnounceWarning = SolidColorBrush.Parse("#FFE866");
    private static readonly IBrush RadarAnnounceDanger = SolidColorBrush.Parse("#FF5C5C");
    private static readonly IBrush[] RadarLifeBrushes =
    [
        SolidColorBrush.Parse("#4FC3A1"),
        SolidColorBrush.Parse("#6FBF7D"),
        SolidColorBrush.Parse("#77D7C8"),
        SolidColorBrush.Parse("#5AA7D6"),
        SolidColorBrush.Parse("#82C977")
    ];
    private static IBrush RadarBrush(FlatResourceRow row, string problem, bool isFilteredOut, string colorAlert)
    {
        if (isFilteredOut)
        {
            return RadarFilteredBrush;
        }

        if (!colorAlert.Equals("none", StringComparison.OrdinalIgnoreCase))
        {
            return AlertColorBrush(colorAlert, row, problem);
        }

        return RadarTerrainBrush(row);
    }

    private static IBrush RadarTerrainBrush(FlatResourceRow row)
    {
        return row.Kind switch
        {
            "Cluster" or "Node" or "PersistentVolume" or "StorageClass" or "CustomResourceDefinition" or "GatewayClass" => RadarTerrainStone,
            "Namespace" => RadarTerrainForest,
            "ConfigMap" or "Secret" or "ServiceAccount" or "PersistentVolumeClaim" => RadarTerrainGrass,
            "Deployment" or "StatefulSet" or "DaemonSet" or "Job" or "CronJob" or "ReplicaSet" => RadarTerrainDirt,
            "Pod" or "Service" or "EndpointSlice" => RadarTerrainSand,
            "Ingress" or "Gateway" or "HTTPRoute" or "GRPCRoute" or "NetworkPolicy" => RadarTerrainShallowWater,
            "Event" => RadarTerrainDeepWater,
            _ => RadarTerrainGrass
        };
    }

    private static IBrush AlertColorBrush(string color, FlatResourceRow row, string problem)
    {
        if (TryParseBrush(color, out var brush))
        {
            return brush;
        }

        return color.ToLowerInvariant() switch
        {
            "status" => problem.Length > 0
                ? IsSevere(row, problem) ? UnitProblemSevere : UnitProblemWarning
                : IsRecentlyChanged(row) ? UnitFreshChange : RadarTerrainBrush(row),
            "fresh" or "cyan" => UnitFreshChange,
            "green" => AppThemeCatalog.StatusBrush("HEALTHY"),
            "amber" => AppThemeCatalog.StatusBrush("WARNING"),
            "red" => AppThemeCatalog.StatusBrush("CRITICAL"),
            "blue" => SolidColorBrush.Parse("#58A6FF"),
            "violet" => SolidColorBrush.Parse("#B58CFF"),
            _ => RadarTerrainBrush(row)
        };
    }

    private static bool TryParseBrush(string value, out IBrush brush)
    {
        brush = Brushes.Transparent;
        if (!value.StartsWith('#') || value.Length is not (7 or 9))
        {
            return false;
        }

        try
        {
            brush = SolidColorBrush.Parse(value);
            return true;
        }
        catch (FormatException)
        {
            return false;
        }
    }

    private static IBrush RadarBorderBrush(FlatResourceRow row, string problem, bool eventShallow, bool isFilteredOut, bool colorAlert)
    {
        if (isFilteredOut)
        {
            return RadarFilteredBorderBrush;
        }

        if (colorAlert && problem.Length > 0)
        {
            return IsSevere(row, problem) ? UnitProblemSevere : UnitProblemWarning;
        }

        return eventShallow ? RadarCliffEdge : RadarShoreEdge;
    }

    private static IBrush RadarAnnounceBrush(FlatResourceRow row, string problem, bool isFilteredOut, bool colorAlert)
    {
        if (isFilteredOut)
        {
            return RadarFilteredBorderBrush;
        }

        if (colorAlert && problem.Length > 0)
        {
            return IsSevere(row, problem) ? RadarAnnounceDanger : RadarAnnounceWarning;
        }

        return RadarAnnounceSuccess;
    }

    private static bool IsRecentlyChanged(FlatResourceRow row)
    {
        // Show fresh-change flag for resources whose LastChange parses as < 30s.
        var raw = row.LastChange;
        if (string.IsNullOrWhiteSpace(raw))
        {
            return false;
        }

        var trimmed = raw.Trim();
        if (trimmed.EndsWith('s') && int.TryParse(trimmed[..^1], out var seconds))
        {
            return seconds <= 30;
        }

        return string.Equals(trimmed, "now", StringComparison.OrdinalIgnoreCase);
    }

    private static bool IsSevere(FlatResourceRow row, string problem)
    {
        return problem.Contains("Crash", StringComparison.OrdinalIgnoreCase)
               || problem.Contains("Error", StringComparison.OrdinalIgnoreCase)
               || problem.Contains("Failed", StringComparison.OrdinalIgnoreCase)
               || problem.Contains("Unavailable", StringComparison.OrdinalIgnoreCase)
               || row.Status is "CrashLoopBackOff" or "CreateContainerConfigError" or "CreateContainerError" or "ErrImagePull" or "Error" or "Failed" or "ImagePullBackOff" or "NotReady" or "OOMKilled" or "Unavailable";
    }

    private string RadarMetrics(FlatResourceRow row)
    {
        var issue = ResourceFilterMatcher.ProblemReason(row, restartOutlierThreshold);
        var parts = new[]
        {
            $"Status: {row.Status}",
            $"Ready: {row.Ready}",
            $"Restarts: {row.Restarts}",
            $"CPU: {row.CpuSummaryDisplay}",
            $"Memory: {row.MemorySummaryDisplay}",
            $"Network: {row.NetworkDisplay}",
            $"Storage: {row.StorageDisplay}",
            $"Node: {row.Node ?? "-"}",
            $"Image: {row.ImageSummary}",
            $"Owner: {row.Owner ?? "-"}",
            issue.Length == 0 ? "Issue: none" : $"Issue: {issue}"
        };
        return string.Join(Environment.NewLine, parts);
    }

    private void SaveSettings(Settings settings)
    {
        var previousLanguage = state.Settings().Language;
        state.SaveSettings(settings);
        AppThemeCatalog.Apply(settings.Theme, settings.PixelEffectIntensity, settings.ThemeVariant);
        OnPropertyChanged(nameof(ThemeSetting));
        OnPropertyChanged(nameof(ThemeVariantSetting));
        OnPropertyChanged(nameof(LanguageSetting));
        OnPropertyChanged(nameof(GraphicsQualitySetting));
        OnPropertyChanged(nameof(AnimationIntensitySetting));
        OnPropertyChanged(nameof(AnimationIntensityLabel));
        OnPropertyChanged(nameof(RadarWaterEnabledSetting));
        OnPropertyChanged(nameof(IsRadarWaterVisible));
        OnPropertyChanged(nameof(RadarWaterSpeedSetting));
        OnPropertyChanged(nameof(RadarWaterSpeedPercent));
        OnPropertyChanged(nameof(RadarWaterSpeedLabel));
        OnPropertyChanged(nameof(RadarAutoFollowAlertsSetting));
        OnPropertyChanged(nameof(InactiveSyncSetting));
        OnPropertyChanged(nameof(InactiveSyncDescription));
        OnPropertyChanged(nameof(RequestHardLimitSetting));
        OnPropertyChanged(nameof(RequestHardLimitDescription));
        OnPropertyChanged(nameof(WorkspaceRestoreSetting));
        OnPropertyChanged(nameof(TelemetrySetting));
        OnPropertyChanged(nameof(ScreensaverSetting));
        if (!string.Equals(previousLanguage, settings.Language, StringComparison.OrdinalIgnoreCase))
        {
            NotifyLocalizedTextChanged();
        }

        StatusLine = T("status.settingsSaved");
    }

    internal string T(string key)
    {
        return PodlordLocalizer.Text(key, state.Settings().Language);
    }

    private string TF(string key, params object[] values)
    {
        return string.Format(CultureInfo.CurrentCulture, T(key), values);
    }

    private void NotifyLocalizedTextChanged()
    {
        foreach (var property in LocalizedProperties)
        {
            OnPropertyChanged(property);
        }
    }

    private static readonly string[] LocalizedProperties =
    [
        nameof(ResourceLogoTitle),
        nameof(ResourceLogoMessage),
        nameof(NavSearchText),
        nameof(NavResourcesText),
        nameof(NavGraphText),
        nameof(NavEventsText),
        nameof(NavPortsText),
        nameof(NavSettingsText),
        nameof(SourcesTitleText),
        nameof(ImportPlaceholderText),
        nameof(ImportActionText),
        nameof(ImportFileTipText),
        nameof(ManageActionText),
        nameof(FiltersTitleText),
        nameof(ProblemsText),
        nameof(ActivityText),
        nameof(SavedFiltersText),
        nameof(SearchSavedFiltersText),
        nameof(FilterNamePlaceholderText),
        nameof(SaveActionText),
        nameof(DeleteActionText),
        nameof(DuplicateActionText),
        nameof(AddActionText),
        nameof(ClearActionText),
        nameof(CloseActionText),
        nameof(PortActionText),
        nameof(ApplyServerSideActionText),
        nameof(ResetActionText),
        nameof(SettingsTitleText),
        nameof(SettingsAlertsText),
        nameof(SettingsSourcesText),
        nameof(SettingsAppearanceText),
        nameof(SettingsGraphicsText),
        nameof(SettingsSyncText),
        nameof(SettingsWorkspaceText),
        nameof(SettingsPrivacyText),
        nameof(SettingsDiagnosticsText),
        nameof(ThemeText),
        nameof(VariantText),
        nameof(ThemeIntensityText),
        nameof(ThemeHelpText),
        nameof(VariantHelpText),
        nameof(ThemeIntensityHelpText),
        nameof(LanguageText),
        nameof(LanguageHelpText),
        nameof(RadarWaterText),
        nameof(RadarWaterHelpText),
        nameof(RadarWaterSpeedText),
        nameof(RadarWaterSpeedHelpText),
        nameof(AnimationIntensityText),
        nameof(AnimationHelpText),
        nameof(RadarAutoFollowText),
        nameof(RadarAutoFollowHelpText),
        nameof(RadarScreensaverText),
        nameof(GraphicsHelpText),
        nameof(InactiveBackgroundSyncText),
        nameof(RequestHardLimitText),
        nameof(RequestHardLimitHelpText),
        nameof(WorkspaceRestoreText),
        nameof(WorkspaceRestoreHelpText),
        nameof(TelemetryText),
        nameof(TelemetryHelpText),
        nameof(RequestAuditTitleText),
        nameof(AlertActiveText),
        nameof(AlertTypeText),
        nameof(AlertNameText),
        nameof(AlertDescriptionText),
        nameof(AlertWhenText),
        nameof(AlertActionsText),
        nameof(AlertSoundText),
        nameof(AlertMatchersText),
        nameof(AlertOrMatcherText),
        nameof(AlertMatcherBlockHelpText),
        nameof(AlertAndText),
        nameof(AlertRemoveMatcherBlockText),
        nameof(AlertRemoveMatcherText),
        nameof(AlertColorText),
        nameof(AlertNoColorText),
        nameof(AlertStatusColorText),
        nameof(AlertAnimationText),
        nameof(AlertZoomText),
        nameof(AlertPreviewZoomText),
        nameof(AlertSoundSearchText),
        nameof(AlertPreviewSoundText),
        nameof(AlertAuthorText),
        nameof(AlertSourceText),
        nameof(AlertAssetText),
        nameof(AudioMuteText),
        nameof(FilterSearchOrCustomText),
        nameof(CustomValuesText),
        nameof(FilterSyntaxHelpText),
        nameof(InspectorOverviewText),
        nameof(InspectorYamlText),
        nameof(InspectorEventsText),
        nameof(InspectorLinksText),
        nameof(InspectorLogsText),
        nameof(InspectorValuesText),
        nameof(YamlTabIndentText),
        nameof(PortForwardTitleText),
        nameof(PortContainerPortText),
        nameof(PortLocalPortText),
        nameof(PortContainerPortTipText),
        nameof(PortLocalPortTipText)
    ];

    private static string InactiveSyncLabel(int minutes)
    {
        return minutes <= 0 ? "disabled" : $"{minutes}m";
    }

    private static string SortGlyph(string activeColumn, ResourceSortDirection direction, string column)
    {
        if (!activeColumn.Equals(column, StringComparison.Ordinal) || direction == ResourceSortDirection.None)
        {
            return string.Empty;
        }

        return direction == ResourceSortDirection.Ascending ? "▲" : "▼";
    }

    private static int ParseInactiveSyncMinutes(string? value)
    {
        var text = value?.Trim().ToLowerInvariant() ?? string.Empty;
        if (text.Length == 0 || text == "disabled" || text == "off" || text == "0" || text == "0m")
        {
            return 0;
        }

        text = text.TrimEnd('m');
        return int.TryParse(text, out var minutes) && minutes is 1 or 5 or 10 or 20 or 30 or 60
            ? minutes
            : 0;
    }

    private static string RequestHardLimitLabel(int perMinute)
    {
        return perMinute <= 0 ? "none" : $"{perMinute}/min";
    }

    private static int ParseRequestHardLimitPerMinute(string? value)
    {
        var text = value?.Trim().ToLowerInvariant() ?? string.Empty;
        if (text.Length == 0 || text is "none" or "disabled" or "off" or "0" or "0/min")
        {
            return 0;
        }

        text = text
            .Replace("requests", string.Empty, StringComparison.Ordinal)
            .Replace("request", string.Empty, StringComparison.Ordinal)
            .Replace("req", string.Empty, StringComparison.Ordinal)
            .Replace("/minute", string.Empty, StringComparison.Ordinal)
            .Replace("per minute", string.Empty, StringComparison.Ordinal)
            .Replace("/min", string.Empty, StringComparison.Ordinal)
            .Replace("min", string.Empty, StringComparison.Ordinal)
            .Replace("/", string.Empty, StringComparison.Ordinal)
            .Trim();
        return int.TryParse(text, out var limit) ? Math.Clamp(limit, 0, 60_000) : 0;
    }

    private void ExecuteCommand(string command)
    {
        if (string.IsNullOrWhiteSpace(command))
        {
            return;
        }

        var normalized = command.Trim().ToLowerInvariant();
        if (normalized.Contains("resource", StringComparison.Ordinal))
        {
            SelectWorkspace("resources");
        }
        else if (normalized.Contains("graph", StringComparison.Ordinal) || normalized.Contains("diagram", StringComparison.Ordinal))
        {
            SelectWorkspace("graph");
        }
        else if (normalized.Contains("event", StringComparison.Ordinal))
        {
            SelectWorkspace("events");
        }
        else if (normalized.Contains("source", StringComparison.Ordinal) || normalized.Contains("session", StringComparison.Ordinal))
        {
            OpenSourcesSettings();
        }
        else if (normalized.Contains("port", StringComparison.Ordinal))
        {
            PreparePortForward();
        }
        else if (normalized.Contains("setting", StringComparison.Ordinal))
        {
            SelectWorkspace("settings");
        }
        else if (normalized.Contains("problem", StringComparison.Ordinal))
        {
            ProblemsOnly = !ProblemsOnly;
        }
        else if (normalized.Contains("k3d", StringComparison.Ordinal))
        {
            _ = ImportK3dNowAsync();
        }
        else
        {
            StatusLine = $"Unknown command: {command}";
            return;
        }

        IsCommandPaletteOpen = false;
    }

    private void UpdateCommandSuggestions()
    {
        var all = new[]
        {
            "Open Resources",
            "Open Graph",
            "Open Events",
            "Open Sources Settings",
            "Open Port Forwards",
            "Open Settings",
            "Toggle Problems",
            "Import K3D"
        };
        var query = CommandText.Trim();
        CommandSuggestions.Clear();
        foreach (var command in all.Where(command => query.Length == 0 || command.Contains(query, StringComparison.OrdinalIgnoreCase)).Take(8))
        {
            CommandSuggestions.Add(command);
        }
    }

    private void ApplyLocalFilter()
    {
        var localQuery = BuildLocalQuery();
        var now = DateTimeOffset.Now;
        var filteredForViews = SortRows(ResourceFilterMatcher.FilterRows(cachedRows, localQuery with { Limit = 5_000 }))
            .ToList();
        var visibleBaseRows = filteredForViews
            .Take(ResourceFilterMatcher.NormalizeLimit(localQuery.Limit))
            .ToList();
        var visibleResourceIds = visibleBaseRows.Select(row => row.Id).ToHashSet(StringComparer.Ordinal);
        EvaluateAlertRules();
        var visibleRows = visibleBaseRows
            .Select(row =>
            {
                var announce = ShouldAnnounceResourceRow(row, visibleResourceIds, now);
                return row with
                {
                    IsAnnouncing = announce,
                    AlertAnimation = announce ? AlertAnimationFor(row.Id) : string.Empty,
                    AlertColor = AlertColorFor(row.Id, isFilteredOut: false)
                };
            })
            .ToList();

        SyncResourcesPreservingSelection(visibleRows);
        SyncPreviousVisibleResourceAlertIds(visibleResourceIds);

        UpdateEvents(EventRowsForCurrentFilter(localQuery));
        UpdateRelationships(filteredForViews);
        UpdateGraphNodes(visibleRows);
        UpdateRadarFromCache(localQuery);
        UpdatePulseLayer(cachedRows, filteredForViews);
        UpdateResourceSearchMatches(resetToFirstMatch: true);
        UpdateEventSearchMatches(resetToFirstMatch: true);
        OnPropertyChanged(nameof(ResourceCountLabel));
        OnPropertyChanged(nameof(FooterLine));
        OnPropertyChanged(nameof(IsInitialLoading));
        NotifyResourceLogoStateChanged();
        StatusLine = $"{ResourceCountLabel}; {Failures.Count} warning(s); {LastSyncedLabel}.";
    }

    private void EvaluateAlertRules()
    {
        var rules = new List<AlertRule>(AlertRules.Count);
        var enabledRuleIds = new HashSet<string>(StringComparer.Ordinal);
        foreach (var rule in AlertRules)
        {
            var resolved = rule.ToRule();
            rules.Add(resolved);
            if (resolved.Enabled)
            {
                enabledRuleIds.Add(resolved.Id);
            }
        }
        var evaluations = AlertRuleEvaluator.Evaluate(cachedRows, rules);
        var now = DateTimeOffset.Now;
        var rowsById = new Dictionary<string, FlatResourceRow>(cachedRows.Count, StringComparer.Ordinal);
        foreach (var row in cachedRows)
        {
            rowsById[row.Id] = row;
        }
        activeAlertActionsByResourceId.Clear();
        activeRadarAlertMatches.Clear();
        RemoveExpiredAlertDurations(enabledRuleIds, rowsById, now);
        ClearSoundDeduplicationForInactiveRules(enabledRuleIds);
        foreach (var rule in AlertRules)
        {
            rule.SetActiveSummary(string.Empty);
        }

        var activeAlerts = new List<ActiveAlertRow>();
        var currentAlertRuleMatches = new HashSet<(string RuleId, string RowId)>();
        var currentAlertRuleRowStates = new Dictionary<(string RuleId, string RowId), string>();
        foreach (var evaluation in evaluations)
        {
            var activeRows = ApplyAlertEvaluation(evaluation, rowsById, now, currentAlertRuleMatches, currentAlertRuleRowStates);
            if (activeRows.Count == 0)
            {
                lastAlertSoundKeysByRuleId.Remove(evaluation.Rule.Id);
                continue;
            }

            MaybePlayAlertSound(evaluation.Rule, activeRows);
            if (evaluation.Rule.Actions.RadarFocus || evaluation.Rule.Actions.RadarZoom)
            {
                activeRadarAlertMatches.Add(new ActiveRadarAlertMatch(
                    evaluation.Rule.Id,
                    activeRows.ToList(),
                    evaluation.Rule.Actions));
            }
            AlertRules.FirstOrDefault(rule => rule.Id.Equals(evaluation.Rule.Id, StringComparison.Ordinal))
                ?.SetActiveSummary(AlertSummary(activeRows));
            activeAlerts.Add(new ActiveAlertRow(
                evaluation.Rule.Name,
                AlertSummary(activeRows),
                ActionSummary(evaluation.Rule.Actions),
                AlertSoundCatalog.Resolve(evaluation.Rule.SoundId).Name));
        }

        SyncCollection(ActiveAlerts, activeAlerts);
        previousAlertRuleMatches.Clear();
        foreach (var key in currentAlertRuleMatches)
        {
            previousAlertRuleMatches.Add(key);
        }
        previousAlertRuleRowStates.Clear();
        foreach (var (key, value) in currentAlertRuleRowStates)
        {
            previousAlertRuleRowStates[key] = value;
        }
    }

    private void ClearSoundDeduplicationForInactiveRules(IReadOnlySet<string> enabledRuleIds)
    {
        foreach (var ruleId in lastAlertSoundKeysByRuleId.Keys
                     .Where(ruleId => !enabledRuleIds.Contains(ruleId))
                     .ToArray())
        {
            lastAlertSoundKeysByRuleId.Remove(ruleId);
        }
    }

    private void MaybePlayAlertSound(AlertRule rule, IReadOnlyList<FlatResourceRow> rows)
    {
        if (!rule.Actions.PlaySound || rows.Count < Math.Max(1, rule.Actions.SoundMinimumMatches))
        {
            lastAlertSoundKeysByRuleId.Remove(rule.Id);
            return;
        }

        var key = AlertRowsStateKey(rows);
        if (lastAlertSoundKeysByRuleId.TryGetValue(rule.Id, out var previous)
            && previous.Equals(key, StringComparison.Ordinal))
        {
            return;
        }

        lastAlertSoundKeysByRuleId[rule.Id] = key;
        TryPlayAutomaticSound(rule.SoundId, priority: true);
    }

    private static string AlertSummary(IReadOnlyList<FlatResourceRow> rows)
    {
        return rows.Count == 0
            ? "no matches"
            : $"{rows.Count} match(es): {string.Join(", ", rows.Take(3).Select(row => $"{row.Kind}/{row.Name}"))}";
    }

    private string AlertRowsStateKey(IEnumerable<FlatResourceRow> rows)
    {
        var ordered = rows is IList<FlatResourceRow> list ? new List<FlatResourceRow>(list) : rows.ToList();
        ordered.Sort((left, right) => string.CompareOrdinal(left.Id, right.Id));
        if (ordered.Count == 0)
        {
            return string.Empty;
        }
        var builder = new System.Text.StringBuilder(ordered.Count * 64);
        for (var index = 0; index < ordered.Count; index++)
        {
            if (index > 0)
            {
                builder.Append('|');
            }
            var row = ordered[index];
            builder.Append(row.Id).Append(':').Append(AlertRowStateKey(row));
        }
        return builder.ToString();
    }

    private string AlertRowStateKey(FlatResourceRow row)
    {
        var problem = ResourceFilterMatcher.ProblemReason(row, restartOutlierThreshold);
        return $"{row.Status}:{row.Ready}:{row.Restarts}:{row.LastChange}:{problem}";
    }

    private void AddAlertAction(FlatResourceRow row, AlertRuleActions actions)
    {
        activeAlertActionsByResourceId[row.Id] = activeAlertActionsByResourceId.TryGetValue(row.Id, out var existing)
            ? MergeAlertActions(existing, actions)
            : actions;
    }

    private IReadOnlyList<FlatResourceRow> ApplyAlertEvaluation(
        AlertEvaluation evaluation,
        IReadOnlyDictionary<string, FlatResourceRow> rowsById,
        DateTimeOffset now,
        ISet<(string RuleId, string RowId)> currentAlertRuleMatches,
        IDictionary<(string RuleId, string RowId), string> currentAlertRuleRowStates)
    {
        var currentMatches = evaluation.Matches.ToDictionary(row => row.Id, row => row, StringComparer.Ordinal);
        ClearStaleHoldsForDisabledDurations(evaluation.Rule);
        foreach (var row in currentMatches.Values)
        {
            var key = (evaluation.Rule.Id, row.Id);
            var rowState = AlertRowStateKey(row);
            currentAlertRuleMatches.Add(key);
            currentAlertRuleRowStates[key] = rowState;
            AddAlertAction(row, MatchingActions(evaluation.Rule.Actions));
            var changedMatch = !previousAlertRuleRowStates.TryGetValue(key, out var previousState)
                               || !previousState.Equals(rowState, StringComparison.Ordinal);
            StartAlertActionHolds(evaluation.Rule, row, now, shouldStart: !previousAlertRuleMatches.Contains(key) || changedMatch);
        }

        AddHeldAlertActions(evaluation.Rule.Id, rowsById, currentMatches, now, alertDurationUntilByRuleResource, evaluation.Rule.Actions);
        AddHeldAlertActions(evaluation.Rule.Id, rowsById, currentMatches, now, alertColorUntilByRuleResource, ColorOnly(evaluation.Rule.Actions));
        AddHeldAlertActions(evaluation.Rule.Id, rowsById, currentMatches, now, alertAnimationUntilByRuleResource, AnimationOnly(evaluation.Rule.Actions));
        return currentMatches.Values.ToList();
    }

    private void ClearStaleHoldsForDisabledDurations(AlertRule rule)
    {
        if (DurationFrom(rule.Until.Mode, rule.Until.Duration) <= TimeSpan.Zero)
        {
            ClearAlertHoldForRule(rule.Id, alertDurationUntilByRuleResource);
        }
        if (!rule.Actions.RadarColor || DurationFrom(rule.Actions.RadarColorUntilMode, rule.Actions.RadarColorUntilDuration) <= TimeSpan.Zero)
        {
            ClearAlertHoldForRule(rule.Id, alertColorUntilByRuleResource);
        }
        if (!rule.Actions.RadarBlink || DurationFrom(rule.Actions.RadarAnimationUntilMode, rule.Actions.RadarAnimationUntilDuration) <= TimeSpan.Zero)
        {
            ClearAlertHoldForRule(rule.Id, alertAnimationUntilByRuleResource);
        }
    }

    private static void ClearAlertHoldForRule(
        string ruleId,
        IDictionary<(string RuleId, string RowId), DateTimeOffset> target)
    {
        foreach (var key in target.Keys.Where(key => key.RuleId.Equals(ruleId, StringComparison.Ordinal)).ToArray())
        {
            target.Remove(key);
        }
    }

    private void StartAlertActionHolds(AlertRule rule, FlatResourceRow row, DateTimeOffset now, bool shouldStart)
    {
        if (!shouldStart)
        {
            return;
        }

        var startedTimedHold = StartAlertHold(rule.Id, row.Id, DurationFrom(rule.Until.Mode, rule.Until.Duration), now, alertDurationUntilByRuleResource);
        if (rule.Actions.RadarColor)
        {
            startedTimedHold |= StartAlertHold(rule.Id, row.Id, DurationFrom(rule.Actions.RadarColorUntilMode, rule.Actions.RadarColorUntilDuration), now, alertColorUntilByRuleResource);
        }
        if (rule.Actions.RadarBlink)
        {
            startedTimedHold |= StartAlertHold(rule.Id, row.Id, DurationFrom(rule.Actions.RadarAnimationUntilMode, rule.Actions.RadarAnimationUntilDuration), now, alertAnimationUntilByRuleResource);
        }

        if (startedTimedHold)
        {
            StartAlertAnimationExpiryTimer();
        }
    }

    private static bool StartAlertHold(
        string ruleId,
        string rowId,
        TimeSpan duration,
        DateTimeOffset now,
        IDictionary<(string RuleId, string RowId), DateTimeOffset> target)
    {
        if (duration > TimeSpan.Zero)
        {
            return target.TryAdd((ruleId, rowId), now.Add(duration));
        }

        return false;
    }

    private void AddHeldAlertActions(
        string ruleId,
        IReadOnlyDictionary<string, FlatResourceRow> rowsById,
        IDictionary<string, FlatResourceRow> activeRows,
        DateTimeOffset now,
        IDictionary<(string RuleId, string RowId), DateTimeOffset> source,
        AlertRuleActions heldActions)
    {
        if (!heldActions.RadarColor && !heldActions.RadarBlink && !heldActions.RadarZoom && !heldActions.RadarFocus && !heldActions.PlaySound)
        {
            return;
        }

        foreach (var ((heldRuleId, rowId), until) in source.ToArray())
        {
            if (!heldRuleId.Equals(ruleId, StringComparison.Ordinal))
            {
                continue;
            }

            if (until <= now || !rowsById.TryGetValue(rowId, out var cachedRow))
            {
                source.Remove((heldRuleId, rowId));
                continue;
            }

            activeRows.TryAdd(rowId, cachedRow);
            AddAlertAction(cachedRow, heldActions);
        }
    }

    private bool RemoveExpiredAlertDurations(
        IReadOnlySet<string> enabledRuleIds,
        IReadOnlyDictionary<string, FlatResourceRow> rowsById,
        DateTimeOffset now)
    {
        var removed = false;
        foreach (var ((ruleId, rowId), until) in alertDurationUntilByRuleResource.ToArray())
        {
            if (!enabledRuleIds.Contains(ruleId) || until <= now || !rowsById.ContainsKey(rowId))
            {
                alertDurationUntilByRuleResource.Remove((ruleId, rowId));
                removed = true;
            }
        }
        foreach (var ((ruleId, rowId), until) in alertColorUntilByRuleResource.ToArray())
        {
            if (!enabledRuleIds.Contains(ruleId) || until <= now || !rowsById.ContainsKey(rowId))
            {
                alertColorUntilByRuleResource.Remove((ruleId, rowId));
                removed = true;
            }
        }
        foreach (var ((ruleId, rowId), until) in alertAnimationUntilByRuleResource.ToArray())
        {
            if (!enabledRuleIds.Contains(ruleId) || until <= now || !rowsById.ContainsKey(rowId))
            {
                alertAnimationUntilByRuleResource.Remove((ruleId, rowId));
                removed = true;
            }
        }

        return removed;
    }

    private void ClearAlertDurationForRule(string ruleId)
    {
        foreach (var key in alertDurationUntilByRuleResource.Keys.Where(key => key.RuleId.Equals(ruleId, StringComparison.Ordinal)).ToArray())
        {
            alertDurationUntilByRuleResource.Remove(key);
        }
        foreach (var key in alertColorUntilByRuleResource.Keys.Where(key => key.RuleId.Equals(ruleId, StringComparison.Ordinal)).ToArray())
        {
            alertColorUntilByRuleResource.Remove(key);
        }
        foreach (var key in alertAnimationUntilByRuleResource.Keys.Where(key => key.RuleId.Equals(ruleId, StringComparison.Ordinal)).ToArray())
        {
            alertAnimationUntilByRuleResource.Remove(key);
        }
    }

    private static AlertRuleActions ColorOnly(AlertRuleActions actions)
    {
        return actions with
        {
            RadarFocus = false,
            RadarZoom = false,
            RadarBlink = false,
            PlaySound = false,
            RadarZoomPercent = 0
        };
    }

    private static AlertRuleActions MatchingActions(AlertRuleActions actions)
    {
        return actions with
        {
            RadarColor = actions.RadarColor && !IsFiniteAlertMode(actions.RadarColorUntilMode),
            RadarBlink = actions.RadarBlink && !IsFiniteAlertMode(actions.RadarAnimationUntilMode)
        };
    }

    private static AlertRuleActions AnimationOnly(AlertRuleActions actions)
    {
        return actions with
        {
            RadarFocus = false,
            RadarZoom = false,
            RadarColor = false,
            PlaySound = false,
            RadarZoomPercent = 0
        };
    }

    private static TimeSpan DurationFrom(string mode, string expression)
    {
        if (mode.Equals(AlertUntilModes.Once, StringComparison.OrdinalIgnoreCase))
        {
            return TimeSpan.FromMilliseconds(1350);
        }

        if (mode.Equals(AlertUntilModes.Duration, StringComparison.OrdinalIgnoreCase))
        {
            return ResourceFilterMatcher.ParseHumanDuration(expression) ?? TimeSpan.Zero;
        }

        return TimeSpan.Zero;
    }

    private static bool IsFiniteAlertMode(string mode)
    {
        return mode.Equals(AlertUntilModes.Once, StringComparison.OrdinalIgnoreCase)
               || mode.Equals(AlertUntilModes.Duration, StringComparison.OrdinalIgnoreCase);
    }

    private static string ActionSummary(AlertRuleActions actions)
    {
        var values = new[]
        {
            actions.RadarColor ? $"color:{actions.RadarColorValue}" : "",
            actions.RadarBlink ? $"animation:{actions.RadarAnimation}" : "",
            actions.RadarZoom ? $"zoom:{actions.RadarZoomPercent}%" : "",
            actions.PlaySound ? actions.SoundMinimumMatches > 1 ? $"sound>={actions.SoundMinimumMatches}" : "sound" : ""
        }.Where(value => value.Length > 0);
        return string.Join(", ", values);
    }

    private static AlertRuleActions MergeAlertActions(AlertRuleActions left, AlertRuleActions right)
    {
        var useRightColor = AlertColorPriority(right) >= AlertColorPriority(left);
        var useRightAnimation = AlertAnimationPriority(right) >= AlertAnimationPriority(left);
        return new AlertRuleActions(
            RadarFocus: left.RadarFocus || right.RadarFocus,
            RadarZoom: left.RadarZoom || right.RadarZoom,
            RadarBlink: left.RadarBlink || right.RadarBlink,
            RadarColor: left.RadarColor || right.RadarColor,
            RadarColorMode: right.RadarColorMode.Length > 0 ? right.RadarColorMode : left.RadarColorMode,
            HealthSegment: false,
            PlaySound: left.PlaySound || right.PlaySound,
            RadarColorValue: useRightColor ? right.RadarColorValue : left.RadarColorValue,
            RadarColorUntilMode: useRightColor ? right.RadarColorUntilMode : left.RadarColorUntilMode,
            RadarColorUntilDuration: useRightColor ? right.RadarColorUntilDuration : left.RadarColorUntilDuration,
            RadarAnimation: useRightAnimation ? right.RadarAnimation : left.RadarAnimation,
            RadarAnimationUntilMode: useRightAnimation ? right.RadarAnimationUntilMode : left.RadarAnimationUntilMode,
            RadarAnimationUntilDuration: useRightAnimation ? right.RadarAnimationUntilDuration : left.RadarAnimationUntilDuration,
            RadarZoomPercent: Math.Max(left.RadarZoomPercent, right.RadarZoomPercent),
            SoundMinimumMatches: Math.Min(Math.Max(1, left.SoundMinimumMatches), Math.Max(1, right.SoundMinimumMatches)));
    }

    private static int AlertColorPriority(AlertRuleActions actions)
    {
        if (!actions.RadarColor || actions.RadarColorValue.Equals("none", StringComparison.OrdinalIgnoreCase))
        {
            return 0;
        }

        var color = actions.RadarColorValue.Trim().ToLowerInvariant();
        if (color.StartsWith('#'))
        {
            return 50;
        }

        return color switch
        {
            "red" => 45,
            "status" or "amber" or "yellow" => 40,
            "blue" or "violet" => 25,
            "fresh" or "cyan" or "green" => 10,
            _ => 20
        };
    }

    private static int AlertAnimationPriority(AlertRuleActions actions)
    {
        if (!actions.RadarBlink || actions.RadarAnimation.Equals("none", StringComparison.OrdinalIgnoreCase))
        {
            return 0;
        }

        return NormalizeAlertAnimation(actions.RadarAnimation) switch
        {
            "blink" => 40,
            "sweep" => 30,
            "outline" => 20,
            "pulse" => 10,
            _ => 1
        };
    }

    private void SyncResourcesPreservingSelection(IReadOnlyList<FlatResourceRow> visibleRows)
    {
        suppressTableSelectionChanges = true;
        try
        {
            SyncCollection(Resources, visibleRows);
            selectedResourceRow = selectedResource is null
                ? null
                : Resources.FirstOrDefault(row => row.Id.Equals(selectedResource.Id, StringComparison.Ordinal));
            OnPropertyChanged(nameof(SelectedResourceRow));
        }
        finally
        {
            suppressTableSelectionChanges = false;
        }
    }

    private void UpdatePulseLayer(IReadOnlyList<FlatResourceRow> allRows, IReadOnlyList<FlatResourceRow> scopedRows)
    {
        var pulseRows = scopedRows as IReadOnlyList<FlatResourceRow> ?? scopedRows.ToList();
        var pods = new List<FlatResourceRow>();
        var nodes = new List<FlatResourceRow>();
        double cpuUsed = 0;
        double podCpuLimit = 0;
        double nodeCpuLimit = 0;
        long memoryUsed = 0;
        long podMemoryLimit = 0;
        long nodeMemoryLimit = 0;
        long storageUsed = 0;
        long storageLimit = 0;
        var hasNetwork = false;
        long networkIn = 0;
        long networkOut = 0;
        foreach (var row in pulseRows)
        {
            var pulse = row.Pulse;
            cpuUsed += pulse.CpuMillicores ?? 0;
            memoryUsed += pulse.MemoryBytes ?? 0;
            storageUsed += pulse.StorageUsedBytes ?? 0;
            storageLimit += pulse.StorageLimitBytes ?? 0;
            if (pulse.NetworkInBytesPerSecond is { } inBytes)
            {
                hasNetwork = true;
                networkIn += inBytes;
            }
            if (pulse.NetworkOutBytesPerSecond is { } outBytes)
            {
                hasNetwork = true;
                networkOut += outBytes;
            }
            switch (row.Kind)
            {
                case "Pod":
                    pods.Add(row);
                    podCpuLimit += pulse.CpuLimitMillicores ?? 0;
                    podMemoryLimit += pulse.MemoryLimitBytes ?? 0;
                    break;
                case "Node":
                    nodes.Add(row);
                    nodeCpuLimit += pulse.CpuLimitMillicores ?? 0;
                    nodeMemoryLimit += pulse.MemoryLimitBytes ?? 0;
                    break;
            }
        }
        var cpuLimit = nodeCpuLimit > 0 ? nodeCpuLimit : podCpuLimit;
        var memoryLimit = nodeMemoryLimit > 0 ? nodeMemoryLimit : podMemoryLimit;
        var cpuPercent = Percent(cpuUsed, cpuLimit);
        var memoryPercent = Percent(memoryUsed, memoryLimit);
        var storagePercent = Percent(storageUsed, storageLimit);
        var scope = PulseScopeTooltip(pulseRows, allRows.Count);
        var cpuSummary = PulseCpuSummary(cpuUsed, cpuLimit);
        var memorySummary = PulseMemorySummary(memoryUsed, memoryLimit);
        var storageSummary = PulseMemorySummary(storageUsed, storageLimit);
        var metrics = new List<PulseMetricCard>();

        if (cpuUsed > 0 || cpuLimit > 0)
        {
            metrics.Add(new PulseMetricCard("CPU", cpuSummary, cpuPercent, string.Empty, PulseMetricTooltip("CPU", cpuSummary, scope)));
        }
        if (memoryUsed > 0 || memoryLimit > 0)
        {
            metrics.Add(new PulseMetricCard("Memory", memorySummary, memoryPercent, string.Empty, PulseMetricTooltip("Memory", memorySummary, scope)));
        }
        if (storageUsed > 0 || storageLimit > 0)
        {
            metrics.Add(new PulseMetricCard("Storage", storageSummary, storagePercent, string.Empty, PulseMetricTooltip("Storage", storageSummary, scope)));
        }
        if (pods.Count > 0)
        {
            var podCount = pods.Count.ToString(CultureInfo.InvariantCulture);
            metrics.Add(new PulseMetricCard("Pods", podCount, 0, string.Empty, PulseMetricTooltip("Pods", podCount, scope), HasBar: false));
        }
        if (nodes.Count > 0)
        {
            var nodeCount = nodes.Count.ToString(CultureInfo.InvariantCulture);
            metrics.Add(new PulseMetricCard("Nodes", nodeCount, 0, string.Empty, PulseMetricTooltip("Nodes", nodeCount, scope), HasBar: false));
        }

        if (hasNetwork)
        {
            var networkSummary = $"↓{FormatBytes(networkIn)}/s ↑{FormatBytes(networkOut)}/s";
            metrics.Add(new PulseMetricCard("Network", networkSummary, 0, string.Empty, PulseMetricTooltip("Network", networkSummary, scope), HasBar: false));
        }

        SyncCollection(ClusterPulseItems, metrics);
    }

    private static string PulseScopeTooltip(IReadOnlyList<FlatResourceRow> rows, int totalCachedRows)
    {
        var clusters = new HashSet<string>(StringComparer.Ordinal);
        var namespaces = new HashSet<string>(StringComparer.Ordinal);
        foreach (var row in rows)
        {
            if (!string.IsNullOrWhiteSpace(row.Cluster))
            {
                clusters.Add(row.Cluster);
            }
            namespaces.Add(row.Namespace ?? "cluster");
        }
        return $"Scope: {rows.Count.ToString(CultureInfo.InvariantCulture)} visible of {totalCachedRows.ToString(CultureInfo.InvariantCulture)} cached resources across {clusters.Count.ToString(CultureInfo.InvariantCulture)} cluster(s) and {namespaces.Count.ToString(CultureInfo.InvariantCulture)} namespace sector(s).";
    }

    private static string PulseMetricTooltip(string label, string value, string scope)
    {
        return $"{label}: {value}{Environment.NewLine}{scope}";
    }

    private static double Percent(double used, double limit)
    {
        return limit <= 0 ? 0 : Math.Clamp(used / limit * 100, 0, 100);
    }

    private static string PulseCpuSummary(double usedMillicores, double limitMillicores)
    {
        var used = FormatCpu(usedMillicores);
        return limitMillicores <= 0 ? used : $"{used} / {FormatCpu(limitMillicores)}";
    }

    private static string PulseMemorySummary(long usedBytes, long limitBytes)
    {
        var used = FormatBytes(usedBytes);
        return limitBytes <= 0 ? used : $"{used} / {FormatBytes(limitBytes)}";
    }

    private static string FormatCpu(double millicores)
    {
        if (millicores <= 0)
        {
            return "-";
        }

        return millicores < 1000 ? $"{millicores:0}m" : $"{millicores / 1000d:0.#}c";
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

        return unit == 0 ? $"{value:0}{units[unit]}" : $"{value:0.#}{units[unit]}";
    }

    private bool ShouldAnnounceResourceRow(FlatResourceRow row, IReadOnlySet<string> visibleResourceIds, DateTimeOffset now)
    {
        if (!activeAlertActionsByResourceId.TryGetValue(row.Id, out var actions) || !actions.RadarBlink)
        {
            resourceAlertBlinkUntil.Remove(row.Id);
            return false;
        }

        if (actions.RadarAnimationUntilMode.Equals(AlertUntilModes.NewInView, StringComparison.OrdinalIgnoreCase))
        {
            return IsResourceViewPulseActive(row.Id, visibleResourceIds, actions, now);
        }

        return true;
    }

    private bool IsResourceViewPulseActive(string rowId, IReadOnlySet<string> visibleResourceIds, AlertRuleActions actions, DateTimeOffset now)
    {
        if (!visibleResourceIds.Contains(rowId))
        {
            resourceAlertBlinkUntil.Remove(rowId);
            return false;
        }

        if (!previousVisibleResourceAlertIds.Contains(rowId))
        {
            resourceAlertBlinkUntil[rowId] = now.Add(AlertViewDuration(actions));
            StartAlertAnimationExpiryTimer();
        }

        if (resourceAlertBlinkUntil.TryGetValue(rowId, out var until) && until > now)
        {
            return true;
        }

        resourceAlertBlinkUntil.Remove(rowId);
        return false;
    }

    private void SyncPreviousVisibleResourceAlertIds(IReadOnlySet<string> visibleResourceIds)
    {
        previousVisibleResourceAlertIds.Clear();
        foreach (var id in visibleResourceIds)
        {
            previousVisibleResourceAlertIds.Add(id);
        }

        foreach (var id in resourceAlertBlinkUntil.Keys.Where(id => !visibleResourceIds.Contains(id)).ToArray())
        {
            resourceAlertBlinkUntil.Remove(id);
        }
    }

    private string AlertAnimationFor(string rowId)
    {
        if (!activeAlertActionsByResourceId.TryGetValue(rowId, out var actions) || !actions.RadarBlink)
        {
            return "pulse";
        }

        return NormalizeAlertAnimation(actions.RadarAnimation);
    }

    private static string NormalizeAlertAnimation(string value)
    {
        var normalized = value.Trim().ToLowerInvariant();
        return normalized is "blink" or "pulse" or "sweep" or "outline" ? normalized : "pulse";
    }

    private void UpdateRadarFromCache(ResourceQuery? localQuery = null)
    {
        var rows = RadarRows();
        UpdateRadarBlocks(rows, RadarFilterScope.From(rows, localQuery ?? BuildLocalQuery()));
    }

    private void NotifyResourceLogoStateChanged()
    {
        OnPropertyChanged(nameof(IsResourceLogoVisible));
        OnPropertyChanged(nameof(ResourceLogoTitle));
        OnPropertyChanged(nameof(ResourceLogoMessage));
    }

    private IEnumerable<FlatResourceRow> SortRows(IEnumerable<FlatResourceRow> rows)
    {
        var column = resourceSortDirection == ResourceSortDirection.None ? "Age" : resourceSortColumn;
        var ascending = resourceSortDirection is ResourceSortDirection.None or ResourceSortDirection.Ascending;
        return ascending
            ? rows.OrderBy(row => ResourceSortValue(row, column), ResourceSortValueComparer.Instance)
                .ThenBy(row => row.Kind, StringComparer.Ordinal)
                .ThenBy(row => row.Namespace ?? string.Empty, StringComparer.Ordinal)
                .ThenBy(row => row.Name, StringComparer.Ordinal)
            : rows.OrderByDescending(row => ResourceSortValue(row, column), ResourceSortValueComparer.Instance)
                .ThenBy(row => row.Kind, StringComparer.Ordinal)
                .ThenBy(row => row.Namespace ?? string.Empty, StringComparer.Ordinal)
                .ThenBy(row => row.Name, StringComparer.Ordinal);
    }

    private IEnumerable<FlatResourceRow> SortEventRows(IEnumerable<FlatResourceRow> rows)
    {
        var column = eventSortDirection == ResourceSortDirection.None ? "Age" : eventSortColumn;
        var ascending = eventSortDirection is ResourceSortDirection.None or ResourceSortDirection.Ascending;
        return ascending
            ? rows.OrderBy(row => ResourceSortValue(row, column), ResourceSortValueComparer.Instance)
                .ThenBy(row => row.Namespace ?? string.Empty, StringComparer.Ordinal)
                .ThenBy(row => row.Name, StringComparer.Ordinal)
            : rows.OrderByDescending(row => ResourceSortValue(row, column), ResourceSortValueComparer.Instance)
                .ThenBy(row => row.Namespace ?? string.Empty, StringComparer.Ordinal)
                .ThenBy(row => row.Name, StringComparer.Ordinal);
    }

    private static void SyncCollection<T>(ObservableCollection<T> target, IReadOnlyList<T> desired)
    {
        var shared = Math.Min(target.Count, desired.Count);
        for (var index = 0; index < shared; index++)
        {
            if (!EqualityComparer<T>.Default.Equals(target[index], desired[index]))
            {
                target[index] = desired[index];
            }
        }

        while (target.Count > desired.Count)
        {
            target.RemoveAt(target.Count - 1);
        }

        for (var index = target.Count; index < desired.Count; index++)
        {
            target.Add(desired[index]);
        }
    }

    private void SyncRadarBlocks(IReadOnlyList<RadarBlockViewModel> desired)
    {
        var shared = Math.Min(RadarBlocks.Count, desired.Count);
        for (var index = 0; index < shared; index++)
        {
            RadarBlocks[index].UpdateFrom(desired[index]);
        }

        while (RadarBlocks.Count > desired.Count)
        {
            RadarBlocks.RemoveAt(RadarBlocks.Count - 1);
        }

        for (var index = RadarBlocks.Count; index < desired.Count; index++)
        {
            RadarBlocks.Add(desired[index]);
        }
    }

    private static IComparable ResourceSortValue(FlatResourceRow row, string column)
    {
        return column switch
        {
            "Status" => row.Status,
            "Kind" => row.Kind,
            "Name" => row.Name,
            "Namespace" => row.Namespace ?? "cluster",
            "Cluster" => row.Cluster,
            "CPU" => MetricSortValue(row.Pulse.CpuMillicores, row.Pulse.CpuLimitMillicores),
            "Memory" => MetricSortValue(row.Pulse.MemoryBytes, row.Pulse.MemoryLimitBytes),
            "Storage" => MetricSortValue(row.Pulse.StorageUsedBytes, row.Pulse.StorageLimitBytes),
            "Age" => AgeDuration(row.Age),
            "Ready" => row.Ready,
            "Restarts" => row.Restarts,
            "Node" => row.Node ?? string.Empty,
            "Image" => row.ImageSummary,
            "Owner" => row.Owner ?? string.Empty,
            "Reason" => row.EventReason,
            "Object" => row.EventObject,
            "Message" => row.EventMessage,
            _ => row.Name
        };
    }

    private static double MetricSortValue(double? current, double? limit)
    {
        return current ?? limit ?? -1;
    }

    private static double MetricSortValue(long? current, long? limit)
    {
        return current ?? limit ?? -1;
    }

    private void UpdateHealthSegments(IReadOnlyList<FlatResourceRow> rows)
    {
        HealthSegments.Clear();
        var summary = ResourceHealthCalculator.Calculate(rows, Failures.Count);
        foreach (var (segment, height) in HealthVisualSegments(summary.Segments))
        {
            AddHealthSegment(segment, height);
        }

        if (summary.Total == 0)
        {
            HealthSummary = "No cached resources yet.";
            return;
        }

        var source = Failures.Count == 0 ? "resource cache" : $"resource cache + {Failures.Count} API warning(s)";
        HealthSummary = $"{summary.Healthy} healthy / {summary.Warning} warning / {summary.Critical} critical / {summary.Total} checked ({source})";
    }

    private static IReadOnlyList<(ResourceHealthSegment Segment, double Height)> HealthVisualSegments(IReadOnlyList<ResourceHealthSegment> segments)
    {
        const int tickCount = 30;
        var ordered = segments.OrderBy(HealthSegmentRank).ToList();
        var heights = ordered.ToDictionary(segment => segment.State, segment => segment.Percent, StringComparer.Ordinal);

        var ticksByState = ordered
            .ToDictionary(
                segment => segment.State,
                segment =>
                {
                    var height = heights.GetValueOrDefault(segment.State, segment.Percent);
                    return height <= 0 ? 0 : Math.Max(1, (int)Math.Round(height / 100d * tickCount));
                },
                StringComparer.Ordinal);
        var totalTicks = ticksByState.Values.Sum();
        while (totalTicks > tickCount)
        {
            var state = ordered.LastOrDefault(segment => ticksByState[segment.State] > 1)?.State;
            if (state is null)
            {
                break;
            }

            ticksByState[state]--;
            totalTicks--;
        }

        while (totalTicks < tickCount && ordered.Count > 0)
        {
            var state = ordered.Last().State;
            ticksByState[state]++;
            totalTicks++;
        }

        var tickHeight = 100d / Math.Max(1, tickCount);
        return ordered
            .SelectMany(segment => Enumerable.Repeat((segment, tickHeight), ticksByState.GetValueOrDefault(segment.State, 0)))
            .ToList();
    }

    private void AddHealthSegment(ResourceHealthSegment segment, double height)
    {
        HealthSegments.Add(new HealthSegmentViewModel(
            segment.State,
            segment.Count,
            segment.Percent,
            height,
            AppThemeCatalog.StatusBrush(segment.State)));
    }

    private static int HealthSegmentRank(ResourceHealthSegment segment)
    {
        return segment.State switch
        {
            "CRITICAL" => 0,
            "WARNING" => 1,
            "UNKNOWN" => 2,
            "HEALTHY" => 3,
            _ => 4
        };
    }

    private IReadOnlyList<FlatResourceRow> RadarRows()
    {
        return cachedRows
            .OrderBy(row => row.Cluster, StringComparer.Ordinal)
            .ThenBy(row => row.Namespace ?? "cluster", StringComparer.Ordinal)
            .ThenBy(row => row.Kind, StringComparer.Ordinal)
            .ThenBy(row => ResourceFilterMatcher.IsProblem(row, restartOutlierThreshold) ? 0 : 1)
            .ThenBy(row => row.Name, StringComparer.Ordinal)
            .ToList();
    }

    // The dependency island lives in an unbounded deterministic world grid; the viewport is sized by the panel.
    private const double RadarGridStep = 7;
    private const double RadarTileVisualSize = 5.5;

    private sealed record RadarRect(double X, double Y, double W, double H);

    private sealed record RadarPoint(double X, double Y);

    private readonly record struct RadarGridCell(int Column, int Row);

    private readonly record struct RadarLifeCell(int X, int Y);

    private sealed record RadarFilterScope(
        bool HasFilter,
        IReadOnlySet<string> ActiveIds,
        IReadOnlySet<string> ActiveClusters,
        IReadOnlySet<string> ActiveNamespaces)
    {
        public static RadarFilterScope From(IReadOnlyList<FlatResourceRow> rows, ResourceQuery query)
        {
            if (!HasEffectiveFilter(query))
            {
                return new RadarFilterScope(false, new HashSet<string>(StringComparer.Ordinal), new HashSet<string>(StringComparer.Ordinal), new HashSet<string>(StringComparer.Ordinal));
            }

            var activeRows = ResourceFilterMatcher.FilterRows(rows, query with { Limit = 5_000 })
                .ToList();
            return new RadarFilterScope(
                true,
                activeRows.Select(row => row.Id).ToHashSet(StringComparer.Ordinal),
                activeRows.Select(row => row.Cluster).ToHashSet(StringComparer.Ordinal),
                activeRows.Select(NamespaceKeyForRow).ToHashSet(StringComparer.Ordinal));
        }

        public bool IsActive(FlatResourceRow row)
        {
            if (!HasFilter)
            {
                return true;
            }

            if (ActiveIds.Contains(row.Id))
            {
                return true;
            }

            if (row.Kind.Equals("Cluster", StringComparison.OrdinalIgnoreCase))
            {
                return ActiveClusters.Contains(row.Cluster) || ActiveClusters.Contains(row.Name);
            }

            if (row.Kind.Equals("Namespace", StringComparison.OrdinalIgnoreCase))
            {
                return ActiveNamespaces.Contains(NamespaceKeyForRow(row));
            }

            return false;
        }

        private static bool HasEffectiveFilter(ResourceQuery query)
        {
            return !string.IsNullOrWhiteSpace(query.Search)
                   || !string.IsNullOrWhiteSpace(query.Id)
                   || !string.IsNullOrWhiteSpace(query.Issue)
                   || !string.IsNullOrWhiteSpace(query.Kind)
                   || !string.IsNullOrWhiteSpace(query.Name)
                   || !string.IsNullOrWhiteSpace(query.Namespace)
                   || !string.IsNullOrWhiteSpace(query.Cluster)
                   || !string.IsNullOrWhiteSpace(query.Status)
                   || !string.IsNullOrWhiteSpace(query.Age)
                   || !string.IsNullOrWhiteSpace(query.Node)
                   || !string.IsNullOrWhiteSpace(query.Image)
                   || !string.IsNullOrWhiteSpace(query.Ready)
                   || !string.IsNullOrWhiteSpace(query.Restarts)
                   || !string.IsNullOrWhiteSpace(query.Owner)
                   || query.ProblemsOnly
                   || query.ActivityOnly;
        }

        private static string NamespaceKeyForRow(FlatResourceRow row)
        {
            var ns = row.Kind.Equals("Namespace", StringComparison.OrdinalIgnoreCase) && string.IsNullOrWhiteSpace(row.Namespace)
                ? row.Name
                : row.Namespace ?? "cluster";
            return $"{row.Cluster}/{ns}";
        }
    }

    private sealed record RadarLifeRule(string Label, IReadOnlySet<int> Birth, IReadOnlySet<int> Survival)
    {
        public bool ShouldLive(bool alive, int neighbors)
        {
            return alive ? Survival.Contains(neighbors) : Birth.Contains(neighbors);
        }
    }

    private static readonly RadarLifeRule[] RadarLifeRules =
    [
        new("B3/S23", new HashSet<int> { 3 }, new HashSet<int> { 2, 3 }),
        new("B36/S23", new HashSet<int> { 3, 6 }, new HashSet<int> { 2, 3 }),
        new("B34/S34", new HashSet<int> { 3, 4 }, new HashSet<int> { 3, 4 }),
        new("B3678/S34678", new HashSet<int> { 3, 6, 7, 8 }, new HashSet<int> { 3, 4, 6, 7, 8 }),
        new("B2/S", new HashSet<int> { 2 }, new HashSet<int>())
    ];

    private void UpdateRadarBlocks(IReadOnlyList<FlatResourceRow> rows, RadarFilterScope filterScope)
    {
        var desired = new List<RadarBlockViewModel>();
        var worldCenters = new Dictionary<string, RadarPoint>(StringComparer.Ordinal);
        var visibleAlertIds = new HashSet<string>(StringComparer.Ordinal);
        var now = DateTimeOffset.Now;
        var capped = rows.ToList();
        var wasIdle = IsRadarIdle;
        if (capped.Count == 0)
        {
            previousVisibleRadarAlertIds.Clear();
            RenderRadarLife(reset: !IsRadarIdle);
            UpdateRadarIdleTimer();
            return;
        }

        var occupied = new HashSet<RadarGridCell>();
        var clusterGroups = capped
            .GroupBy(row => row.Cluster)
            .OrderBy(group => group.Key, StringComparer.Ordinal)
            .ToList();
        var clusterCenters = ClusterCenters(clusterGroups.Count);

        for (var clusterIndex = 0; clusterIndex < clusterGroups.Count; clusterIndex++)
        {
            var clusterGroup = clusterGroups[clusterIndex];
            var clusterCenter = clusterCenters[clusterIndex];
            var clusterRow = RadarVirtualRow("Cluster", clusterGroup.Key, null, clusterGroup.Key);
            clusterCenter = AddRadarBlock(
                desired,
                occupied,
                clusterRow,
                "core/" + clusterGroup.Key,
                clusterCenter,
                eventShallow: false,
                isClickable: true,
                worldCenters,
                visibleAlertIds,
                now,
                displayKind: "Cluster",
                isFilteredOut: !filterScope.IsActive(clusterRow));

            var namespaceGroups = clusterGroup
                .GroupBy(row => row.Namespace ?? "cluster")
                .OrderBy(group => group.Key, StringComparer.Ordinal)
                .ToList();
            for (var namespaceIndex = 0; namespaceIndex < namespaceGroups.Count; namespaceIndex++)
            {
                var namespaceGroup = namespaceGroups[namespaceIndex];
                var namespaceAngle = NamespaceAngle(clusterGroup.Key, namespaceGroup.Key, namespaceIndex, namespaceGroups.Count);
                var namespaceRadiusX = clusterGroups.Count > 1 ? 20 : 28;
                var namespaceRadiusY = clusterGroups.Count > 1 ? 14 : 20;
                var namespaceTarget = new RadarPoint(
                    clusterCenter.X + Math.Cos(namespaceAngle) * namespaceRadiusX,
                    clusterCenter.Y + Math.Sin(namespaceAngle) * namespaceRadiusY);
                var namespaceRow = namespaceGroup.FirstOrDefault(row =>
                    row.Kind.Equals("Namespace", StringComparison.OrdinalIgnoreCase) &&
                    row.Name.Equals(namespaceGroup.Key, StringComparison.Ordinal)) ??
                    RadarVirtualRow("Namespace", namespaceGroup.Key, null, clusterGroup.Key);
                var namespaceCenter = AddRadarBlock(
                    desired,
                    occupied,
                    namespaceRow,
                    clusterGroup.Key + "/" + namespaceGroup.Key,
                    namespaceTarget,
                    eventShallow: false,
                    isClickable: true,
                    worldCenters,
                    visibleAlertIds,
                    now,
                    displayKind: "Namespace",
                    isFilteredOut: !filterScope.IsActive(namespaceRow));

                var resourceRows = namespaceGroup
                    .Where(row => !ReferenceEquals(row, namespaceRow))
                    .Where(row => !row.Kind.Equals("Namespace", StringComparison.OrdinalIgnoreCase))
                    .ToList();
                AddNamespaceArms(
                    desired,
                    occupied,
                    resourceRows,
                    clusterGroup.Key,
                    namespaceGroup.Key,
                    namespaceCenter,
                    namespaceAngle,
                    filterScope,
                    worldCenters,
                    visibleAlertIds,
                    now);
            }
        }

        MaybeStartRadarAutoFollow(capped, filterScope, worldCenters);
        SyncRadarBlocks(desired);
        previousVisibleRadarAlertIds.Clear();
        foreach (var id in visibleAlertIds)
        {
            previousVisibleRadarAlertIds.Add(id);
        }

        if (RadarIdleCells.Count > 0)
        {
            RadarIdleCells.Clear();
        }

        IsRadarIdle = false;
        if (wasIdle)
        {
            TryPlayAutomaticSound("kenney-interface-maximize-001");
        }
        UpdateRadarIdleTimer();
    }

    private void AddNamespaceArms(
        List<RadarBlockViewModel> desired,
        HashSet<RadarGridCell> occupied,
        IReadOnlyList<FlatResourceRow> rows,
        string cluster,
        string ns,
        RadarPoint namespaceCenter,
        double namespaceAngle,
        RadarFilterScope filterScope,
        Dictionary<string, RadarPoint> worldCenters,
        HashSet<string> visibleAlertIds,
        DateTimeOffset now)
    {
        if (rows.Count == 0)
        {
            return;
        }

        var ordered = OrderedRadarRows(rows).ToList();
        var armCount = ordered.Count <= 3
            ? Math.Max(1, ordered.Count)
            : Math.Clamp((int)Math.Ceiling(Math.Sqrt(ordered.Count) / 1.2), 3, 10);
        var branchSpread = Math.Clamp(armCount * 0.09, 0.16, 0.78);
        for (var index = 0; index < ordered.Count; index++)
        {
            var row = ordered[index];
            var eventShallow = IsRadarEvent(row);
            var arm = index % armCount;
            var depth = index / armCount;
            var branchOffset = armCount == 1 ? 0 : ((arm / (double)(armCount - 1)) - 0.5) * branchSpread;
            var jitter = StableRange(row.Id + ":branch", -0.04, 0.04);
            var angle = namespaceAngle + branchOffset + jitter;
            var ringBias = RadarTerrainRing(row) * 6.2;
            var distance = 8 + ringBias + depth * 6.4 + (eventShallow ? 8 : 0);
            var drift = StableRange(row.Id + ":drift", -3.5, 3.5);
            var target = new RadarPoint(
                namespaceCenter.X + Math.Cos(angle) * distance - Math.Sin(angle) * drift,
                namespaceCenter.Y + Math.Sin(angle) * distance + Math.Cos(angle) * drift);
            AddRadarBlock(
                desired,
                occupied,
                row,
                cluster + "/" + ns + "/" + row.Kind,
                target,
                eventShallow: eventShallow,
                isClickable: true,
                worldCenters,
                visibleAlertIds,
                now,
                displayKind: row.Kind,
                isFilteredOut: !filterScope.IsActive(row));
        }
    }

    private RadarPoint AddRadarBlock(
        List<RadarBlockViewModel> desired,
        HashSet<RadarGridCell> occupied,
        FlatResourceRow row,
        string groupKey,
        RadarPoint center,
        bool eventShallow,
        bool isClickable,
        Dictionary<string, RadarPoint> worldCenters,
        HashSet<string> visibleAlertIds,
        DateTimeOffset now,
        string? displayKind = null,
        bool isFilteredOut = false)
    {
        var worldRect = PlaceRadarRect(center, occupied, row.Id);
        var worldCenter = RectCenter(worldRect);
        worldCenters[row.Id] = worldCenter;
        var projected = ProjectRadarRect(worldRect);
        if (!IsRadarRectVisible(projected))
        {
            return worldCenter;
        }

        var problem = ResourceFilterMatcher.ProblemReason(row, restartOutlierThreshold);
        var colorAlert = AlertColorFor(row.Id, isFilteredOut);
        var announce = ShouldAnnounceRadarAlert(row, problem, isFilteredOut, visibleAlertIds, now);
        var alertAnimation = announce ? AlertAnimationFor(row.Id) : string.Empty;
        desired.Add(new RadarBlockViewModel(
            row,
            groupKey,
            projected.X,
            projected.Y,
            Math.Max(0, projected.W),
            Math.Max(0, projected.H),
            RadarBrush(row, problem, isFilteredOut, colorAlert),
            problem,
            RadarMetrics(row),
            isSelected: row.Id == selectedRadarResourceId,
            borderBrush: RadarBorderBrush(row, problem, eventShallow, isFilteredOut, !colorAlert.Equals("none", StringComparison.OrdinalIgnoreCase)),
            announceBrush: RadarAnnounceBrush(row, problem, isFilteredOut, !colorAlert.Equals("none", StringComparison.OrdinalIgnoreCase)),
            showProblemGlyph: false,
            isEventShallow: eventShallow,
            displayKind: displayKind ?? row.Kind,
            displayName: row.Name,
            isClickable: isClickable,
            isAnnouncing: announce,
            alertAnimation: alertAnimation,
            alertColor: colorAlert,
            isDimmed: isFilteredOut));
        return worldCenter;
    }

    private bool ShouldAnnounceRadarAlert(
        FlatResourceRow row,
        string problem,
        bool isFilteredOut,
        HashSet<string> visibleAlertIds,
        DateTimeOffset now)
    {
        if (state.Settings().AnimationIntensity == 0
            || isFilteredOut
            || IsVirtualRadarResource(row))
        {
            radarAlertBlinkUntil.Remove(row.Id);
            return false;
        }

        if (!activeAlertActionsByResourceId.TryGetValue(row.Id, out var actions) || !actions.RadarBlink)
        {
            radarAlertBlinkUntil.Remove(row.Id);
            return false;
        }

        visibleAlertIds.Add(row.Id);
        if (actions.RadarAnimationUntilMode.Equals(AlertUntilModes.NoMatch, StringComparison.OrdinalIgnoreCase))
        {
            radarAlertBlinkUntil.Remove(row.Id);
            return true;
        }

        if (actions.RadarAnimationUntilMode.Equals(AlertUntilModes.NewInView, StringComparison.OrdinalIgnoreCase))
        {
            if (!previousVisibleRadarAlertIds.Contains(row.Id))
            {
                radarAlertBlinkUntil[row.Id] = now.Add(AlertViewDuration(actions));
                StartAlertAnimationExpiryTimer();
            }

            if (radarAlertBlinkUntil.TryGetValue(row.Id, out var viewUntil) && viewUntil > now)
            {
                return true;
            }

            radarAlertBlinkUntil.Remove(row.Id);
            return false;
        }

        radarAlertBlinkUntil.Remove(row.Id);
        return true;
    }

    private int RadarBlinkDurationMilliseconds()
    {
        return 650 + Math.Clamp((int)state.Settings().AnimationIntensity, 0, 100) * 8;
    }

    private TimeSpan AlertViewDuration(AlertRuleActions actions)
    {
        if (ResourceFilterMatcher.ParseHumanDuration(actions.RadarAnimationUntilDuration) is { } parsed && parsed > TimeSpan.Zero)
        {
            return parsed;
        }

        return TimeSpan.FromMilliseconds(RadarBlinkDurationMilliseconds());
    }

    private void StartAlertAnimationExpiryTimer()
    {
        if (!alertAnimationExpiryTimer.IsEnabled)
        {
            alertAnimationExpiryTimer.Start();
        }
    }

    private void ExpireAlertAnimations()
    {
        var now = DateTimeOffset.Now;
        var expired = false;
        if (HasDueAlertHold(now))
        {
            EvaluateAlertRules();
            expired = true;
        }

        foreach (var id in resourceAlertBlinkUntil.Where(pair => pair.Value <= now).Select(pair => pair.Key).ToArray())
        {
            resourceAlertBlinkUntil.Remove(id);
            expired = true;
        }

        foreach (var id in radarAlertBlinkUntil.Where(pair => pair.Value <= now).Select(pair => pair.Key).ToArray())
        {
            radarAlertBlinkUntil.Remove(id);
            expired = true;
        }

        if (resourceAlertBlinkUntil.Count == 0
            && radarAlertBlinkUntil.Count == 0
            && !HasPendingAlertHold(now))
        {
            alertAnimationExpiryTimer.Stop();
        }

        if (expired)
        {
            ApplyLocalFilter();
        }
    }

    private bool HasDueAlertHold(DateTimeOffset now)
    {
        return alertDurationUntilByRuleResource.Values.Any(until => until <= now)
               || alertColorUntilByRuleResource.Values.Any(until => until <= now)
               || alertAnimationUntilByRuleResource.Values.Any(until => until <= now);
    }

    private bool HasPendingAlertHold(DateTimeOffset now)
    {
        return alertDurationUntilByRuleResource.Values.Any(until => until > now)
               || alertColorUntilByRuleResource.Values.Any(until => until > now)
               || alertAnimationUntilByRuleResource.Values.Any(until => until > now);
    }

    private bool IsRadarAlert(FlatResourceRow row, bool isFilteredOut, Func<AlertRuleActions, bool> hasAction)
    {
        return !isFilteredOut
               && !IsVirtualRadarResource(row)
               && activeAlertActionsByResourceId.TryGetValue(row.Id, out var actions)
               && hasAction(actions);
    }

    private bool HasAlertAction(string rowId, Func<AlertRuleActions, bool> hasAction)
    {
        return activeAlertActionsByResourceId.TryGetValue(rowId, out var actions)
               && hasAction(actions);
    }

    private string AlertColorFor(string rowId, bool isFilteredOut)
    {
        if (isFilteredOut || !activeAlertActionsByResourceId.TryGetValue(rowId, out var actions) || !actions.RadarColor)
        {
            return "none";
        }

        return string.IsNullOrWhiteSpace(actions.RadarColorValue) ? "status" : actions.RadarColorValue;
    }

    private void MaybeStartRadarAutoFollow(
        IReadOnlyList<FlatResourceRow> rows,
        RadarFilterScope filterScope,
        IReadOnlyDictionary<string, RadarPoint> worldCenters)
    {
        if (!state.Settings().RadarAutoFollowAlerts || rows.Count == 0 || worldCenters.Count == 0)
        {
            lastRadarAutoFollowAlertKey = string.Empty;
            radarAutoFollowQueue.Clear();
            return;
        }

        var rowIds = rows.Select(row => row.Id).ToHashSet(StringComparer.Ordinal);
        var candidates = new List<(string Key, RadarAutoFollowRequest Request)>();
        foreach (var match in activeRadarAlertMatches)
        {
            if (!match.Actions.RadarFocus && !match.Actions.RadarZoom)
            {
                continue;
            }

            var targets = match.Rows
                .Where(row => rowIds.Contains(row.Id))
                .Where(row => worldCenters.ContainsKey(row.Id))
                .Where(row => filterScope.IsActive(row))
                .Where(row => !IsVirtualRadarResource(row))
                .OrderBy(row => RadarAlertPriority(row, ResourceFilterMatcher.ProblemReason(row, restartOutlierThreshold)))
                .ThenBy(row => RadarAlertAge(row))
                .ThenBy(row => row.Cluster, StringComparer.Ordinal)
                .ThenBy(row => row.Namespace ?? string.Empty, StringComparer.Ordinal)
                .ThenBy(row => row.Kind, StringComparer.Ordinal)
                .ThenBy(row => row.Name, StringComparer.Ordinal)
                .ToList();
            if (targets.Count == 0)
            {
                continue;
            }

            var candidateKey = $"{match.RuleId}:{AlertRowsStateKey(targets)}";
            var worldCenter = new RadarPoint(
                targets.Average(row => worldCenters[row.Id].X),
                targets.Average(row => worldCenters[row.Id].Y));
            var zoomPercent = match.Actions is { RadarZoom: true, RadarZoomPercent: > 0 }
                ? match.Actions.RadarZoomPercent
                : 100;
            candidates.Add((candidateKey, new RadarAutoFollowRequest(worldCenter, Math.Max(1d, zoomPercent / 100d))));
        }

        if (candidates.Count == 0)
        {
            var anyUnfilteredRadarAlert = rows.Any(row =>
                activeAlertActionsByResourceId.TryGetValue(row.Id, out var actions)
                && (actions.RadarFocus || actions.RadarZoom));
            if (!anyUnfilteredRadarAlert)
            {
                lastRadarAutoFollowAlertKey = string.Empty;
                radarAutoFollowQueue.Clear();
            }
            return;
        }

        var key = string.Join("||", candidates.Select(candidate => candidate.Key));
        if (key.Equals(lastRadarAutoFollowAlertKey, StringComparison.Ordinal))
        {
            return;
        }

        lastRadarAutoFollowAlertKey = key;
        radarAutoFollowQueue.Clear();
        StartOrQueueRadarAutoFollow(candidates[0].Request);
        foreach (var candidate in candidates.Skip(1))
        {
            radarAutoFollowQueue.Enqueue(candidate.Request);
        }
    }

    private static int RadarAlertPriority(FlatResourceRow row, string problem)
    {
        if (problem.Length > 0)
        {
            return IsSevere(row, problem) ? 0 : 1;
        }

        return 2;
    }

    private static TimeSpan RadarAlertAge(FlatResourceRow row)
    {
        return ResourceFilterMatcher.ParseHumanDuration(row.LastChange)
               ?? ResourceFilterMatcher.ParseHumanDuration(row.Age)
               ?? TimeSpan.MaxValue;
    }

    private bool StartRadarAutoFollow(RadarPoint worldCenter, double targetZoom)
    {
        var nextPanX = -worldCenter.X;
        var nextPanY = -worldCenter.Y;
        if (Math.Abs(radarZoom - targetZoom) < 0.001
            && Math.Abs(radarPanX - nextPanX) < 0.001
            && Math.Abs(radarPanY - nextPanY) < 0.001)
        {
            return false;
        }

        radarAutoFollowStartPanX = radarPanX;
        radarAutoFollowStartPanY = radarPanY;
        radarAutoFollowStartZoom = radarZoom;
        radarAutoFollowTargetZoom = targetZoom;
        radarAutoFollowTargetPanX = nextPanX;
        radarAutoFollowTargetPanY = nextPanY;
        radarAutoFollowStep = 0;
        IsRadarWaterPaused = true;
        radarWaterPauseTimer.Stop();
        radarAutoFollowTimer.Stop();
        radarAutoFollowTimer.Start();
        return true;
    }

    private void StartOrQueueRadarAutoFollow(RadarAutoFollowRequest request)
    {
        if (radarAutoFollowTimer.IsEnabled)
        {
            radarAutoFollowQueue.Enqueue(request);
            return;
        }

        if (!StartRadarAutoFollow(request.WorldCenter, request.TargetZoom)
            && radarAutoFollowQueue.TryDequeue(out var next))
        {
            StartRadarAutoFollow(next.WorldCenter, next.TargetZoom);
        }
    }

    private void StepRadarAutoFollow()
    {
        const int steps = 18;
        radarAutoFollowStep++;
        var progress = Math.Clamp(radarAutoFollowStep / (double)steps, 0, 1);
        var eased = 1 - Math.Pow(1 - progress, 3);
        radarPanX = Lerp(radarAutoFollowStartPanX, radarAutoFollowTargetPanX, eased);
        radarPanY = Lerp(radarAutoFollowStartPanY, radarAutoFollowTargetPanY, eased);
        radarZoom = Lerp(radarAutoFollowStartZoom, radarAutoFollowTargetZoom, eased);
        OnPropertyChanged(nameof(RadarPanX));
        OnPropertyChanged(nameof(RadarPanY));
        OnPropertyChanged(nameof(RadarZoom));
        OnPropertyChanged(nameof(RadarZoomLabel));
        UpdateRadarFromCache();
        if (radarAutoFollowStep < steps)
        {
            return;
        }

        radarAutoFollowTimer.Stop();
        while (radarAutoFollowQueue.TryDequeue(out var next))
        {
            if (StartRadarAutoFollow(next.WorldCenter, next.TargetZoom))
            {
                return;
            }
        }

        IsRadarWaterPaused = false;
    }

    private static double Lerp(double start, double end, double progress)
    {
        return start + (end - start) * progress;
    }

    private static IReadOnlyList<RadarPoint> ClusterCenters(int count)
    {
        if (count <= 1)
        {
            return [new RadarPoint(0, 0)];
        }

        var points = new List<RadarPoint>(count);
        var radiusX = Math.Min(84, 18 + count * 12);
        var radiusY = Math.Min(54, 14 + count * 7);
        for (var index = 0; index < count; index++)
        {
            var angle = -Math.PI / 2 + index * Math.PI * 2 / count;
            points.Add(new RadarPoint(
                Math.Cos(angle) * radiusX,
                Math.Sin(angle) * radiusY));
        }

        return points;
    }

    private static double NamespaceAngle(string cluster, string ns, int index, int count)
    {
        var baseAngle = -Math.PI / 2 + index * Math.PI * 2 / Math.Max(1, count);
        return baseAngle + StableRange(cluster + "/" + ns + ":namespace-angle", -0.16, 0.16);
    }

    private static int RadarTerrainRing(FlatResourceRow row)
    {
        return row.Kind switch
        {
            "Cluster" => 0,
            "Node" or "PersistentVolume" or "StorageClass" or "CustomResourceDefinition" or "GatewayClass" => 1,
            "Namespace" => 2,
            "ConfigMap" or "Secret" or "ServiceAccount" or "PersistentVolumeClaim" => 3,
            "Deployment" or "StatefulSet" or "DaemonSet" or "Job" or "CronJob" or "ReplicaSet" => 4,
            "Pod" or "Service" or "EndpointSlice" => 5,
            "Ingress" or "Gateway" or "HTTPRoute" or "GRPCRoute" or "NetworkPolicy" => 6,
            "Event" => 7,
            _ => 4
        };
    }

    private static RadarRect PlaceRadarRect(RadarPoint target, HashSet<RadarGridCell> occupied, string key)
    {
        var targetCell = RadarCellFromPoint(target);
        if (occupied.Add(targetCell))
        {
            return RectFromCell(targetCell);
        }

        var maxRadius = occupied.Count + 2;
        for (var radius = 1; radius < maxRadius; radius++)
        {
            foreach (var candidate in RadarRingCells(targetCell, radius)
                         .OrderBy(cell => RadarCellDistanceSquared(cell, targetCell))
                         .ThenBy(cell => StableHash($"{key}:{cell.Column}:{cell.Row}")))
            {
                if (occupied.Add(candidate))
                {
                    return RectFromCell(candidate);
                }
            }
        }

        return RectFromCell(targetCell);
    }

    private static IEnumerable<RadarGridCell> RadarRingCells(RadarGridCell center, int radius)
    {
        for (var dx = -radius; dx <= radius; dx++)
        {
            foreach (var dy in new[] { -radius, radius })
            {
                var cell = new RadarGridCell(center.Column + dx, center.Row + dy);
                yield return cell;
            }
        }

        for (var dy = -radius + 1; dy <= radius - 1; dy++)
        {
            foreach (var dx in new[] { -radius, radius })
            {
                var cell = new RadarGridCell(center.Column + dx, center.Row + dy);
                yield return cell;
            }
        }
    }

    private static double RadarCellDistanceSquared(RadarGridCell left, RadarGridCell right)
    {
        var dx = left.Column - right.Column;
        var dy = left.Row - right.Row;
        return dx * dx + dy * dy;
    }

    private static RadarGridCell RadarCellFromPoint(RadarPoint point)
    {
        var column = (int)Math.Round(point.X / RadarGridStep);
        var row = (int)Math.Round(point.Y / RadarGridStep);
        return new RadarGridCell(column, row);
    }

    private static RadarRect RectFromCell(RadarGridCell cell)
    {
        return new RadarRect(
            cell.Column * RadarGridStep - RadarTileVisualSize / 2,
            cell.Row * RadarGridStep - RadarTileVisualSize / 2,
            RadarTileVisualSize,
            RadarTileVisualSize);
    }

    private RadarRect ProjectRadarRect(RadarRect worldRect)
    {
        return new RadarRect(
            radarCanvasWidth / 2 + (worldRect.X + radarPanX) * radarZoom,
            radarCanvasHeight / 2 + (worldRect.Y + radarPanY) * radarZoom,
            worldRect.W * radarZoom,
            worldRect.H * radarZoom);
    }

    private bool IsRadarRectVisible(RadarRect rect)
    {
        return rect.X < radarCanvasWidth
               && rect.X + rect.W > 0
               && rect.Y < radarCanvasHeight
               && rect.Y + rect.H > 0;
    }

    private static RadarPoint RectCenter(RadarRect rect)
    {
        return new RadarPoint(rect.X + rect.W / 2, rect.Y + rect.H / 2);
    }

    private static FlatResourceRow RadarVirtualRow(string kind, string name, string? ns, string cluster)
    {
        return new FlatResourceRow(
            "radar:" + cluster + ":" + (ns ?? "cluster") + ":" + kind + ":" + name,
            "Observed",
            kind,
            name,
            ns,
            cluster,
            "now",
            "-",
            0,
            null,
            "-",
            null,
            "-",
            FreshnessState.Fresh);
    }

    private static IEnumerable<FlatResourceRow> OrderedRadarRows(IEnumerable<FlatResourceRow> rows)
    {
        return rows
            .OrderBy(RadarDependencyRank)
            .ThenBy(row => row.Owner ?? string.Empty, StringComparer.Ordinal)
            .ThenBy(row => row.Kind, StringComparer.Ordinal)
            .ThenBy(row => row.Name, StringComparer.Ordinal);
    }

    private static int RadarDependencyRank(FlatResourceRow row)
    {
        return row.Kind switch
        {
            "Cluster" => 0,
            "Namespace" => 1,
            "Node" => 2,
            "PersistentVolume" or "StorageClass" => 3,
            "PersistentVolumeClaim" => 4,
            "ConfigMap" or "Secret" or "ServiceAccount" => 5,
            "Deployment" or "StatefulSet" or "DaemonSet" or "Job" or "CronJob" => 6,
            "ReplicaSet" => 7,
            "Pod" => 8,
            "Service" or "EndpointSlice" => 9,
            "Ingress" or "Gateway" or "HTTPRoute" or "NetworkPolicy" => 10,
            "Event" => 11,
            _ => 12
        };
    }

    private static bool IsRadarEvent(FlatResourceRow row)
    {
        return row.Kind.Equals("Event", StringComparison.OrdinalIgnoreCase);
    }

    private static double StableRange(string key, double min, double max)
    {
        return min + (StableHash(key) % 10_000) / 9_999d * (max - min);
    }

    private static int StableHash(string value)
    {
        var hash = 17;
        foreach (var ch in value)
        {
            hash = unchecked(hash * 31 + ch);
        }

        return hash & 0x7fffffff;
    }

    private void UpdateRadarSelection()
    {
        foreach (var block in RadarBlocks)
        {
            block.SetSelected(!block.IsPlaceholder && block.Resource.Id == selectedRadarResourceId);
        }
    }

    private void AdvanceRadarIdleLife()
    {
        if (!IsRadarIdle)
        {
            radarIdleTimer.Stop();
            return;
        }

        StepRadarLife();
        RenderRadarLife(reset: false);
    }

    private void UpdateRadarIdleTimer()
    {
        // The radar screensaver intentionally keeps animating while the window is inactive — that is when a
        // screensaver is most useful. It stops only when there is real radar data, the setting is disabled, or
        // the window is minimized (nothing is on screen to animate, so there is no reason to repaint).
        if (!IsRadarIdle || !state.Settings().ScreensaverEnabled || !isWindowVisible)
        {
            radarIdleTimer.Stop();
        }
        else if (!radarIdleTimer.IsEnabled)
        {
            radarIdleTimer.Start();
        }
    }

    private IEnumerable<RadarGridCell> VisibleRadarCells()
    {
        var left = -radarCanvasWidth / 2 / radarZoom - radarPanX - RadarGridStep;
        var right = radarCanvasWidth / 2 / radarZoom - radarPanX + RadarGridStep;
        var top = -radarCanvasHeight / 2 / radarZoom - radarPanY - RadarGridStep;
        var bottom = radarCanvasHeight / 2 / radarZoom - radarPanY + RadarGridStep;
        var minColumn = (int)Math.Floor(left / RadarGridStep);
        var maxColumn = (int)Math.Ceiling(right / RadarGridStep);
        var minRow = (int)Math.Floor(top / RadarGridStep);
        var maxRow = (int)Math.Ceiling(bottom / RadarGridStep);

        for (var row = minRow; row <= maxRow; row++)
        {
            for (var column = minColumn; column <= maxColumn; column++)
            {
                yield return new RadarGridCell(column, row);
            }
        }
    }

    private void RenderRadarLife(bool reset)
    {
        if (reset || radarLifeCells.Count == 0)
        {
            ResetRadarLife();
        }

        var cellWidth = radarCanvasWidth / RadarLifeColumns;
        var cellHeight = radarCanvasHeight / RadarLifeRows;
        var desired = new List<RadarIdleCellViewModel>(radarLifeCells.Count);
        foreach (var cell in radarLifeCells
                     .OrderBy(cell => cell.Y)
                     .ThenBy(cell => cell.X))
        {
            desired.Add(new RadarIdleCellViewModel(
                cell.X * cellWidth,
                cell.Y * cellHeight,
                Math.Max(1, cellWidth),
                Math.Max(1, cellHeight),
                LifeCellBrush(cell)));
        }

        SyncRadarIdleCells(desired);
        if (RadarBlocks.Count > 0)
        {
            RadarBlocks.Clear();
        }

        IsRadarIdle = true;
        UpdateRadarIdleTimer();
    }

    private void SyncRadarIdleCells(IReadOnlyList<RadarIdleCellViewModel> desired)
    {
        var shared = Math.Min(RadarIdleCells.Count, desired.Count);
        for (var index = 0; index < shared; index++)
        {
            RadarIdleCells[index].UpdateFrom(desired[index]);
        }

        while (RadarIdleCells.Count > desired.Count)
        {
            RadarIdleCells.RemoveAt(RadarIdleCells.Count - 1);
        }

        for (var index = RadarIdleCells.Count; index < desired.Count; index++)
        {
            RadarIdleCells.Add(desired[index]);
        }
    }

    private void ResetRadarLife()
    {
        radarLifeCells.Clear();
        radarLifeGeneration = 0;
        radarLifeSeed = radarLifeRandom.Next(1, int.MaxValue);
        radarLifeRuleIndex = radarLifeRandom.Next(RadarLifeRules.Length);
        radarLifeStagnantGenerations = 0;
        radarLifeLastSignature = string.Empty;
        radarLifeSeenSignatures.Clear();

        // Conway radar traffic: gliders, lightweight ships, beacons, a gear-like oscillator, and sparse noise.
        var patterns = new IReadOnlyList<(int X, int Y)>[]
        {
            [(1, 0), (2, 1), (0, 2), (1, 2), (2, 2)],
            [(1, 0), (4, 0), (0, 1), (0, 2), (4, 2), (0, 3), (1, 3), (2, 3), (3, 3)],
            [(0, 0), (1, 0), (0, 1), (3, 2), (2, 3), (3, 3)],
            [(1, 0), (2, 0), (0, 1), (3, 1), (1, 2), (2, 2)],
            [(2, 0), (3, 0), (4, 0), (8, 0), (9, 0), (10, 0), (0, 2), (5, 2), (7, 2), (12, 2), (0, 3), (5, 3), (7, 3), (12, 3), (0, 4), (5, 4), (7, 4), (12, 4), (2, 5), (3, 5), (4, 5), (8, 5), (9, 5), (10, 5), (2, 7), (3, 7), (4, 7), (8, 7), (9, 7), (10, 7), (0, 8), (5, 8), (7, 8), (12, 8), (0, 9), (5, 9), (7, 9), (12, 9), (0, 10), (5, 10), (7, 10), (12, 10), (2, 12), (3, 12), (4, 12), (8, 12), (9, 12), (10, 12)]
        };
        var placements = radarLifeRandom.Next(7, 12);
        for (var index = 0; index < placements; index++)
        {
            var pattern = patterns[radarLifeRandom.Next(patterns.Length)];
            AddRadarLifePattern(
                radarLifeRandom.Next(RadarLifeColumns),
                radarLifeRandom.Next(RadarLifeRows),
                pattern);
        }

        var noise = radarLifeRandom.Next(14, 30);
        for (var index = 0; index < noise; index++)
        {
            radarLifeCells.Add(new RadarLifeCell(
                radarLifeRandom.Next(RadarLifeColumns),
                radarLifeRandom.Next(RadarLifeRows)));
        }

        OnPropertyChanged(nameof(RadarIdleSeed));
        OnPropertyChanged(nameof(RadarIdleRuleLabel));
    }

    private void AddRadarLifePattern(int offsetX, int offsetY, IReadOnlyList<(int X, int Y)> cells)
    {
        foreach (var (x, y) in cells)
        {
            radarLifeCells.Add(new RadarLifeCell(
                Mod(offsetX + x, RadarLifeColumns),
                Mod(offsetY + y, RadarLifeRows)));
        }
    }

    private void StepRadarLife()
    {
        var neighborCounts = new Dictionary<RadarLifeCell, int>();
        foreach (var cell in radarLifeCells)
        {
            for (var dy = -1; dy <= 1; dy++)
            {
                for (var dx = -1; dx <= 1; dx++)
                {
                    if (dx == 0 && dy == 0)
                    {
                        continue;
                    }

                    var neighbor = new RadarLifeCell(
                        Mod(cell.X + dx, RadarLifeColumns),
                        Mod(cell.Y + dy, RadarLifeRows));
                    neighborCounts[neighbor] = neighborCounts.GetValueOrDefault(neighbor) + 1;
                }
            }
        }

        var rule = RadarLifeRules[radarLifeRuleIndex];
        var next = neighborCounts
            .Where(pair => rule.ShouldLive(radarLifeCells.Contains(pair.Key), pair.Value))
            .Select(pair => pair.Key)
            .ToHashSet();
        var signature = RadarLifeSignature(next);
        radarLifeStagnantGenerations = signature == radarLifeLastSignature
            ? radarLifeStagnantGenerations + 1
            : 0;
        radarLifeLastSignature = signature;
        radarLifeSeenSignatures[signature] = radarLifeSeenSignatures.GetValueOrDefault(signature) + 1;
        radarLifeCells.Clear();
        foreach (var cell in next)
        {
            radarLifeCells.Add(cell);
        }

        radarLifeGeneration++;
        if (radarLifeCells.Count == 0
            || radarLifeCells.Count < 8
            || radarLifeCells.Count > RadarLifeColumns * RadarLifeRows * 0.62
            || radarLifeStagnantGenerations >= 10
            || radarLifeSeenSignatures.GetValueOrDefault(signature) >= 4
            || radarLifeGeneration >= 240)
        {
            ResetRadarLife();
        }
    }

    private static string RadarLifeSignature(IEnumerable<RadarLifeCell> cells)
    {
        return string.Join(';', cells
            .OrderBy(cell => cell.Y)
            .ThenBy(cell => cell.X)
            .Select(cell => $"{cell.X},{cell.Y}"));
    }

    private static int Mod(int value, int modulo)
    {
        var result = value % modulo;
        return result < 0 ? result + modulo : result;
    }

    private static IBrush LifeCellBrush(RadarLifeCell cell)
    {
        return RadarLifeBrushes[cell.X % RadarLifeBrushes.Length];
    }

    private void ScheduleRefresh()
    {
        if (disposed)
        {
            return;
        }
        refreshDebounce?.Cancel();
        refreshDebounce?.Dispose();
        refreshDebounce = new CancellationTokenSource();
        _ = DebouncedRefresh(refreshDebounce.Token);
    }

    private void OnRemoteFilterChanged()
    {
        if (applyingPreset)
        {
            return;
        }

        MarkUserActivity();
        ApplyLocalFilter();
        ScheduleRefresh();
    }

    private void OnLocalFilterChanged()
    {
        if (applyingPreset)
        {
            return;
        }

        MarkUserActivity();
        ApplyLocalFilter();
    }

    private void ApplyPreset(FilterPreset preset)
    {
        applyingPreset = true;
        try
        {
            ProblemsOnly = preset.ProblemsOnly;
            Search = preset.Search ?? string.Empty;
            IdPicker.SetExpression(string.Empty);
            IssuePicker.SetExpression(preset.Issue ?? string.Empty);
            KindPicker.SetExpression(preset.Kind ?? string.Empty);
            NamePicker.SetExpression(preset.NameFilter ?? string.Empty);
            NamespacePicker.SetExpression(preset.Namespace ?? string.Empty);
            ClusterPicker.SetExpression(preset.Cluster ?? string.Empty);
            StatusPicker.SetExpression(preset.Status ?? string.Empty);
            AgePicker.SetExpression(preset.Age ?? string.Empty);
            NodePicker.SetExpression(preset.Node ?? string.Empty);
            ImagePicker.SetExpression(preset.Image ?? string.Empty);
            ReadyPicker.SetExpression(preset.Ready ?? string.Empty);
            RestartFilter = preset.Restarts ?? string.Empty;
            CpuPicker.SetExpression(preset.Cpu ?? string.Empty);
            MemoryPicker.SetExpression(preset.Memory ?? string.Empty);
            StoragePicker.SetExpression(preset.Storage ?? string.Empty);
            OwnerPicker.SetExpression(preset.Owner ?? string.Empty);
            LimitText = string.IsNullOrWhiteSpace(preset.Limit) ? "256" : preset.Limit;
            ActivityOnly = preset.ActivityOnly;
            PresetName = preset.Name;
        }
        finally
        {
            applyingPreset = false;
        }

        ApplyLocalFilter();
        ScheduleRefresh();
    }

    private async Task DebouncedRefresh(CancellationToken cancellationToken)
    {
        try
        {
            await Task.Delay(TimeSpan.FromMilliseconds(650), cancellationToken).ConfigureAwait(false);
            await Dispatcher.UIThread.InvokeAsync(async () => await RefreshResourcesAsync(false, false).ConfigureAwait(true));
        }
        catch (OperationCanceledException)
        {
        }
    }

    private void ScheduleLocalFilter()
    {
        filterDebounce?.Cancel();
        filterDebounce?.Dispose();
        filterDebounce = new CancellationTokenSource();
        _ = DebouncedLocalFilter(filterDebounce.Token);
    }

    private async Task DebouncedLocalFilter(CancellationToken cancellationToken)
    {
        try
        {
            await Task.Delay(TimeSpan.FromMilliseconds(80), cancellationToken).ConfigureAwait(false);
            await Dispatcher.UIThread.InvokeAsync(ApplyLocalFilter);
        }
        catch (OperationCanceledException)
        {
        }
    }

    private void ScheduleFocusLoad()
    {
        focusDebounce?.Cancel();
        focusDebounce?.Dispose();
        focusDebounce = new CancellationTokenSource();
        _ = DebouncedFocus(focusDebounce.Token);
    }

    private async Task DebouncedFocus(CancellationToken cancellationToken)
    {
        try
        {
            await Task.Delay(TimeSpan.FromMilliseconds(320), cancellationToken).ConfigureAwait(false);
            await Dispatcher.UIThread.InvokeAsync(async () => await OpenSelectedResourceAsync().ConfigureAwait(true));
        }
        catch (OperationCanceledException)
        {
        }
    }

    private async Task BackgroundRefreshLoop(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                await Task.Delay(BackgroundRefreshInterval(), cancellationToken).ConfigureAwait(false);
                await Dispatcher.UIThread.InvokeAsync(async () =>
                {
                    if (ShouldRunBackgroundRefresh())
                    {
                        await RefreshResourcesAsync(true, true).ConfigureAwait(true);
                    }
                });
            }
            catch (OperationCanceledException)
            {
                return;
            }
        }
    }

    private TimeSpan BackgroundRefreshInterval()
    {
        var idle = DateTimeOffset.Now - lastUserActivityAt;
        if (IsInactiveForBackgroundSync(idle))
        {
            return InactiveBackgroundCheckInterval(state.Settings().InactiveSyncMinutes, lastSyncedAt, DateTimeOffset.Now);
        }

        return BackgroundRefreshIntervalFor(
            isAppFocused,
            SelectedSession is not null,
            cachedRows.Count > 0,
            Failures.Count > 0,
            idle);
    }

    private bool ShouldRunBackgroundRefresh()
    {
        if (SelectedSession is null)
        {
            return false;
        }

        var idle = DateTimeOffset.Now - lastUserActivityAt;
        if (!IsInactiveForBackgroundSync(idle))
        {
            return true;
        }

        var minutes = state.Settings().InactiveSyncMinutes;
        if (minutes > 0
            && (lastSyncedAt is null || DateTimeOffset.Now - lastSyncedAt.Value >= TimeSpan.FromMinutes(minutes)))
        {
            return true;
        }

        return lastSyncedAt is null || DateTimeOffset.Now - lastSyncedAt.Value >= MinimumBackgroundCadence;
    }

    private bool IsInactiveForBackgroundSync(TimeSpan idle)
    {
        return !isAppFocused || idle >= TimeSpan.FromMinutes(5);
    }

    private static readonly TimeSpan MinimumBackgroundCadence = TimeSpan.FromSeconds(60);

    internal static TimeSpan InactiveBackgroundCheckInterval(
        int inactiveSyncMinutes,
        DateTimeOffset? lastSynced,
        DateTimeOffset now)
    {
        if (inactiveSyncMinutes <= 0)
        {
            return MinimumBackgroundCadence;
        }

        if (lastSynced is null)
        {
            return TimeSpan.FromSeconds(1);
        }

        var remaining = TimeSpan.FromMinutes(inactiveSyncMinutes) - (now - lastSynced.Value);
        if (remaining <= TimeSpan.Zero)
        {
            return TimeSpan.FromSeconds(1);
        }

        return remaining < MinimumBackgroundCadence ? remaining : MinimumBackgroundCadence;
    }

    internal static TimeSpan BackgroundRefreshIntervalFor(
        bool focused,
        bool hasSession,
        bool hasCachedRows,
        bool hasFailures,
        TimeSpan idle)
    {
        if (!focused)
        {
            if (!hasSession)
            {
                return TimeSpan.FromSeconds(90);
            }

            return hasCachedRows ? TimeSpan.FromMinutes(4) : TimeSpan.FromSeconds(45);
        }

        if (!hasSession)
        {
            return TimeSpan.FromSeconds(45);
        }

        if (!hasCachedRows)
        {
            return hasFailures ? TimeSpan.FromSeconds(25) : TimeSpan.FromSeconds(12);
        }

        if (idle < TimeSpan.FromSeconds(30))
        {
            return TimeSpan.FromSeconds(20);
        }

        return idle < TimeSpan.FromMinutes(5)
            ? TimeSpan.FromSeconds(45)
            : TimeSpan.FromSeconds(120);
    }

    private TimeSpan LogTailInterval()
    {
        if (!isAppFocused)
        {
            return TimeSpan.FromSeconds(30);
        }

        var idle = DateTimeOffset.Now - lastUserActivityAt;
        return idle < TimeSpan.FromMinutes(1) ? TimeSpan.FromSeconds(5) : TimeSpan.FromSeconds(15);
    }

    private void MarkUserActivity()
    {
        lastUserActivityAt = DateTimeOffset.Now;
        UpdateRequestWorkLabel();
    }

    private DateTimeOffset initialLoadStartedAt = DateTimeOffset.MinValue;
    private int initialLoadExpectedTotal;

    private void UpdateRequestWorkLabel()
    {
        var telemetry = service.RequestTelemetry();
        var backoff = telemetry.BackoffUntil is { } until && until > DateTimeOffset.UtcNow
            ? $" backoff {Math.Max(0, (int)(until - DateTimeOffset.UtcNow).TotalSeconds)}s"
            : string.Empty;
        RadarWaterActivityRate = telemetry.RequestsLastMinute;
        RequestWorkLabel = $"API {telemetry.RequestsLastMinute}/min {telemetry.RequestsPerSecond:0.00}/s Q{telemetry.QueuedRequests}{backoff}";
        if (IsInitialLoading)
        {
            UpdateLoadingHealthSegments();
        }
        OnPropertyChanged(nameof(InitialLoadPercent));
        if (IsSettingsWorkspace)
        {
            UpdateRequestAuditRows();
        }

        OnPropertyChanged(nameof(FooterLine));
    }

    public double InitialLoadPercent
    {
        get
        {
            if (!IsInitialLoading || initialLoadExpectedTotal <= 0)
            {
                return 0;
            }
            var completed = service.CompletedRequestsSinceStart(initialLoadStartedAt);
            return Math.Clamp(completed / (double)initialLoadExpectedTotal * 100d, 0d, 100d);
        }
    }

    internal void SetInitialLoadProgressForTests(DateTimeOffset start, int expectedTotal)
    {
        initialLoadStartedAt = start;
        initialLoadExpectedTotal = expectedTotal;
    }

    internal void SimulateTimerTickForTests()
    {
        RefreshTimeLabels();
    }

    internal void ExpireAlertAnimationsForTests()
    {
        ExpireAlertAnimations();
    }

    internal void PlayNextQueuedAlertSoundForTests()
    {
        PlayNextQueuedAlertSound();
    }

    internal int RadarAutoFollowQueueCountForTests => radarAutoFollowQueue.Count;

    internal void StepRadarAutoFollowForTests()
    {
        StepRadarAutoFollow();
    }

    internal void UpdateLoadingHealthSegments(int _ignored = 0)
    {
        const int tickCount = 30;
        var percent = InitialLoadPercent;
        var lit = (int)Math.Round(percent / 100d * tickCount);
        lit = Math.Clamp(lit, 0, tickCount);
        var litBrush = AppThemeCatalog.StatusBrush("HEALTHY");
        var unlitBrush = AppThemeCatalog.StatusBrush("UNKNOWN");
        var tickHeight = 100d / tickCount;
        if (HealthSegments.Count != tickCount)
        {
            HealthSegments.Clear();
            for (var i = 0; i < tickCount; i++)
            {
                HealthSegments.Add(new HealthSegmentViewModel("PENDING", 0, 0, tickHeight, unlitBrush));
            }
        }

        for (var i = 0; i < tickCount; i++)
        {
            var fromBottom = tickCount - i;
            var isLit = fromBottom <= lit;
            var desiredState = isLit ? "LOADING" : "PENDING";
            var desiredBrush = isLit ? litBrush : unlitBrush;
            if (HealthSegments[i].State != desiredState)
            {
                HealthSegments[i] = new HealthSegmentViewModel(desiredState, 0, 0, tickHeight, desiredBrush);
            }
        }
        HealthSummary = $"Loading resources from cluster… {(int)percent}% ({service.CompletedRequestsSinceStart(initialLoadStartedAt)}/{initialLoadExpectedTotal})";
    }

    private void RefreshPortForwardBadges()
    {
        portForwardBadgeVersion++;
        OnPropertyChanged(nameof(PortForwardBadgeVersion));
        OnPropertyChanged(nameof(VisiblePortForwards));
    }

    private void UpdateRequestAuditRows()
    {
        var rows = service.RequestAuditLog()
            .Select(entry => new RequestAuditRow(
                entry.StartedAt,
                entry.Method,
                entry.Path,
                entry.Priority,
                entry.Status,
                entry.Duration,
                entry.Outcome))
            .ToList();
        SyncCollection(RequestAuditRows, rows);
    }

    private static NamespaceScope ScopeFromText(string text)
    {
        var trimmed = text.Trim();
        if (trimmed.Length == 0 || trimmed.Equals("all", StringComparison.OrdinalIgnoreCase) || trimmed.Equals("all sectors", StringComparison.OrdinalIgnoreCase))
        {
            return NamespaceScope.All;
        }

        return NamespaceScope.Many(trimmed.Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries));
    }

    private static int GuessPort(FlatResourceRow row)
    {
        if (row.Kind == "Service")
        {
            return 80;
        }

        if (row.ImageSummary.Contains("nginx", StringComparison.OrdinalIgnoreCase)
            || row.ImageSummary.Contains("http", StringComparison.OrdinalIgnoreCase))
        {
            return 80;
        }

        return 8080;
    }

    private static IReadOnlyList<int> DeclaredPortsFromYaml(string kind, string yaml)
    {
        var key = kind == "Service" ? "port" : "containerPort";
        return Regex.Matches(yaml, $@"^\s*{Regex.Escape(key)}:\s*(\d+)\s*$", RegexOptions.Multiline)
            .Select(match => int.TryParse(match.Groups[1].Value, out var port) ? port : 0)
            .Where(port => port > 0)
            .Distinct()
            .Order()
            .ToList();
    }

    private static int FreePort(int preferred)
    {
        if (preferred > 0 && IsPortFree(preferred))
        {
            return preferred;
        }

        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        return ((IPEndPoint)listener.LocalEndpoint).Port;
    }

    private static bool IsPortFree(int port)
    {
        try
        {
            using var listener = new TcpListener(IPAddress.Loopback, port);
            listener.Start();
            return true;
        }
        catch (SocketException)
        {
            return false;
        }
    }

    private static string ResolveTool(string executable, IReadOnlyList<string> fallbacks)
    {
        var path = Environment.GetEnvironmentVariable("PATH") ?? string.Empty;
        foreach (var directory in path.Split(Path.PathSeparator).Where(value => value.Length > 0))
        {
            var candidate = Path.Combine(directory, executable);
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }

        foreach (var fallback in fallbacks)
        {
            if (File.Exists(fallback))
            {
                return fallback;
            }
        }

        throw new InvalidOperationException($"{executable} was not found.");
    }

    private sealed record ProcessResult(int ExitCode, string Output, string Error);

    private int ParseLimit()
    {
        return int.TryParse(LimitText.Trim(), out var limit) ? ResourceFilterMatcher.NormalizeLimit(limit) : 256;
    }

    private static string HumanSince(DateTimeOffset then)
    {
        var age = DateTimeOffset.Now - then;
        if (age.TotalSeconds < 60)
        {
            return $"{Math.Max(0, (int)age.TotalSeconds)}s";
        }

        if (age.TotalMinutes < 60)
        {
            return $"{(int)age.TotalMinutes}m";
        }

        return $"{(int)age.TotalHours}h";
    }

    private static TimeSpan AgeDuration(string age)
    {
        if (string.IsNullOrWhiteSpace(age) || age == "-")
        {
            return TimeSpan.MaxValue;
        }

        var index = 0;
        var total = TimeSpan.Zero;
        while (index < age.Length)
        {
            var start = index;
            while (index < age.Length && char.IsDigit(age[index]))
            {
                index++;
            }

            if (start == index || index >= age.Length)
            {
                break;
            }

            if (!int.TryParse(age[start..index], out var amount))
            {
                break;
            }

            var unit = age[index++];
            total += unit switch
            {
                'd' => TimeSpan.FromDays(amount),
                'h' => TimeSpan.FromHours(amount),
                'm' => TimeSpan.FromMinutes(amount),
                's' => TimeSpan.FromSeconds(amount),
                _ => TimeSpan.Zero
            };
        }

        return total == TimeSpan.Zero && !age.StartsWith('0') ? TimeSpan.MaxValue : total;
    }

    private static string? EmptyToNull(string value)
    {
        return string.IsNullOrWhiteSpace(value) ? null : value.Trim();
    }

    private bool SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        OnPropertyChanged(propertyName);
        return true;
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }

    private enum ResourceSortDirection
    {
        None,
        Descending,
        Ascending
    }

    private enum SelectionSurface
    {
        Resource,
        Table,
        Radar,
        Graph
    }

    private sealed record ActiveRadarAlertMatch(
        string RuleId,
        IReadOnlyList<FlatResourceRow> Rows,
        AlertRuleActions Actions);

    private readonly record struct RadarAutoFollowRequest(RadarPoint WorldCenter, double TargetZoom);

    private sealed class ResourceSortValueComparer : IComparer<IComparable>
    {
        public static ResourceSortValueComparer Instance { get; } = new();

        public int Compare(IComparable? x, IComparable? y)
        {
            if (ReferenceEquals(x, y))
            {
                return 0;
            }

            if (x is null)
            {
                return -1;
            }

            if (y is null)
            {
                return 1;
            }

            return x.GetType() == y.GetType()
                ? x.CompareTo(y)
                : string.Compare(x.ToString(), y.ToString(), StringComparison.OrdinalIgnoreCase);
        }
    }
}
