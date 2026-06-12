namespace Podlord.Core;

public sealed record ResourceHealthSegment(
    string State,
    int Count,
    double Percent);

public sealed record ResourceHealthSummary(
    int Healthy,
    int Warning,
    int Critical,
    int Unknown,
    int Total,
    IReadOnlyList<ResourceHealthSegment> Segments);

public static class ResourceHealthCalculator
{
    public static ResourceHealthSummary Calculate(
        IReadOnlyList<FlatResourceRow> rows,
        int infrastructureWarnings)
    {
        ArgumentNullException.ThrowIfNull(rows);
        if (infrastructureWarnings < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(infrastructureWarnings), "Infrastructure warnings cannot be negative.");
        }

        if (rows.Count == 0 && infrastructureWarnings == 0)
        {
            return new ResourceHealthSummary(
                Healthy: 0,
                Warning: 0,
                Critical: 0,
                Unknown: 1,
                Total: 0,
                Segments: [new ResourceHealthSegment("UNKNOWN", 1, 100)]);
        }

        var threshold = ResourceFilterMatcher.RestartOutlierThreshold(rows);
        var resourceWarnings = 0;
        var critical = 0;
        foreach (var row in rows)
        {
            var problem = ResourceFilterMatcher.ProblemReason(row, threshold);
            if (problem.Length == 0)
            {
                continue;
            }

            if (IsCritical(row, problem))
            {
                critical++;
            }
            else
            {
                resourceWarnings++;
            }
        }

        var warning = resourceWarnings + infrastructureWarnings;
        var healthy = Math.Max(0, rows.Count - resourceWarnings - critical);
        var total = rows.Count + infrastructureWarnings;
        var segments = new List<ResourceHealthSegment>(3);
        AddSegment(segments, "HEALTHY", healthy, total);
        AddSegment(segments, "WARNING", warning, total);
        AddSegment(segments, "CRITICAL", critical, total);
        return new ResourceHealthSummary(
            Healthy: healthy,
            Warning: warning,
            Critical: critical,
            Unknown: 0,
            Total: total,
            Segments: segments);
    }

    private static void AddSegment(List<ResourceHealthSegment> segments, string state, int count, int total)
    {
        if (count == 0)
        {
            return;
        }

        segments.Add(new ResourceHealthSegment(state, count, count / (double)total * 100));
    }

    private static bool IsCritical(FlatResourceRow row, string problem)
    {
        return problem.Contains("Crash", StringComparison.OrdinalIgnoreCase)
               || problem.Contains("Error", StringComparison.OrdinalIgnoreCase)
               || problem.Contains("Failed", StringComparison.OrdinalIgnoreCase)
               || problem.Contains("Unavailable", StringComparison.OrdinalIgnoreCase)
               || row.Status is "CrashLoopBackOff" or "CreateContainerConfigError" or "CreateContainerError" or "ErrImagePull" or "Error" or "Failed" or "ImagePullBackOff" or "NotReady" or "OOMKilled" or "Unavailable";
    }
}
