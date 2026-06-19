namespace Podlord.Core;

public sealed record AlertRule(
    string Id,
    string Name,
    string Description,
    bool Enabled,
    bool BuiltIn,
    string Severity,
    AlertRuleMatchers Matchers,
    AlertRuleActions Actions,
    AlertRuleUntil Until,
    string SoundId = "",
    IReadOnlyList<AlertMatcherGroup>? MatcherGroups = null)
{
    public bool CanEdit => !BuiltIn;

    public bool CanDelete => !BuiltIn;
}

public sealed record AlertMatcherGroup(
    string Id,
    IReadOnlyList<AlertMatcherCriterion> Criteria);

public sealed record AlertMatcherCriterion(
    string Id,
    string Field,
    string Expression);

public sealed record AlertRuleMatchers(
    string Search = "",
    string Id = "",
    string Issue = "",
    string Kind = "",
    string Name = "",
    string Namespace = "",
    string Cluster = "",
    string Status = "",
    string Age = "",
    string Node = "",
    string Image = "",
    string Ready = "",
    string Restarts = "",
    string Owner = "",
    string Cpu = "",
    string Memory = "",
    string Storage = "",
    string Freshness = "",
    bool ProblemsOnly = false,
    bool ActivityOnly = false);

public sealed record AlertRuleActions(
    bool RadarFocus = true,
    bool RadarZoom = true,
    bool RadarBlink = true,
    bool RadarColor = true,
    string RadarColorMode = "severity",
    bool HealthSegment = false,
    bool PlaySound = false,
    string RadarColorValue = "none",
    string RadarColorUntilMode = AlertUntilModes.NoMatch,
    string RadarColorUntilDuration = "",
    string RadarAnimation = "none",
    string RadarAnimationUntilMode = AlertUntilModes.NoMatch,
    string RadarAnimationUntilDuration = "",
    int RadarZoomPercent = 0,
    int SoundMinimumMatches = 1);

public sealed record AlertRuleUntil(
    string Mode = AlertUntilModes.NoMatch,
    string Duration = "");

public static class AlertUntilModes
{
    public const string Once = "once";
    public const string NoMatch = "no-match";
    public const string Duration = "duration";
    public const string NewInView = "new-in-view";
}

public sealed record AlertEvaluation(
    AlertRule Rule,
    IReadOnlyList<FlatResourceRow> Matches,
    bool Triggered,
    string Summary);

public static class AlertRuleCatalog
{
    public static IReadOnlyList<AlertRule> DefaultRules { get; } =
    [
        new AlertRule(
            "default-problem-color",
            "Problem color",
            "Paint resources yellow or red while they have an active problem.",
            true,
            true,
            "",
            new AlertRuleMatchers(ProblemsOnly: true),
            new AlertRuleActions(
                RadarFocus: true,
                RadarZoom: true,
                RadarBlink: false,
                RadarColor: true,
                RadarColorValue: "status",
                RadarAnimation: "none",
                PlaySound: true,
                RadarZoomPercent: 100),
            new AlertRuleUntil(AlertUntilModes.NoMatch),
            "warning-ping",
            Groups(Group(
                "default-problem-color-match",
                Criterion("default-problem-color-problems", "Problems", "true")))),
        new AlertRule(
            "default-recent-change-color",
            "Recent change color",
            "Paint recently changed resources cyan for the old short freshness window.",
            true,
            true,
            "",
            new AlertRuleMatchers(),
            new AlertRuleActions(
                RadarFocus: false,
                RadarZoom: false,
                RadarBlink: false,
                RadarColor: true,
                RadarColorValue: "fresh",
                RadarAnimation: "none"),
            new AlertRuleUntil(AlertUntilModes.NoMatch),
            "none",
            Groups(Group(
                "default-recent-change-color-match",
                Criterion("default-recent-change-color-recent", "Recently changed", "true")))),
        new AlertRule(
            "default-active-view-pulse",
            "Active view pulse",
            "Pulse active resources for the old visibility announcement window when they enter the view.",
            true,
            true,
            "",
            new AlertRuleMatchers(),
            new AlertRuleActions(
                RadarFocus: false,
                RadarZoom: false,
                RadarBlink: true,
                RadarColor: false,
                RadarColorValue: "none",
                RadarAnimation: "pulse",
                RadarAnimationUntilMode: AlertUntilModes.NewInView,
                RadarAnimationUntilDuration: "5s"),
            new AlertRuleUntil(AlertUntilModes.NoMatch),
            "none",
            Groups(Group(
                "default-active-view-pulse-match",
                Criterion("default-active-view-pulse-new-in-view", "New in view", "true"),
                Criterion("default-active-view-pulse-active", "Active", "true"))))
    ];

    private static IReadOnlyList<AlertMatcherGroup> Groups(params AlertMatcherGroup[] groups) => groups;

    private static AlertMatcherGroup Group(string id, params AlertMatcherCriterion[] criteria) =>
        new(id, criteria);

    private static AlertMatcherCriterion Criterion(string id, string field, string expression) =>
        new(id, field, expression);
}

public static class AlertRuleEvaluator
{
    public static IReadOnlyList<AlertEvaluation> Evaluate(
        IEnumerable<FlatResourceRow> rows,
        IEnumerable<AlertRule> rules)
    {
        ArgumentNullException.ThrowIfNull(rows);
        ArgumentNullException.ThrowIfNull(rules);

        var materializedRows = rows as IReadOnlyList<FlatResourceRow> ?? rows.ToList();
        return rules
            .Where(rule => rule.Enabled)
            .Select(rule => EvaluateRule(materializedRows, rule))
            .ToList();
    }

    public static AlertEvaluation EvaluateRule(IReadOnlyList<FlatResourceRow> rows, AlertRule rule)
    {
        ArgumentNullException.ThrowIfNull(rows);
        ArgumentNullException.ThrowIfNull(rule);

        if (!rule.Enabled)
        {
            return new AlertEvaluation(rule, Array.Empty<FlatResourceRow>(), false, "disabled");
        }

        var matches = HasMatcherGroups(rule)
            ? rows.Where(row => MatchesAnyGroup(row, rows, rule.MatcherGroups!)).ToList()
            : LegacyMatches(rows, rule.Matchers);
        var summary = matches.Count == 0
            ? "no matches"
            : $"{matches.Count} match(es): {string.Join(", ", matches.Take(3).Select(row => $"{row.Kind}/{row.Name}"))}";
        return new AlertEvaluation(rule, matches, matches.Count > 0, summary);
    }

    private static bool HasMatcherGroups(AlertRule rule)
    {
        return rule.MatcherGroups is { Count: > 0 }
               && rule.MatcherGroups.Any(group => ActiveCriteria(group.Criteria).Count > 0);
    }

    private static IReadOnlyList<FlatResourceRow> LegacyMatches(IReadOnlyList<FlatResourceRow> rows, AlertRuleMatchers matchers)
    {
        var query = QueryFor(matchers);
        return ResourceFilterMatcher.FilterRows(rows, query)
            .Where(row => MatchesFreshness(row, matchers.Freshness))
            .Where(row => MatchesSpecialRestarts(row, rows, matchers.Restarts))
            .Where(row => MatchesSpecialAge(row, rows, matchers.Age))
            .Where(row => MatchesSpecialMetric(row, rows, matchers.Cpu, row => row.Pulse.CpuMillicores))
            .Where(row => MatchesSpecialMetric(row, rows, matchers.Memory, row => row.Pulse.MemoryBytes))
            .Where(row => MatchesSpecialMetric(row, rows, matchers.Storage, row => row.Pulse.StorageUsedBytes))
            .ToList();
    }

    private static bool MatchesAnyGroup(
        FlatResourceRow row,
        IReadOnlyList<FlatResourceRow> rows,
        IReadOnlyList<AlertMatcherGroup> groups)
    {
        return groups
            .Select(group => ActiveCriteria(group.Criteria))
            .Where(criteria => criteria.Count > 0)
            .Any(criteria => criteria.All(criterion => MatchesCriterion(row, rows, criterion)));
    }

    private static IReadOnlyList<AlertMatcherCriterion> ActiveCriteria(IEnumerable<AlertMatcherCriterion> criteria)
    {
        return criteria
            .Where(criterion => !string.IsNullOrWhiteSpace(criterion.Field))
            .Where(criterion => !string.IsNullOrWhiteSpace(criterion.Expression))
            .ToList();
    }

    private static bool MatchesCriterion(
        FlatResourceRow row,
        IReadOnlyList<FlatResourceRow> rows,
        AlertMatcherCriterion criterion)
    {
        var field = NormalizeField(criterion.Field);
        var expression = criterion.Expression;
        return field switch
        {
            "search" => ResourceFilterMatcher.FilterRows([row], QueryFor(new AlertRuleMatchers(Search: expression))).Count > 0,
            "id" => ResourceFilterMatcher.MatchesText(row.Id, expression),
            "issue" => ResourceFilterMatcher.MatchesText(ResourceFilterMatcher.ProblemReason(row, ResourceFilterMatcher.RestartOutlierThreshold(rows)), expression),
            "kind" => ResourceFilterMatcher.MatchesText(row.Kind, expression),
            "name" => ResourceFilterMatcher.MatchesText(row.Name, expression),
            "namespace" => ResourceFilterMatcher.MatchesText(row.Namespace ?? "cluster", expression),
            "cluster" => ResourceFilterMatcher.MatchesText(row.Cluster, expression),
            "status" => ResourceFilterMatcher.MatchesText(row.Status, expression),
            "age" => MatchesSpecialAge(row, rows, expression) && ResourceFilterMatcher.MatchesAge(row.Age, IsSpecialStat(expression) ? string.Empty : expression),
            "node" => ResourceFilterMatcher.MatchesText(row.Node ?? string.Empty, expression),
            "image" => ResourceFilterMatcher.MatchesText(row.ImageSummary, expression),
            "ready" => ResourceFilterMatcher.MatchesText(row.Ready, expression),
            "restarts" => MatchesSpecialRestarts(row, rows, expression) && ResourceFilterMatcher.MatchesNumber(row.Restarts, IsSpecialStat(expression) ? string.Empty : expression),
            "owner" => ResourceFilterMatcher.MatchesText(row.Owner ?? string.Empty, expression),
            "cpu" => MatchesSpecialMetric(row, rows, expression, item => item.Pulse.CpuMillicores) && ResourceFilterMatcher.MatchesCpu(row.Pulse, IsSpecialStat(expression) ? string.Empty : expression),
            "memory" => MatchesSpecialMetric(row, rows, expression, item => item.Pulse.MemoryBytes) && ResourceFilterMatcher.MatchesBytes(row.Pulse.MemoryBytes, row.Pulse.MemoryLimitBytes, row.MemorySummaryDisplay, IsSpecialStat(expression) ? string.Empty : expression),
            "storage" => MatchesSpecialMetric(row, rows, expression, item => item.Pulse.StorageUsedBytes) && ResourceFilterMatcher.MatchesBytes(row.Pulse.StorageUsedBytes, row.Pulse.StorageLimitBytes, row.StorageDisplay, IsSpecialStat(expression) ? string.Empty : expression),
            "freshness" => ResourceFilterMatcher.MatchesText(row.Freshness.ToString(), expression),
            "problems" => BooleanExpression(expression) ? ResourceFilterMatcher.IsProblem(row, ResourceFilterMatcher.RestartOutlierThreshold(rows)) : !ResourceFilterMatcher.IsProblem(row, ResourceFilterMatcher.RestartOutlierThreshold(rows)),
            "error" or "errors" => BooleanExpression(expression) ? ResourceFilterMatcher.IsError(row, ResourceFilterMatcher.RestartOutlierThreshold(rows)) : !ResourceFilterMatcher.IsError(row, ResourceFilterMatcher.RestartOutlierThreshold(rows)),
            "recentlychanged" or "recent" => BooleanExpression(expression) ? IsRecentlyChanged(row) : !IsRecentlyChanged(row),
            "newinview" => BooleanExpression(expression),
            "active" => BooleanExpression(expression) ? ResourceFilterMatcher.IsActivity(row) : !ResourceFilterMatcher.IsActivity(row),
            "activity" => BooleanExpression(expression) ? ResourceFilterMatcher.IsActivity(row) : !ResourceFilterMatcher.IsActivity(row),
            _ => ResourceFilterMatcher.MatchesText(ValueForUnknownField(row, field), expression)
        };
    }

    private static bool IsRecentlyChanged(FlatResourceRow row)
    {
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

    private static string ValueForUnknownField(FlatResourceRow row, string field)
    {
        return field switch
        {
            "lastchange" => row.LastChange,
            "eventreason" => row.EventReason,
            "eventmessage" => row.EventMessage,
            _ => string.Empty
        };
    }

    private static ResourceQuery QueryFor(AlertRuleMatchers matchers)
    {
        return new ResourceQuery(
            Search: matchers.Search,
            Id: matchers.Id,
            Issue: matchers.Issue,
            Kind: matchers.Kind,
            Name: matchers.Name,
            Namespace: matchers.Namespace,
            Cluster: matchers.Cluster,
            Status: matchers.Status,
            Age: IsSpecialStat(matchers.Age) ? string.Empty : matchers.Age,
            Node: matchers.Node,
            Image: matchers.Image,
            Ready: matchers.Ready,
            Restarts: IsSpecialStat(matchers.Restarts) ? string.Empty : matchers.Restarts,
            Owner: matchers.Owner,
            ProblemsOnly: matchers.ProblemsOnly,
            ActivityOnly: matchers.ActivityOnly,
            Limit: 5_000,
            Cpu: IsSpecialStat(matchers.Cpu) ? string.Empty : matchers.Cpu,
            Memory: IsSpecialStat(matchers.Memory) ? string.Empty : matchers.Memory,
            Storage: IsSpecialStat(matchers.Storage) ? string.Empty : matchers.Storage);
    }

    private static bool MatchesFreshness(FlatResourceRow row, string expression)
    {
        return ResourceFilterMatcher.MatchesText(row.Freshness.ToString(), expression);
    }

    private static bool MatchesSpecialRestarts(FlatResourceRow row, IReadOnlyList<FlatResourceRow> rows, string expression)
    {
        if (!IsSpecialStat(expression))
        {
            return true;
        }

        return NormalizeStat(expression) switch
        {
            "outlier" => row.Restarts > ResourceFilterMatcher.RestartOutlierThreshold(rows),
            "p95" => row.Restarts >= Percentile(rows.Select(item => (double)item.Restarts), 0.95),
            _ => true
        };
    }

    private static bool MatchesSpecialAge(FlatResourceRow row, IReadOnlyList<FlatResourceRow> rows, string expression)
    {
        if (!IsSpecialStat(expression))
        {
            return true;
        }

        var rowAge = ResourceFilterMatcher.ParseHumanDuration(row.Age);
        if (rowAge is null)
        {
            return false;
        }

        var threshold = NormalizeStat(expression) switch
        {
            "outlier" or "p95" => Percentile(rows
                .Select(item => ResourceFilterMatcher.ParseHumanDuration(item.Age))
                .Where(duration => duration is not null)
                .Select(duration => duration!.Value.TotalSeconds), 0.95),
            _ => 0
        };
        return rowAge.Value.TotalSeconds >= threshold;
    }

    private static bool MatchesSpecialMetric(
        FlatResourceRow row,
        IReadOnlyList<FlatResourceRow> rows,
        string expression,
        Func<FlatResourceRow, double?> value)
    {
        if (!IsSpecialStat(expression))
        {
            return true;
        }

        var rowValue = value(row);
        if (rowValue is null)
        {
            return false;
        }

        var samples = rows.Select(value).Where(sample => sample is not null).Select(sample => sample!.Value);
        var threshold = NormalizeStat(expression) switch
        {
            "outlier" or "p95" => Percentile(samples, 0.95),
            _ => double.MinValue
        };
        return rowValue.Value >= threshold;
    }

    private static bool IsSpecialStat(string expression)
    {
        return NormalizeStat(expression) is "outlier" or "p95";
    }

    private static string NormalizeStat(string expression)
    {
        return expression.Trim().Trim('"').ToLowerInvariant();
    }

    private static double Percentile(IEnumerable<double> values, double percentile)
    {
        var sorted = values.Where(value => !double.IsNaN(value)).OrderBy(value => value).ToList();
        if (sorted.Count == 0)
        {
            return double.PositiveInfinity;
        }

        if (sorted.Count == 1)
        {
            return sorted[0];
        }

        var position = Math.Clamp(percentile, 0, 1) * (sorted.Count - 1);
        var lower = (int)Math.Floor(position);
        var upper = (int)Math.Ceiling(position);
        if (lower == upper)
        {
            return sorted[lower];
        }

        var weight = position - lower;
        return sorted[lower] + (sorted[upper] - sorted[lower]) * weight;
    }
}
