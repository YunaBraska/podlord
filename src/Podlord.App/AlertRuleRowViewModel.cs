using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using Avalonia;
using Avalonia.Media;
using Podlord.Core;

namespace Podlord.App;

public sealed class AlertRuleRowViewModel : INotifyPropertyChanged
{
    private const string DurationOnceChoice = "once";
    private const string DurationUntilChangeChoice = "until change";
    private static readonly IReadOnlyList<string> FieldChoicesValue =
    [
        "Kind", "Namespace", "Name", "Status", "Issue", "Ready", "Restarts", "Age",
        "CPU", "Memory", "Storage", "Node", "Image", "Owner", "Cluster", "Freshness",
        "Problems", "Error", "Active", "Recently changed", "New in view", "Event reason", "Event message", "ID"
    ];
    private static readonly IReadOnlyList<string> DurationChoicesValue =
        [DurationOnceChoice, DurationUntilChangeChoice, .. Enumerable.Range(1, 60).Select(second => $"{second}s")];
    private string name;
    private string description;
    private bool enabled;
    private string soundId;
    private string colorChoice;
    private string colorUntilMode;
    private string colorUntilDuration;
    private string animationChoice;
    private string animationUntilMode;
    private string animationUntilDuration;
    private string zoomChoice;
    private string soundSearch = string.Empty;
    private string activeSummary = string.Empty;
    private readonly int soundMinimumMatches;

    public AlertRuleRowViewModel(AlertRule rule)
    {
        Id = rule.Id;
        BuiltIn = rule.BuiltIn;
        name = rule.Name;
        description = rule.Description;
        enabled = rule.Enabled;
        soundId = string.IsNullOrWhiteSpace(rule.SoundId) ? "none" : rule.SoundId;
        soundMinimumMatches = Math.Max(1, rule.Actions.SoundMinimumMatches);
        colorChoice = NormalizeChoice(rule.Actions.RadarColorValue, rule.Actions.RadarColor ? "status" : "none");
        colorUntilMode = NormalizeUntil(rule.Actions.RadarColorUntilMode, rule.Until.Mode);
        colorUntilDuration = string.IsNullOrWhiteSpace(rule.Actions.RadarColorUntilDuration) ? rule.Until.Duration : rule.Actions.RadarColorUntilDuration;
        animationChoice = NormalizeChoice(rule.Actions.RadarAnimation, rule.Actions.RadarBlink ? "blink" : "none");
        animationUntilMode = NormalizeUntil(rule.Actions.RadarAnimationUntilMode, rule.Until.Mode);
        animationUntilDuration = string.IsNullOrWhiteSpace(rule.Actions.RadarAnimationUntilDuration) ? rule.Until.Duration : rule.Actions.RadarAnimationUntilDuration;
        zoomChoice = rule.Actions.RadarZoomPercent > 0
            ? $"{rule.Actions.RadarZoomPercent}%"
            : rule.Actions.RadarZoom ? "100%" : "none";
        LoadGroups(rule);
        EnsureMinimumShape();
    }

    public string Id { get; }

    public bool BuiltIn { get; }

    public bool CanEdit => !BuiltIn;

    public bool CanDelete => !BuiltIn;

    public string LockState => BuiltIn ? "locked" : "";

    public bool IsCurrentlyActive => !string.IsNullOrWhiteSpace(ActiveSummary);

    public string ActiveSummary
    {
        get => activeSummary;
        private set
        {
            if (SetField(ref activeSummary, value))
            {
                OnPropertyChanged(nameof(IsCurrentlyActive));
                OnPropertyChanged(nameof(ActiveStateText));
            }
        }
    }

    public string ActiveStateText => IsCurrentlyActive ? ActiveSummary : "-";

    public string ActivationGlyphKind => Enabled ? "Visible" : "Hidden";

    public string ActivationActionText => Enabled ? "Disable alert" : "Enable alert";

    public string Name { get => name; set => SetEditable(ref name, value); }

    public string Description { get => description; set => SetEditable(ref description, value); }

    public bool Enabled
    {
        get => enabled;
        set
        {
            if (SetField(ref enabled, value))
            {
                OnPropertyChanged(nameof(ActivationGlyphKind));
                OnPropertyChanged(nameof(ActivationActionText));
            }
        }
    }

    public ObservableCollection<AlertMatcherGroupViewModel> MatcherGroups { get; } = [];

    public IReadOnlyList<string> FieldChoices => FieldChoicesValue;

    public IReadOnlyList<string> AnimationChoices { get; } = ["none", "blink", "pulse", "sweep", "outline"];

    public IReadOnlyList<string> UntilChoices { get; } = ["none", AlertUntilModes.NoMatch, AlertUntilModes.Duration, AlertUntilModes.NewInView];

    public IReadOnlyList<string> DurationChoices => DurationChoicesValue;

    public IReadOnlyList<string> ZoomChoices { get; } = ["none", "75%", "100%", "125%", "150%", "200%"];

    public string ColorChoice
    {
        get => colorChoice;
        set
        {
            if (SetEditable(ref colorChoice, NormalizeChoice(value, "none")))
            {
                OnPropertyChanged(nameof(ColorPreviewBrush));
                OnPropertyChanged(nameof(SelectedColor));
                OnPropertyChanged(nameof(ColorModeLabel));
                OnPropertyChanged(nameof(IsStatusColor));
                OnPropertyChanged(nameof(IsNoColor));
                OnPropertyChanged(nameof(AnimationPreviewBrush));
            }
        }
    }

    public Color SelectedColor
    {
        get => ColorFromChoice(ColorChoice);
        set
        {
            var hex = $"#{value.R:X2}{value.G:X2}{value.B:X2}";
            if (SetEditable(ref colorChoice, hex, nameof(ColorChoice)))
            {
                OnPropertyChanged(nameof(SelectedColor));
                OnPropertyChanged(nameof(ColorPreviewBrush));
                OnPropertyChanged(nameof(ColorModeLabel));
                OnPropertyChanged(nameof(IsStatusColor));
                OnPropertyChanged(nameof(IsNoColor));
                OnPropertyChanged(nameof(AnimationPreviewBrush));
            }
        }
    }

    public string ColorModeLabel => ColorChoice.Equals("none", StringComparison.OrdinalIgnoreCase)
        ? "X"
        : ColorChoice.Equals("status", StringComparison.OrdinalIgnoreCase)
            ? "STATUS"
            : ColorChoice;

    public bool IsStatusColor => ColorChoice.Equals("status", StringComparison.OrdinalIgnoreCase);

    public bool IsNoColor => ColorChoice.Equals("none", StringComparison.OrdinalIgnoreCase);

    public string ColorUntilMode { get => colorUntilMode; set => SetEditable(ref colorUntilMode, NormalizeUntil(value, "none")); }

    public string ColorUntilDuration { get => colorUntilDuration; set => SetEditable(ref colorUntilDuration, value); }

    public string ColorDurationChoice
    {
        get => DurationChoiceFrom(ColorUntilMode, ColorUntilDuration);
        set => SetDurationChoice(value, ref colorUntilMode, ref colorUntilDuration, nameof(ColorDurationChoice));
    }

    public string AnimationChoice
    {
        get => animationChoice;
        set
        {
            if (SetEditable(ref animationChoice, NormalizeChoice(value, "none")))
            {
                OnPropertyChanged(nameof(IsAnimationEnabled));
                OnPropertyChanged(nameof(AnimationPreviewBrush));
            }
        }
    }

    public string AnimationUntilMode { get => animationUntilMode; set => SetEditable(ref animationUntilMode, NormalizeUntil(value, "none")); }

    public string AnimationUntilDuration { get => animationUntilDuration; set => SetEditable(ref animationUntilDuration, value); }

    public string AnimationDurationChoice
    {
        get => DurationChoiceFrom(AnimationUntilMode, AnimationUntilDuration);
        set => SetDurationChoice(value, ref animationUntilMode, ref animationUntilDuration, nameof(AnimationDurationChoice));
    }

    public string ZoomChoice
    {
        get => zoomChoice;
        set
        {
            if (SetEditable(ref zoomChoice, NormalizeChoice(value, "none")))
            {
                OnPropertyChanged(nameof(HasZoom));
            }
        }
    }

    public string SoundId
    {
        get => soundId;
        set
        {
            if (SetEditable(ref soundId, AlertSoundCatalog.Resolve(value).Id))
            {
                NotifySoundChanged();
            }
        }
    }

    public string SoundLabel => AlertSoundCatalog.Resolve(SoundId).Label;

    public string SoundChoice
    {
        get => SoundLabel;
        set
        {
            var sound = AlertSoundCatalog.BuiltIn.FirstOrDefault(candidate => candidate.Label.Equals(value, StringComparison.Ordinal));
            SoundId = sound?.Id ?? value;
            OnPropertyChanged();
        }
    }

    public string SoundAttribution => HasSound ? AlertSoundCatalog.Resolve(SoundId).Attribution : string.Empty;

    public string SoundSourceUrl => HasSound ? AlertSoundCatalog.Resolve(SoundId).SourceUrl : string.Empty;

    public string SoundAsset => AlertSoundCatalog.Resolve(SoundId).Asset;

    public bool HasSound => !SoundId.Equals("none", StringComparison.OrdinalIgnoreCase);

    public bool HasZoom => !ZoomChoice.Equals("none", StringComparison.OrdinalIgnoreCase);

    public string SoundSearch
    {
        get => soundSearch;
        set
        {
            if (SetField(ref soundSearch, value))
            {
                OnPropertyChanged(nameof(FilteredSoundChoices));
                OnPropertyChanged(nameof(FilteredSoundItems));
            }
        }
    }

    public IReadOnlyList<string> FilteredSoundChoices
    {
        get
        {
            if (string.IsNullOrWhiteSpace(SoundSearch))
            {
                return AlertSoundCatalog.BuiltIn.Select(sound => sound.Label).ToList();
            }

            return AlertSoundCatalog.BuiltIn
                .Where(sound => sound.Matches(SoundSearch))
                .Select(sound => sound.Label)
                .ToList();
        }
    }

    public IReadOnlyList<AlertSoundDefinition> FilteredSoundItems
    {
        get
        {
            if (string.IsNullOrWhiteSpace(SoundSearch))
            {
                return AlertSoundCatalog.BuiltIn;
            }

            return AlertSoundCatalog.BuiltIn
                .Where(sound => sound.Matches(SoundSearch))
                .ToList();
        }
    }

    public IBrush ColorPreviewBrush => BrushForColor(ColorChoice);

    public IBrush AnimationPreviewBrush => IsAnimationEnabled && IsNoColor
        ? AppThemeCatalog.StatusBrush("HEALTHY")
        : ColorPreviewBrush;

    public bool IsAnimationEnabled => !AnimationChoice.Equals("none", StringComparison.OrdinalIgnoreCase);

    public string MatcherSummary
    {
        get
        {
            var groups = ActiveGroups()
                .Select(group => string.Join(" and ", group.ActiveCriteria().Select(criterion => $"{criterion.Field}={criterion.Expression}")))
                .ToList();
            return groups.Count == 0 ? "any cached resource" : string.Join(" or ", groups.Select(group => $"({group})"));
        }
    }

    public string ActionSummary
    {
        get
        {
            var actions = new[]
            {
                ColorChoice.Equals("none", StringComparison.OrdinalIgnoreCase) ? "" : $"color:{ColorChoice}",
                AnimationChoice.Equals("none", StringComparison.OrdinalIgnoreCase) ? "" : $"animation:{AnimationChoice}",
                HasZoom ? $"zoom:{ZoomChoice}" : "",
                HasSound ? $"sound:{SoundId}" : ""
            }.Where(action => action.Length > 0);
            var summary = string.Join(", ", actions);
            return summary.Length == 0 ? "none" : summary;
        }
    }

    public string Kind { get => FirstExpression("Kind"); set => SetFirstExpression("Kind", value); }
    public string Namespace { get => FirstExpression("Namespace"); set => SetFirstExpression("Namespace", value); }
    public string NameFilter { get => FirstExpression("Name"); set => SetFirstExpression("Name", value); }
    public string Status { get => FirstExpression("Status"); set => SetFirstExpression("Status", value); }
    public string Issue { get => FirstExpression("Issue"); set => SetFirstExpression("Issue", value); }
    public string Restarts { get => FirstExpression("Restarts"); set => SetFirstExpression("Restarts", value); }
    public string Age { get => FirstExpression("Age"); set => SetFirstExpression("Age", value); }
    public string Cpu { get => FirstExpression("CPU"); set => SetFirstExpression("CPU", value); }
    public string Memory { get => FirstExpression("Memory"); set => SetFirstExpression("Memory", value); }
    public string Storage { get => FirstExpression("Storage"); set => SetFirstExpression("Storage", value); }
    public string Node { get => FirstExpression("Node"); set => SetFirstExpression("Node", value); }
    public string Image { get => FirstExpression("Image"); set => SetFirstExpression("Image", value); }
    public string Owner { get => FirstExpression("Owner"); set => SetFirstExpression("Owner", value); }
    public string Ready { get => FirstExpression("Ready"); set => SetFirstExpression("Ready", value); }
    public string Freshness { get => FirstExpression("Freshness"); set => SetFirstExpression("Freshness", value); }
    public string Search { get => FirstExpression("Search"); set => SetFirstExpression("Search", value); }
    public bool ProblemsOnly { get => FirstExpression("Problems").Equals("true", StringComparison.OrdinalIgnoreCase); set => SetFirstExpression("Problems", value ? "true" : string.Empty); }
    public bool ActivityOnly { get => FirstExpression("Activity").Equals("true", StringComparison.OrdinalIgnoreCase); set => SetFirstExpression("Activity", value ? "true" : string.Empty); }
    public string Severity { get => string.Empty; set { } }
    public bool RadarFocus { get => HasZoom; set => ZoomChoice = value ? "100%" : "none"; }
    public bool RadarZoom { get => HasZoom; set => ZoomChoice = value ? "100%" : "none"; }
    public bool RadarBlink { get => AnimationChoice != "none"; set => AnimationChoice = value ? "blink" : "none"; }
    public bool RadarColor { get => ColorChoice != "none"; set => ColorChoice = value ? "status" : "none"; }
    public bool HealthSegment { get => false; set { } }
    public bool PlaySound { get => HasSound; set => SoundId = value ? "warning-ping" : "none"; }
    public string UntilMode { get => AnimationUntilMode; set => AnimationUntilMode = value; }
    public string UntilDuration { get => AnimationUntilDuration; set => AnimationUntilDuration = value; }

    public event PropertyChangedEventHandler? PropertyChanged;

    public AlertRule ToRule()
    {
        var groups = ActiveGroups()
            .Select(group => new AlertMatcherGroup(
                group.Id,
                group.ActiveCriteria()
                    .Select(criterion => new AlertMatcherCriterion(criterion.Id, criterion.Field, criterion.Expression.Trim()))
                    .ToList()))
            .ToList();
        var legacy = LegacyMatchers(groups);
        var zoomPercent = ParseZoom(ZoomChoice);
        var actionHold = ActionUntil();
        var viewTriggered = HasNewInViewMatcher();
        var colorUntilModeForRule = viewTriggered && ColorUntilMode.Equals(AlertUntilModes.Duration, StringComparison.OrdinalIgnoreCase)
            ? AlertUntilModes.NewInView
            : ColorUntilMode;
        var animationUntilModeForRule = viewTriggered && AnimationUntilMode.Equals(AlertUntilModes.Duration, StringComparison.OrdinalIgnoreCase)
            ? AlertUntilModes.NewInView
            : AnimationUntilMode;
        return new AlertRule(
            Id,
            string.IsNullOrWhiteSpace(Name) ? "Unnamed alert" : Name.Trim(),
            Description.Trim(),
            Enabled,
            BuiltIn,
            string.Empty,
            legacy,
            new AlertRuleActions(
                RadarFocus: zoomPercent > 0,
                RadarZoom: zoomPercent > 0,
                RadarBlink: !AnimationChoice.Equals("none", StringComparison.OrdinalIgnoreCase),
                RadarColor: !ColorChoice.Equals("none", StringComparison.OrdinalIgnoreCase),
                RadarColorMode: "custom",
                HealthSegment: false,
                PlaySound: HasSound,
                RadarColorValue: ColorChoice,
                RadarColorUntilMode: colorUntilModeForRule,
                RadarColorUntilDuration: ColorUntilDuration.Trim(),
                RadarAnimation: AnimationChoice,
                RadarAnimationUntilMode: animationUntilModeForRule,
                RadarAnimationUntilDuration: AnimationUntilDuration.Trim(),
                RadarZoomPercent: zoomPercent,
                SoundMinimumMatches: soundMinimumMatches),
            actionHold,
            SoundId,
            groups);
    }

    public void RemoveGroup(AlertMatcherGroupViewModel group)
    {
        if (!CanEdit || MatcherGroups.Count <= 1)
        {
            return;
        }

        MatcherGroups.Remove(group);
        EnsureMinimumShape();
        NotifyRuleShapeChanged();
    }

    public void RemoveCriterion(AlertMatcherCriterionViewModel criterion)
    {
        if (!CanEdit)
        {
            return;
        }

        foreach (var group in MatcherGroups)
        {
            if (group.Criteria.Remove(criterion))
            {
                EnsureMinimumShape();
                NotifyRuleShapeChanged();
                return;
            }
        }
    }

    public void AddMatcherGroup()
    {
        if (!CanEdit)
        {
            return;
        }

        MatcherGroups.Add(new AlertMatcherGroupViewModel(this, AlertMatcherGroupViewModel.EmptyWithCriterion()));
        NotifyRuleShapeChanged();
    }

    public void AddCriterion(AlertMatcherGroupViewModel group)
    {
        if (!CanEdit || !MatcherGroups.Contains(group))
        {
            return;
        }

        group.Criteria.Add(new AlertMatcherCriterionViewModel(this, AlertMatcherCriterionViewModel.Empty()));
        NotifyRuleShapeChanged();
    }

    internal void OnCriterionChanged()
    {
        NotifyRuleShapeChanged();
    }

    public void UseStatusColor()
    {
        ColorChoice = "status";
    }

    public void UseNoColor()
    {
        ColorChoice = "none";
    }

    public void SetActiveSummary(string summary)
    {
        ActiveSummary = summary;
    }

    private void LoadGroups(AlertRule rule)
    {
        var sourceGroups = rule.MatcherGroups is { Count: > 0 }
            ? rule.MatcherGroups
            : LegacyGroups(rule.Matchers);
        foreach (var group in sourceGroups)
        {
            MatcherGroups.Add(new AlertMatcherGroupViewModel(this, group));
        }
    }

    private static IReadOnlyList<AlertMatcherGroup> LegacyGroups(AlertRuleMatchers matchers)
    {
        var criteria = new List<AlertMatcherCriterion>();
        Add("Search", matchers.Search);
        Add("ID", matchers.Id);
        Add("Issue", matchers.Issue);
        Add("Kind", matchers.Kind);
        Add("Name", matchers.Name);
        Add("Namespace", matchers.Namespace);
        Add("Cluster", matchers.Cluster);
        Add("Status", matchers.Status);
        Add("Age", matchers.Age);
        Add("Node", matchers.Node);
        Add("Image", matchers.Image);
        Add("Ready", matchers.Ready);
        Add("Restarts", matchers.Restarts);
        Add("Owner", matchers.Owner);
        Add("CPU", matchers.Cpu);
        Add("Memory", matchers.Memory);
        Add("Storage", matchers.Storage);
        Add("Freshness", matchers.Freshness);
        if (matchers.ProblemsOnly) Add("Problems", "true");
        if (matchers.ActivityOnly) Add("Active", "true");
        return [new AlertMatcherGroup($"group-{Guid.NewGuid():N}", criteria)];

        void Add(string field, string expression)
        {
            if (!string.IsNullOrWhiteSpace(expression))
            {
                criteria.Add(new AlertMatcherCriterion($"criterion-{Guid.NewGuid():N}", field, expression));
            }
        }
    }

    private void EnsureMinimumShape()
    {
        if (MatcherGroups.Count == 0)
        {
            MatcherGroups.Add(new AlertMatcherGroupViewModel(this, AlertMatcherGroupViewModel.EmptyWithCriterion()));
        }

        foreach (var group in MatcherGroups)
        {
            if (group.Criteria.Count == 0)
            {
                group.Criteria.Add(new AlertMatcherCriterionViewModel(this, AlertMatcherCriterionViewModel.Empty()));
            }
        }
    }

    private IReadOnlyList<AlertMatcherGroupViewModel> ActiveGroups()
    {
        return MatcherGroups.Where(group => group.HasActiveCriteria).ToList();
    }

    private string FirstExpression(string field)
    {
        return MatcherGroups
            .SelectMany(group => group.Criteria)
            .FirstOrDefault(criterion => criterion.Field.Equals(field, StringComparison.OrdinalIgnoreCase))
            ?.Expression ?? string.Empty;
    }

    private void SetFirstExpression(string field, string value)
    {
        if (!CanEdit)
        {
            return;
        }

        var criterion = MatcherGroups
            .SelectMany(group => group.Criteria)
            .FirstOrDefault(item => item.Field.Equals(field, StringComparison.OrdinalIgnoreCase));
        if (criterion is null)
        {
            criterion = MatcherGroups.First().Criteria.FirstOrDefault(item => !item.IsActive);
            if (criterion is null)
            {
                criterion = new AlertMatcherCriterionViewModel(this, AlertMatcherCriterionViewModel.Empty());
                MatcherGroups.First().Criteria.Add(criterion);
            }

            criterion.Field = field;
        }

        criterion.Expression = value;
        NotifyRuleShapeChanged();
    }

    private static AlertRuleMatchers LegacyMatchers(IReadOnlyList<AlertMatcherGroup> groups)
    {
        var first = groups.FirstOrDefault()?.Criteria ?? [];
        return new AlertRuleMatchers(
            Search: Expression(first, "Search"),
            Id: Expression(first, "ID"),
            Issue: Expression(first, "Issue"),
            Kind: Expression(first, "Kind"),
            Name: Expression(first, "Name"),
            Namespace: Expression(first, "Namespace"),
            Cluster: Expression(first, "Cluster"),
            Status: Expression(first, "Status"),
            Age: Expression(first, "Age"),
            Node: Expression(first, "Node"),
            Image: Expression(first, "Image"),
            Ready: Expression(first, "Ready"),
            Restarts: Expression(first, "Restarts"),
            Owner: Expression(first, "Owner"),
            Cpu: Expression(first, "CPU"),
            Memory: Expression(first, "Memory"),
            Storage: Expression(first, "Storage"),
            Freshness: Expression(first, "Freshness"),
            ProblemsOnly: Expression(first, "Problems").Equals("true", StringComparison.OrdinalIgnoreCase),
            ActivityOnly: (Expression(first, "Active").Length > 0 ? Expression(first, "Active") : Expression(first, "Activity")).Equals("true", StringComparison.OrdinalIgnoreCase));
    }

    private static string Expression(IEnumerable<AlertMatcherCriterion> criteria, string field)
    {
        return criteria.FirstOrDefault(criterion => criterion.Field.Equals(field, StringComparison.OrdinalIgnoreCase))?.Expression ?? string.Empty;
    }

    private AlertRuleUntil ActionUntil()
    {
        return new AlertRuleUntil("none");
    }

    private bool HasNewInViewMatcher()
    {
        return ActiveGroups()
            .SelectMany(group => group.ActiveCriteria())
            .Any(criterion => NormalizeField(criterion.Field).Equals("newinview", StringComparison.Ordinal)
                              && BooleanExpression(criterion.Expression));
    }

    private static int ParseZoom(string value)
    {
        var normalized = value.Trim().TrimEnd('%');
        return int.TryParse(normalized, out var percent) ? Math.Clamp(percent, 0, 400) : 0;
    }

    private static string NormalizeChoice(string? value, string fallback)
    {
        return string.IsNullOrWhiteSpace(value) ? fallback : value.Trim();
    }

    private static string NormalizeUntil(string? value, string fallback)
    {
        var normalized = NormalizeChoice(value, fallback);
        return normalized is "none" or AlertUntilModes.Once or AlertUntilModes.NoMatch or AlertUntilModes.Duration or AlertUntilModes.NewInView ? normalized : fallback;
    }

    private static string DurationChoiceFrom(string mode, string duration)
    {
        if (mode.Equals(AlertUntilModes.Once, StringComparison.OrdinalIgnoreCase))
        {
            return DurationOnceChoice;
        }

        if (mode.Equals(AlertUntilModes.NewInView, StringComparison.OrdinalIgnoreCase)
            && DurationChoicesValue.Contains(duration, StringComparer.OrdinalIgnoreCase))
        {
            return duration;
        }

        if (mode.Equals(AlertUntilModes.NewInView, StringComparison.OrdinalIgnoreCase))
        {
            return DurationOnceChoice;
        }

        if (mode.Equals(AlertUntilModes.Duration, StringComparison.OrdinalIgnoreCase)
            && DurationChoicesValue.Contains(duration, StringComparer.OrdinalIgnoreCase))
        {
            return duration;
        }

        return DurationUntilChangeChoice;
    }

    private void SetDurationChoice(string value, ref string modeField, ref string durationField, string propertyName)
    {
        if (!CanEdit)
        {
            return;
        }

        var normalized = NormalizeChoice(value, DurationUntilChangeChoice);
        var viewTriggered = HasNewInViewMatcher();
        var nextMode = AlertUntilModes.NoMatch;
        var nextDuration = string.Empty;
        if (normalized.Equals(DurationOnceChoice, StringComparison.OrdinalIgnoreCase))
        {
            nextMode = viewTriggered ? AlertUntilModes.NewInView : AlertUntilModes.Once;
        }
        else if (DurationChoicesValue.Contains(normalized, StringComparer.OrdinalIgnoreCase)
                 && !normalized.Equals(DurationUntilChangeChoice, StringComparison.OrdinalIgnoreCase))
        {
            nextMode = viewTriggered ? AlertUntilModes.NewInView : AlertUntilModes.Duration;
            nextDuration = normalized;
        }

        var modeProperty = propertyName.Equals(nameof(ColorDurationChoice), StringComparison.Ordinal)
            ? nameof(ColorUntilMode)
            : nameof(AnimationUntilMode);
        var durationProperty = propertyName.Equals(nameof(ColorDurationChoice), StringComparison.Ordinal)
            ? nameof(ColorUntilDuration)
            : nameof(AnimationUntilDuration);
        var changed = false;
        if (!modeField.Equals(nextMode, StringComparison.Ordinal))
        {
            modeField = nextMode;
            OnPropertyChanged(modeProperty);
            changed = true;
        }
        if (!durationField.Equals(nextDuration, StringComparison.Ordinal))
        {
            durationField = nextDuration;
            OnPropertyChanged(durationProperty);
            changed = true;
        }

        if (changed)
        {
            OnPropertyChanged(propertyName);
            NotifyRuleShapeChanged();
        }
    }

    private static IBrush BrushForColor(string color)
    {
        if (TryParseHexColor(color, out var parsed))
        {
            return new SolidColorBrush(parsed);
        }

        return color.ToLowerInvariant() switch
        {
            "status" => AppThemeCatalog.StatusBrush("WARNING"),
            "fresh" or "cyan" => AppThemeCatalog.StatusBrush("HEALTHY"),
            "green" => AppThemeCatalog.StatusBrush("HEALTHY"),
            "amber" => AppThemeCatalog.StatusBrush("WARNING"),
            "red" => AppThemeCatalog.StatusBrush("CRITICAL"),
            "blue" => SolidColorBrush.Parse("#58A6FF"),
            "violet" => SolidColorBrush.Parse("#B58CFF"),
            _ => Brushes.Transparent
        };
    }

    private static Color ColorFromChoice(string color)
    {
        if (TryParseHexColor(color, out var parsed))
        {
            return parsed;
        }

        return color.ToLowerInvariant() switch
        {
            "fresh" or "cyan" or "green" => Color.Parse("#55D98B"),
            "amber" or "status" => Color.Parse("#E2B84D"),
            "red" => Color.Parse("#F25F5C"),
            "blue" => Color.Parse("#58A6FF"),
            "violet" => Color.Parse("#B58CFF"),
            _ => Color.Parse("#7DFFC3")
        };
    }

    private static bool TryParseHexColor(string value, out Color color)
    {
        color = default;
        if (!value.StartsWith('#') || value.Length is not (7 or 9))
        {
            return false;
        }

        try
        {
            color = Color.Parse(value);
            return true;
        }
        catch (FormatException)
        {
            return false;
        }
    }

    private static string NormalizeField(string field)
    {
        return field.Trim().ToLowerInvariant().Replace(" ", string.Empty, StringComparison.Ordinal);
    }

    private static bool BooleanExpression(string expression)
    {
        return !expression.Trim().Equals("false", StringComparison.OrdinalIgnoreCase)
               && !expression.Trim().Equals("no", StringComparison.OrdinalIgnoreCase)
               && !expression.Trim().Equals("0", StringComparison.OrdinalIgnoreCase);
    }

    private bool SetEditable(ref string field, string value, [CallerMemberName] string? propertyName = null)
    {
        if (!CanEdit)
        {
            return false;
        }

        return SetField(ref field, value, propertyName);
    }

    private bool SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        OnPropertyChanged(propertyName);
        NotifyRuleShapeChanged();
        return true;
    }

    private void NotifySoundChanged()
    {
        OnPropertyChanged(nameof(SoundLabel));
        OnPropertyChanged(nameof(SoundChoice));
        OnPropertyChanged(nameof(SoundAttribution));
        OnPropertyChanged(nameof(SoundSourceUrl));
        OnPropertyChanged(nameof(HasSound));
        OnPropertyChanged(nameof(FilteredSoundItems));
    }

    private void NotifyRuleShapeChanged()
    {
        OnPropertyChanged(nameof(MatcherSummary));
        OnPropertyChanged(nameof(ActionSummary));
        OnPropertyChanged(nameof(ColorPreviewBrush));
        OnPropertyChanged(nameof(AnimationPreviewBrush));
        OnPropertyChanged(nameof(IsAnimationEnabled));
        OnPropertyChanged(nameof(ColorDurationChoice));
        OnPropertyChanged(nameof(AnimationDurationChoice));
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}

public sealed class AlertMatcherGroupViewModel
{
    private readonly AlertRuleRowViewModel owner;

    public AlertMatcherGroupViewModel(AlertRuleRowViewModel owner, AlertMatcherGroup group)
    {
        this.owner = owner;
        Id = group.Id;
        foreach (var criterion in group.Criteria)
        {
            Criteria.Add(new AlertMatcherCriterionViewModel(owner, criterion));
        }
    }

    public string Id { get; }

    public bool CanEdit => owner.CanEdit;

    public ObservableCollection<AlertMatcherCriterionViewModel> Criteria { get; } = [];

    public bool HasActiveCriteria => ActiveCriteria().Count > 0;

    public IReadOnlyList<AlertMatcherCriterionViewModel> ActiveCriteria()
    {
        return Criteria
            .Where(criterion => !string.IsNullOrWhiteSpace(criterion.Field))
            .Where(criterion => !string.IsNullOrWhiteSpace(criterion.Expression))
            .ToList();
    }

    public static AlertMatcherGroup Empty() => new($"group-{Guid.NewGuid():N}", []);

    public static AlertMatcherGroup EmptyWithCriterion() => new($"group-{Guid.NewGuid():N}", [AlertMatcherCriterionViewModel.Empty()]);
}

public sealed class AlertMatcherCriterionViewModel : INotifyPropertyChanged
{
    private readonly AlertRuleRowViewModel owner;
    private string fieldName;
    private string expression;

    public AlertMatcherCriterionViewModel(AlertRuleRowViewModel owner, AlertMatcherCriterion criterion)
    {
        this.owner = owner;
        Id = criterion.Id;
        fieldName = string.IsNullOrWhiteSpace(criterion.Field) ? "Kind" : criterion.Field;
        expression = criterion.Expression;
    }

    public string Id { get; }

    public bool CanEdit => owner.CanEdit;

    public string Field
    {
        get => fieldName;
        set
        {
            if (SetField(ref fieldName, value))
            {
                OnPropertyChanged(nameof(FieldType));
                OnPropertyChanged(nameof(ExampleOptions));
            }
        }
    }

    public string Expression { get => expression; set => SetField(ref expression, value); }

    public bool IsActive => !string.IsNullOrWhiteSpace(Field) && !string.IsNullOrWhiteSpace(Expression);

    public string FieldType => FieldKind(Field);

    public IReadOnlyList<string> FieldChoices => owner.FieldChoices;

    public IReadOnlyList<string> ExampleOptions => ExamplesFor(Field);

    public event PropertyChangedEventHandler? PropertyChanged;

    public static AlertMatcherCriterion Empty() => new($"criterion-{Guid.NewGuid():N}", "Kind", string.Empty);

    private bool SetField(ref string target, string value, [CallerMemberName] string? propertyName = null)
    {
        if (target.Equals(value, StringComparison.Ordinal))
        {
            return false;
        }

        target = value;
        OnPropertyChanged(propertyName);
        OnPropertyChanged(nameof(IsActive));
        owner.OnCriterionChanged();
        return true;
    }

    private static string FieldKind(string field)
    {
        return field.ToLowerInvariant() switch
        {
            "restarts" => "number",
            "age" => "time",
            "cpu" or "memory" or "storage" => "metric",
            "problems" or "error" or "active" or "activity" or "recently changed" or "new in view" => "boolean",
            _ => "text"
        };
    }

    private static IReadOnlyList<string> ExamplesFor(string field)
    {
        return FieldKind(field) switch
        {
            "number" => [">1", "=5", "<3", ">=10", "outlier", "p95"],
            "time" => ["<10m", ">1h", "<=30s", ">2d", "p95"],
            "metric" when field.Equals("CPU", StringComparison.OrdinalIgnoreCase) => [">500m", ">1c", "p95", "outlier"],
            "metric" => [">512Mi", ">1Gi", "p95", "outlier"],
            "boolean" => ["true", "false"],
            _ => ["pod", "\"exact\"", "~prefix", "suffix~", "/regex/"]
        };
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}

public sealed record ActiveAlertRow(
    string Rule,
    string Matches,
    string Actions,
    string Sound);
