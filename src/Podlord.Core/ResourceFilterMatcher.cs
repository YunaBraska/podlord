using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;

namespace Podlord.Core;

public static class ResourceFilterMatcher
{
    private static readonly TimeSpan EventSuccessActivityTtl = TimeSpan.FromMinutes(5);
    private static readonly TimeSpan EventNormalActivityTtl = TimeSpan.FromMinutes(5);
    private static readonly TimeSpan EventWarningActivityTtl = TimeSpan.FromMinutes(30);
    private static readonly TimeSpan PodStartupProblemGrace = TimeSpan.FromMinutes(5);

    public static IReadOnlyList<FlatResourceRow> FilterRows(IEnumerable<FlatResourceRow> rows, ResourceQuery query)
    {
        var materialized = rows as IReadOnlyCollection<FlatResourceRow> ?? rows.ToList();
        var threshold = RestartOutlierThreshold(materialized);
        return materialized
            .Where(row => MatchesText(row.Id, query.Id))
            .Where(row => MatchesText(ProblemReason(row, threshold), query.Issue))
            .Where(row => MatchesText(row.Kind, query.Kind))
            .Where(row => MatchesText(row.Name, query.Name))
            .Where(row => MatchesText(row.Namespace ?? "cluster", query.Namespace))
            .Where(row => MatchesText(row.Cluster, query.Cluster))
            .Where(row => MatchesText(row.Status, query.Status))
            .Where(row => MatchesAge(row.Age, query.Age))
            .Where(row => MatchesText(row.Node ?? string.Empty, query.Node))
            .Where(row => MatchesText(row.ImageSummary, query.Image))
            .Where(row => MatchesText(row.Ready, query.Ready))
            .Where(row => MatchesText(row.Owner ?? string.Empty, query.Owner))
            .Where(row => MatchesNumber(row.Restarts, query.Restarts))
            .Where(row => MatchesCpu(row.Pulse, query.Cpu))
            .Where(row => MatchesBytes(row.Pulse.MemoryBytes, row.Pulse.MemoryLimitBytes, row.MemorySummaryDisplay, query.Memory))
            .Where(row => MatchesBytes(row.Pulse.StorageUsedBytes, row.Pulse.StorageLimitBytes, row.StorageDisplay, query.Storage))
            .Where(row => MatchesSearch(row, query.Search))
            .Where(row => !query.ProblemsOnly || IsProblem(row, threshold))
            .Where(row => !query.ActivityOnly || IsActivity(row))
            .Take(NormalizeLimit(query.Limit))
            .ToList();
    }

    public static bool MatchesText(string? value, string? expression)
    {
        var tokens = Parse(expression);
        if (tokens.Count == 0)
        {
            return true;
        }

        var candidate = value ?? string.Empty;
        return tokens.Any(token => token.Matches(candidate));
    }

    public static bool MatchesNumber(int value, string? expression)
    {
        var tokens = Parse(expression);
        if (tokens.Count == 0)
        {
            return true;
        }

        return tokens.Any(token => token.MatchesNumber(value));
    }

    public static bool MatchesCpu(ResourcePulse pulse, string? expression)
    {
        return MatchesQuantity(pulse.CpuMillicores ?? pulse.CpuLimitMillicores, pulse.CpuSummaryDisplay, expression, ParseCpuQuantity);
    }

    public static bool MatchesBytes(long? current, long? limit, string display, string? expression)
    {
        return MatchesQuantity(current ?? limit, display, expression, ParseByteQuantity);
    }

    private static bool MatchesQuantity(
        double? value,
        string display,
        string? expression,
        Func<string, double?> parse)
    {
        if (string.IsNullOrWhiteSpace(expression))
        {
            return true;
        }

        var tokens = ParseQuantityTokens(expression, parse);
        if (tokens.Count == 0)
        {
            return MatchesText(display, expression);
        }

        if (value is null)
        {
            return false;
        }

        var rangeTokens = tokens.Where(token => token.IsRange).ToList();
        var exactTokens = tokens.Where(token => !token.IsRange).ToList();
        var rangeMatches = rangeTokens.Count == 0 || rangeTokens.All(token => token.Matches(value.Value));
        var exactMatches = exactTokens.Count == 0 || exactTokens.Any(token => token.Matches(value.Value));
        return rangeMatches && exactMatches;
    }

    public static bool MatchesAge(string value, string? expression)
    {
        if (string.IsNullOrWhiteSpace(expression))
        {
            return true;
        }

        var duration = ParseHumanDuration(value);
        if (duration is null)
        {
            return MatchesText(value, expression);
        }

        var tokens = ParseDurationTokens(expression);
        if (tokens.Count == 0)
        {
            return MatchesText(value, expression);
        }

        var rangeTokens = tokens.Where(token => token.IsRange).ToList();
        var exactTokens = tokens.Where(token => !token.IsRange).ToList();
        var rangeMatches = rangeTokens.Count == 0 || rangeTokens.All(token => token.Matches(duration.Value));
        var exactMatches = exactTokens.Count == 0 || exactTokens.Any(token => token.Matches(duration.Value));
        return rangeMatches && exactMatches;
    }

    public static IReadOnlyList<string> ExactTerms(string? expression)
    {
        return Parse(expression)
            .Where(token => token.Kind == FilterTokenKind.Exact)
            .Select(token => token.Value)
            .Where(value => value.Length > 0)
            .Distinct(StringComparer.Ordinal)
            .ToList();
    }

    public static int NormalizeLimit(int limit)
    {
        return limit <= 0 ? 256 : Math.Clamp(limit, 1, 5_000);
    }

    // Restart count under which a "Running" row's restarts are treated as normal background noise,
    // not a problem. Non-Running rows still surface any restart count.
    public const int DefaultRestartOutlierThreshold = 3;

    public static bool IsProblem(FlatResourceRow row)
    {
        return ProblemReason(row).Length > 0;
    }

    public static bool IsProblem(FlatResourceRow row, int restartOutlierThreshold)
    {
        return ProblemReason(row, restartOutlierThreshold).Length > 0;
    }

    public static bool IsError(FlatResourceRow row, int restartOutlierThreshold)
    {
        var problem = ProblemReason(row, restartOutlierThreshold);
        return problem.Contains("Crash", StringComparison.OrdinalIgnoreCase)
               || problem.Contains("Error", StringComparison.OrdinalIgnoreCase)
               || problem.Contains("Failed", StringComparison.OrdinalIgnoreCase)
               || problem.Contains("Unavailable", StringComparison.OrdinalIgnoreCase)
               || row.Status is "CrashLoopBackOff" or "CreateContainerConfigError" or "CreateContainerError" or "ErrImagePull" or "Error" or "Failed" or "ImagePullBackOff" or "NotReady" or "OOMKilled" or "Unavailable";
    }

    public static bool IsActivity(FlatResourceRow row)
    {
        if (row.Kind == "Event")
        {
            return IsEventActivity(row);
        }

        if (ActivityStatuses.Contains(row.Status))
        {
            return true;
        }

        if (ParseHumanDuration(row.LastChange) is { } changed && changed <= TimeSpan.FromMinutes(15))
        {
            return true;
        }

        return ParseHumanDuration(row.Age) is { } age && age <= TimeSpan.FromMinutes(15);
    }

    public static string ProblemReason(FlatResourceRow row)
    {
        return ProblemReason(row, DefaultRestartOutlierThreshold);
    }

    public static string ProblemReason(FlatResourceRow row, int restartOutlierThreshold)
    {
        if (row.Freshness == FreshnessState.Forbidden)
        {
            return "RBAC hidden";
        }

        if (row.Kind == "Event")
        {
            return IsActiveProblemEvent(row)
                ? string.IsNullOrWhiteSpace(row.EventReason) ? row.Status : row.EventReason
                : string.Empty;
        }

        if (row.Status is "Succeeded" or "Complete" or "Completed")
        {
            return string.Empty;
        }

        if (row.Restarts > 0 && IsRestartFlagged(row, restartOutlierThreshold))
        {
            return $"Restarted {row.Restarts}";
        }

        if (string.Equals(row.Kind, "Pod", StringComparison.OrdinalIgnoreCase)
            && string.Equals(row.Status, "Pending", StringComparison.OrdinalIgnoreCase)
            && IsTransientPodStartup(row))
        {
            return string.Empty;
        }

        if (ProblemStatuses.Contains(row.Status)
            && !string.Equals(row.Status, "Warning", StringComparison.OrdinalIgnoreCase)
            && !string.Equals(row.Status, "Unknown", StringComparison.OrdinalIgnoreCase)
            && !string.Equals(row.Status, "Pending", StringComparison.OrdinalIgnoreCase))
        {
            return row.Status;
        }

        if (ReadyParts(row.Ready) is { } ready && ready.Ready < ready.Total)
        {
            if (IsTransientPodStartup(row))
            {
                return string.Empty;
            }

            return $"Ready {row.Ready}";
        }

        return ProblemStatuses.Contains(row.Status) ? row.Status : string.Empty;
    }

    // Statistical cutoff for "unusually high" restarts: max(default floor, P75 + 1.5*IQR)
    // over the Running rows in scope. Small samples fall back to the floor.
    public static int RestartOutlierThreshold(IEnumerable<FlatResourceRow> rows)
    {
        ArgumentNullException.ThrowIfNull(rows);
        var samples = rows
            .Where(row => string.Equals(row.Status, "Running", StringComparison.OrdinalIgnoreCase))
            .Select(row => row.Restarts)
            .OrderBy(value => value)
            .ToList();
        if (samples.Count < 4)
        {
            return DefaultRestartOutlierThreshold;
        }

        var p25 = Percentile(samples, 0.25);
        var p75 = Percentile(samples, 0.75);
        var iqr = p75 - p25;
        var statistical = (int)Math.Ceiling(p75 + 1.5 * iqr);
        return Math.Max(DefaultRestartOutlierThreshold, statistical);
    }

    private static bool IsRestartFlagged(FlatResourceRow row, int restartOutlierThreshold)
    {
        if (!string.Equals(row.Status, "Running", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        return row.Restarts > restartOutlierThreshold;
    }

    private static bool IsTransientPodStartup(FlatResourceRow row)
    {
        if (!string.Equals(row.Kind, "Pod", StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        if (row.Status is "CrashLoopBackOff" or "CreateContainerConfigError" or "CreateContainerError" or "ErrImagePull" or "Error" or "Failed" or "ImagePullBackOff" or "NotReady" or "OOMKilled" or "Unavailable" or "Unknown" or "Warning" or "Terminating")
        {
            return false;
        }

        var reference = ParseHumanDuration(row.LastChange) ?? ParseHumanDuration(row.Age);
        return reference is { } age && age <= PodStartupProblemGrace;
    }

    private static bool IsEventActivity(FlatResourceRow row)
    {
        if (row.Status is "Observed" or "Historical")
        {
            return false;
        }

        if (row.Status is "Succeeded" or "Complete" or "Completed" or "Resolved")
        {
            return IsEventRecent(row, EventSuccessActivityTtl);
        }

        if (IsWarningEventStatus(row.Status))
        {
            return IsEventRecent(row, EventWarningActivityTtl);
        }

        return IsEventRecent(row, EventNormalActivityTtl);
    }

    private static bool IsActiveProblemEvent(FlatResourceRow row)
    {
        return IsWarningEventStatus(row.Status) && IsEventRecent(row, EventWarningActivityTtl);
    }

    private static bool IsWarningEventStatus(string status)
    {
        return status is "Warning" or "Error" or "Failed" or "Critical" or "Unavailable";
    }

    private static bool IsEventRecent(FlatResourceRow row, TimeSpan ttl)
    {
        return ParseHumanDuration(row.LastChange) is { } changed
            ? changed <= ttl
            : ParseHumanDuration(row.Age) is { } age && age <= ttl;
    }

    private static double Percentile(IReadOnlyList<int> sortedAscending, double quantile)
    {
        if (sortedAscending.Count == 0)
        {
            return 0;
        }

        var position = quantile * (sortedAscending.Count - 1);
        var lowerIndex = (int)Math.Floor(position);
        var upperIndex = (int)Math.Ceiling(position);
        if (lowerIndex == upperIndex)
        {
            return sortedAscending[lowerIndex];
        }

        var weight = position - lowerIndex;
        return sortedAscending[lowerIndex] + (sortedAscending[upperIndex] - sortedAscending[lowerIndex]) * weight;
    }

    private static bool MatchesSearch(FlatResourceRow row, string? expression)
    {
        if (string.IsNullOrWhiteSpace(expression))
        {
            return true;
        }

        var values = new[]
        {
            row.Kind,
            row.Id,
            row.Name,
            row.Namespace ?? "cluster",
            row.Cluster,
            row.Status,
            row.Ready,
            row.Node ?? string.Empty,
            row.ImageSummary,
            row.Owner ?? string.Empty,
            row.CpuSummaryDisplay,
            row.MemorySummaryDisplay,
            row.StorageDisplay,
            row.Age,
            row.LastChange
        };
        return values.Any(value => MatchesText(value, expression));
    }

    public static TimeSpan? ParseHumanDuration(string? value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return null;
        }

        var text = value.Trim();
        if (text.Equals("now", StringComparison.OrdinalIgnoreCase))
        {
            return TimeSpan.Zero;
        }

        var index = 0;
        var total = TimeSpan.Zero;
        var matched = false;
        while (index < text.Length)
        {
            while (index < text.Length && char.IsWhiteSpace(text[index]))
            {
                index++;
            }

            var numberStart = index;
            while (index < text.Length && char.IsDigit(text[index]))
            {
                index++;
            }

            if (numberStart == index)
            {
                return matched ? total : null;
            }

            var unitStart = index;
            while (index < text.Length && char.IsLetter(text[index]))
            {
                index++;
            }

            var unit = text[unitStart..index].ToLowerInvariant();
            if (!int.TryParse(text[numberStart..unitStart], NumberStyles.Integer, CultureInfo.InvariantCulture, out var number))
            {
                return null;
            }

            total += unit switch
            {
                "" or "s" or "sec" or "secs" => TimeSpan.FromSeconds(number),
                "ms" => TimeSpan.FromMilliseconds(number),
                "m" or "min" or "mins" => TimeSpan.FromMinutes(number),
                "h" or "hr" or "hrs" => TimeSpan.FromHours(number),
                "d" or "day" or "days" => TimeSpan.FromDays(number),
                "w" or "week" or "weeks" => TimeSpan.FromDays(number * 7),
                _ => TimeSpan.MinValue
            };
            if (total < TimeSpan.Zero)
            {
                return null;
            }

            matched = true;
        }

        return matched ? total : null;
    }

    private static (int Ready, int Total)? ReadyParts(string ready)
    {
        var parts = ready.Split('/', StringSplitOptions.TrimEntries);
        if (parts.Length != 2)
        {
            return null;
        }

        return int.TryParse(parts[0], NumberStyles.Integer, CultureInfo.InvariantCulture, out var readyCount)
               && int.TryParse(parts[1], NumberStyles.Integer, CultureInfo.InvariantCulture, out var total)
            ? (readyCount, total)
            : null;
    }

    private static IReadOnlyList<FilterToken> Parse(string? expression)
    {
        if (string.IsNullOrWhiteSpace(expression))
        {
            return Array.Empty<FilterToken>();
        }

        var tokens = new List<FilterToken>();
        var text = expression.Trim();
        var index = 0;
        while (index < text.Length)
        {
            while (index < text.Length && char.IsWhiteSpace(text[index]))
            {
                index++;
            }

            if (index >= text.Length)
            {
                break;
            }

            if (text[index] == '"')
            {
                var quoted = ReadDelimited(text, ref index, '"');
                tokens.Add(new FilterToken(FilterTokenKind.Exact, quoted));
                continue;
            }

            if (text[index] == '/')
            {
                var regex = ReadDelimited(text, ref index, '/');
                if (regex.Length > 0)
                {
                    tokens.Add(new FilterToken(FilterTokenKind.Regex, regex));
                }

                continue;
            }

            var start = index;
            while (index < text.Length && !char.IsWhiteSpace(text[index]))
            {
                index++;
            }

            var raw = text[start..index].Trim();
            if (raw.Length > 0)
            {
                tokens.Add(FilterToken.FromRaw(raw));
            }
        }

        return tokens;
    }

    private static string ReadDelimited(string text, ref int index, char delimiter)
    {
        index++;
        var value = new StringBuilder();
        while (index < text.Length)
        {
            var character = text[index++];
            if (character == delimiter)
            {
                break;
            }

            if (character == '\\' && index < text.Length)
            {
                value.Append(text[index++]);
                continue;
            }

            value.Append(character);
        }

        return value.ToString();
    }

    private static readonly HashSet<string> ProblemStatuses = new(StringComparer.OrdinalIgnoreCase)
    {
        "CrashLoopBackOff",
        "CreateContainerConfigError",
        "CreateContainerError",
        "ErrImagePull",
        "Error",
        "Failed",
        "ImagePullBackOff",
        "NotReady",
        "OOMKilled",
        "Pending",
        "Terminating",
        "Unavailable",
        "Unknown",
        "Warning"
    };

    private static readonly HashSet<string> ActivityStatuses = new(StringComparer.OrdinalIgnoreCase)
    {
        "Pending",
        "Progressing",
        "Running",
        "Terminating",
        "Updating",
        "Warning",
        "CrashLoopBackOff",
        "CreateContainerConfigError",
        "CreateContainerError",
        "ErrImagePull",
        "Error",
        "Failed",
        "ImagePullBackOff",
        "NotReady",
        "OOMKilled",
        "Unavailable"
    };

    private static IReadOnlyList<DurationFilterToken> ParseDurationTokens(string expression)
    {
        var tokens = new List<DurationFilterToken>();
        foreach (var raw in expression.Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            var op = "";
            var value = raw;
            foreach (var candidate in new[] { ">=", "=>", "<=", "=<", ">", "<", "=" })
            {
                if (raw.StartsWith(candidate, StringComparison.Ordinal) && raw.Length > candidate.Length)
                {
                    op = candidate;
                    value = raw[candidate.Length..];
                    break;
                }
            }

            if (ParseHumanDuration(value) is { } duration)
            {
                tokens.Add(new DurationFilterToken(op, duration));
            }
        }

        return tokens;
    }

    private static IReadOnlyList<QuantityFilterToken> ParseQuantityTokens(string expression, Func<string, double?> parse)
    {
        var tokens = new List<QuantityFilterToken>();
        foreach (var raw in expression.Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            var op = "";
            var value = raw;
            foreach (var candidate in new[] { ">=", "=>", "<=", "=<", ">", "<", "=" })
            {
                if (raw.StartsWith(candidate, StringComparison.Ordinal) && raw.Length > candidate.Length)
                {
                    op = candidate;
                    value = raw[candidate.Length..];
                    break;
                }
            }

            if (parse(value) is { } numeric)
            {
                tokens.Add(new QuantityFilterToken(op, numeric));
            }
        }

        return tokens;
    }

    private static double? ParseCpuQuantity(string raw)
    {
        var text = raw.Trim().ToLowerInvariant();
        if (text.Length == 0)
        {
            return null;
        }

        return text switch
        {
            _ when text.EndsWith("millicores", StringComparison.Ordinal) => ParseDouble(text[..^10]),
            _ when text.EndsWith("millicore", StringComparison.Ordinal) => ParseDouble(text[..^9]),
            _ when text.EndsWith('m') => ParseDouble(text[..^1]),
            _ when text.EndsWith("cores", StringComparison.Ordinal) => ParseDouble(text[..^5]) * 1000d,
            _ when text.EndsWith("core", StringComparison.Ordinal) => ParseDouble(text[..^4]) * 1000d,
            _ when text.EndsWith('c') => ParseDouble(text[..^1]) * 1000d,
            _ => ParseDouble(text) * 1000d
        };
    }

    private static double? ParseByteQuantity(string raw)
    {
        var text = raw.Trim().ToLowerInvariant();
        if (text.Length == 0)
        {
            return null;
        }

        var suffixes = new (string Suffix, double Multiplier)[]
        {
            ("kib", Math.Pow(1024, 1)),
            ("ki", Math.Pow(1024, 1)),
            ("kb", Math.Pow(1000, 1)),
            ("k", Math.Pow(1000, 1)),
            ("mib", Math.Pow(1024, 2)),
            ("mi", Math.Pow(1024, 2)),
            ("mb", Math.Pow(1000, 2)),
            ("m", Math.Pow(1000, 2)),
            ("gib", Math.Pow(1024, 3)),
            ("gi", Math.Pow(1024, 3)),
            ("gb", Math.Pow(1000, 3)),
            ("g", Math.Pow(1000, 3)),
            ("tib", Math.Pow(1024, 4)),
            ("ti", Math.Pow(1024, 4)),
            ("tb", Math.Pow(1000, 4)),
            ("t", Math.Pow(1000, 4)),
            ("pib", Math.Pow(1024, 5)),
            ("pi", Math.Pow(1024, 5)),
            ("pb", Math.Pow(1000, 5)),
            ("p", Math.Pow(1000, 5)),
            ("b", 1d)
        };

        foreach (var (suffix, multiplier) in suffixes.OrderByDescending(item => item.Suffix.Length))
        {
            if (text.EndsWith(suffix, StringComparison.Ordinal))
            {
                return ParseDouble(text[..^suffix.Length]) * multiplier;
            }
        }

        return ParseDouble(text);
    }

    private static double? ParseDouble(string value)
    {
        return double.TryParse(value.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out var result)
            ? result
            : null;
    }

    private sealed record DurationFilterToken(string Operator, TimeSpan Duration)
    {
        public bool IsRange => Operator is ">" or ">=" or "=>" or "<" or "<=" or "=<";

        public bool Matches(TimeSpan value)
        {
            return Operator switch
            {
                ">" => value > Duration,
                ">=" or "=>" => value >= Duration,
                "<" => value < Duration,
                "<=" or "=<" => value <= Duration,
                "=" => value == Duration,
                "" => value <= Duration,
                _ => value == Duration
            };
        }
    }

    private sealed record QuantityFilterToken(string Operator, double Expected)
    {
        public bool IsRange => Operator is ">" or ">=" or "=>" or "<" or "<=" or "=<";

        public bool Matches(double value)
        {
            return Operator switch
            {
                ">" => value > Expected,
                ">=" or "=>" => value >= Expected,
                "<" => value < Expected,
                "<=" or "=<" => value <= Expected,
                "=" => Math.Abs(value - Expected) < 0.0001d,
                "" => Math.Abs(value - Expected) < 0.0001d,
                _ => Math.Abs(value - Expected) < 0.0001d
            };
        }
    }

    private enum FilterTokenKind
    {
        Contains,
        Exact,
        Regex,
        StartsWith,
        EndsWith,
        Number
    }

    private sealed record FilterToken(FilterTokenKind Kind, string Value, string Operator = "")
    {
        public static FilterToken FromRaw(string raw)
        {
            foreach (var op in new[] { ">=", "=>", "<=", "=<", ">", "<", "=" })
            {
                if (raw.StartsWith(op, StringComparison.Ordinal) && raw.Length > op.Length)
                {
                    var value = raw[op.Length..];
                    return int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out _)
                        ? new FilterToken(FilterTokenKind.Number, value, op)
                        : new FilterToken(op == "=" ? FilterTokenKind.Exact : FilterTokenKind.Contains, value);
                }
            }

            if (raw.StartsWith('~') && raw.Length > 1)
            {
                return new FilterToken(FilterTokenKind.StartsWith, raw[1..]);
            }

            if (raw.EndsWith('~') && raw.Length > 1)
            {
                return new FilterToken(FilterTokenKind.EndsWith, raw[..^1]);
            }

            return int.TryParse(raw, NumberStyles.Integer, CultureInfo.InvariantCulture, out _)
                ? new FilterToken(FilterTokenKind.Number, raw, "=")
                : new FilterToken(FilterTokenKind.Contains, raw);
        }

        public bool Matches(string candidate)
        {
            return Kind switch
            {
                FilterTokenKind.Contains => candidate.Contains(Value, StringComparison.OrdinalIgnoreCase),
                FilterTokenKind.Exact => string.Equals(candidate, Value, StringComparison.OrdinalIgnoreCase),
                FilterTokenKind.Regex => MatchesRegex(candidate),
                FilterTokenKind.StartsWith => candidate.StartsWith(Value, StringComparison.OrdinalIgnoreCase),
                FilterTokenKind.EndsWith => candidate.EndsWith(Value, StringComparison.OrdinalIgnoreCase),
                FilterTokenKind.Number => int.TryParse(candidate, NumberStyles.Integer, CultureInfo.InvariantCulture, out var number) && MatchesNumber(number),
                _ => false
            };
        }

        public bool MatchesNumber(int number)
        {
            if (!int.TryParse(Value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var expected))
            {
                return Matches(number.ToString(CultureInfo.InvariantCulture));
            }

            return Operator switch
            {
                ">" => number > expected,
                ">=" or "=>" => number >= expected,
                "<" => number < expected,
                "<=" or "=<" => number <= expected,
                "=" => number == expected,
                "" => number == expected,
                _ => number == expected
            };
        }

        private bool MatchesRegex(string candidate)
        {
            try
            {
                return Regex.IsMatch(candidate, Value, RegexOptions.IgnoreCase | RegexOptions.CultureInvariant, TimeSpan.FromMilliseconds(100));
            }
            catch (ArgumentException)
            {
                return false;
            }
            catch (RegexMatchTimeoutException)
            {
                return false;
            }
        }
    }
}
