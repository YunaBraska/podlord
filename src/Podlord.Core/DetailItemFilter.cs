namespace Podlord.Core;

public static class DetailItemFilter
{
    private static readonly HashSet<string> OptionalLabels = new(StringComparer.Ordinal)
    {
        "CPU",
        "CPU %",
        "Memory",
        "Memory %",
        "Network",
        "Storage",
        "Metric source",
        "Node",
        "Image",
        "Owner",
        "Ready",
        "Restarts",
        "Issue",
        "Reason",
        "Message",
        "Involved object",
        "Owned copy",
        "Server",
        "Detail",
        "UID"
    };

    public static IReadOnlyList<DetailItem> Available(IEnumerable<DetailItem> items)
    {
        var snapshot = items.ToList();
        var kind = snapshot.FirstOrDefault(item => item.Label.Equals("Kind", StringComparison.Ordinal))?.Value ?? string.Empty;
        return snapshot
            .Where(item => IsAvailable(item, kind))
            .ToList();
    }

    public static bool IsAvailable(DetailItem item, string kind)
    {
        var value = item.Value.Trim();
        if (!OptionalLabels.Contains(item.Label))
        {
            return value.Length > 0;
        }

        if (value.Length == 0 || value == "-")
        {
            return false;
        }

        if (item.Label.Equals("Issue", StringComparison.Ordinal) && value.Equals("none", StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        if (item.Label.Equals("Metric source", StringComparison.Ordinal) && value.Equals("API", StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        if (item.Label.Equals("Restarts", StringComparison.Ordinal)
            && !kind.Equals("Pod", StringComparison.Ordinal)
            && value == "0")
        {
            return false;
        }

        return true;
    }
}
