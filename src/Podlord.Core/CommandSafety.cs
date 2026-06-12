namespace Podlord.Core;

public static class CommandSafety
{
    private static readonly string[] CriticalTerms =
    [
        "delete namespace",
        "delete ns",
        "drain",
        "replace --force"
    ];

    private static readonly string[] DestructiveTerms =
    [
        " delete ",
        " scale ",
        " scale --replicas=0",
        " patch ",
        " replace ",
        " apply ",
        " cordon "
    ];

    private static readonly string[] MutatingTerms =
    [
        " rollout restart ",
        " rollout undo ",
        " annotate ",
        " label ",
        " create ",
        " edit "
    ];

    public static CommandRisk Classify(string command, SafetyLevel safetyLevel)
    {
        var normalized = $" {command.Trim().ToLowerInvariant()} ";
        if (string.IsNullOrWhiteSpace(command))
        {
            return new CommandRisk(CommandRiskLevel.None, Array.Empty<string>(), false, "No command entered.");
        }

        var critical = MatchedTerms(normalized, CriticalTerms);
        if (critical.Count > 0)
        {
            return Risk(CommandRiskLevel.Critical, critical, safetyLevel, "Critical Kubernetes operation.");
        }

        var destructive = MatchedTerms(normalized, DestructiveTerms);
        if (destructive.Count > 0)
        {
            return Risk(CommandRiskLevel.Destructive, destructive, safetyLevel, "Destructive or state-changing Kubernetes operation.");
        }

        var mutating = MatchedTerms(normalized, MutatingTerms);
        if (mutating.Count > 0)
        {
            return Risk(CommandRiskLevel.Mutating, mutating, safetyLevel, "Mutating Kubernetes operation.");
        }

        if (normalized.Contains(" kubectl get ", StringComparison.Ordinal)
            || normalized.Contains(" kubectl describe ", StringComparison.Ordinal)
            || normalized.Contains(" kubectl logs ", StringComparison.Ordinal))
        {
            return new CommandRisk(CommandRiskLevel.ReadOnly, Array.Empty<string>(), false, "Read-only Kubernetes operation.");
        }

        return new CommandRisk(CommandRiskLevel.None, Array.Empty<string>(), false, "No Kubernetes risk detected.");
    }

    private static CommandRisk Risk(
        CommandRiskLevel level,
        IReadOnlyList<string> terms,
        SafetyLevel safetyLevel,
        string explanation)
    {
        var protectedContext = safetyLevel is SafetyLevel.Production or SafetyLevel.Staging or SafetyLevel.Unknown;
        return new CommandRisk(level, terms, protectedContext, explanation);
    }

    private static IReadOnlyList<string> MatchedTerms(string normalizedCommand, IEnumerable<string> terms)
    {
        return terms
            .Where(term => normalizedCommand.Contains(term, StringComparison.Ordinal))
            .Select(term => term.Trim())
            .ToList();
    }
}
