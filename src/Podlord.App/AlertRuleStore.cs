using System.Text.Json;
using System.Text.Json.Serialization;
using Podlord.Core;

namespace Podlord.App;

public static class AlertRuleStore
{
    private static readonly JsonSerializerOptions Options = new(JsonSerializerDefaults.Web)
    {
        WriteIndented = true,
        TypeInfoResolver = AlertRuleJsonContext.Default
    };
    private static readonly AlertRuleJsonContext JsonContext = new(Options);

    public static IReadOnlyList<AlertRule> Load()
    {
        var path = PathToStore();
        if (!File.Exists(path))
        {
            return AlertRuleCatalog.DefaultRules;
        }

        try
        {
            var saved = JsonSerializer.Deserialize(File.ReadAllText(path), JsonContext.IReadOnlyListAlertRule)
                        ?? Array.Empty<AlertRule>();
            return MergeDefaults(saved);
        }
        catch (JsonException)
        {
            return AlertRuleCatalog.DefaultRules;
        }
    }

    public static void Save(IEnumerable<AlertRule> rules)
    {
        var path = PathToStore();
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var merged = MergeDefaults(rules).OrderBy(rule => rule.BuiltIn ? 0 : 1).ThenBy(rule => rule.Name, StringComparer.Ordinal).ToList();
        File.WriteAllText(path, JsonSerializer.Serialize(merged, JsonContext.IReadOnlyListAlertRule));
    }

    private static IReadOnlyList<AlertRule> MergeDefaults(IEnumerable<AlertRule> savedRules)
    {
        var saved = savedRules.ToDictionary(rule => rule.Id, StringComparer.Ordinal);
        var defaults = AlertRuleCatalog.DefaultRules
            .Select(defaultRule => saved.TryGetValue(defaultRule.Id, out var savedRule)
                ? defaultRule with { Enabled = savedRule.Enabled }
                : defaultRule)
            .ToList();
        var custom = saved.Values
            .Where(rule => !rule.BuiltIn)
            .Where(rule => defaults.All(defaultRule => !defaultRule.Id.Equals(rule.Id, StringComparison.Ordinal)))
            .OrderBy(rule => rule.Name, StringComparer.Ordinal)
            .ToList();
        return defaults.Concat(custom).ToList();
    }

    private static string PathToStore()
    {
        var overrideRoot = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        if (!string.IsNullOrWhiteSpace(overrideRoot))
        {
            return Path.Combine(overrideRoot, "alert-rules.json");
        }

        var root = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        return Path.Combine(root, "Podlord", "alert-rules.json");
    }
}

[JsonSerializable(typeof(AlertRule))]
[JsonSerializable(typeof(AlertRuleMatchers))]
[JsonSerializable(typeof(AlertMatcherGroup))]
[JsonSerializable(typeof(AlertMatcherCriterion))]
[JsonSerializable(typeof(AlertRuleActions))]
[JsonSerializable(typeof(AlertRuleUntil))]
[JsonSerializable(typeof(IReadOnlyList<AlertRule>))]
internal sealed partial class AlertRuleJsonContext : JsonSerializerContext;
