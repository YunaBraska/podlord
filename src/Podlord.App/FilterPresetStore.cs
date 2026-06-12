using System.Text.Json;
using System.Text.Json.Serialization;

namespace Podlord.App;

public sealed record FilterPreset(
    string Name,
    bool ProblemsOnly,
    string Search,
    string Id,
    string Issue,
    string Kind,
    string NameFilter,
    string Namespace,
    string Cluster,
    string Status,
    string Age,
    string Node,
    string Image,
    string Ready,
    string Restarts,
    string Owner,
    string Limit,
    bool ActivityOnly = false,
    string Cpu = "",
    string Memory = "",
    string Storage = "")
{
    [JsonIgnore]
    public bool CanDelete => !Name.Equals(FilterPresetStore.DefaultFilterName, StringComparison.OrdinalIgnoreCase);

    [JsonIgnore]
    public bool CanRename => CanDelete;
}

public static class FilterPresetStore
{
    public const string DefaultFilterName = "default";

    public static FilterPreset DefaultFilter { get; } = new(
        DefaultFilterName,
        false,
        string.Empty,
        string.Empty,
        string.Empty,
        string.Empty,
        string.Empty,
        string.Empty,
        string.Empty,
        string.Empty,
        string.Empty,
        string.Empty,
        string.Empty,
        string.Empty,
        string.Empty,
        string.Empty,
        "256",
        false);

    private static readonly JsonSerializerOptions Options = new(JsonSerializerDefaults.Web)
    {
        WriteIndented = true,
        TypeInfoResolver = FilterPresetJsonContext.Default
    };
    private static readonly FilterPresetJsonContext JsonContext = new(Options);

    public static IReadOnlyList<FilterPreset> Load()
    {
        var path = PathToStore();
        if (!File.Exists(path))
        {
            return [DefaultFilter];
        }

        try
        {
            return EnsureDefault(JsonSerializer.Deserialize(File.ReadAllText(path), JsonContext.IReadOnlyListFilterPreset)
                                 ?? Array.Empty<FilterPreset>());
        }
        catch (JsonException)
        {
            return [DefaultFilter];
        }
    }

    public static void Save(IEnumerable<FilterPreset> presets)
    {
        var path = PathToStore();
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        IReadOnlyList<FilterPreset> ordered = EnsureDefault(presets).OrderBy(preset => preset.Name, StringComparer.Ordinal).ToList();
        File.WriteAllText(path, JsonSerializer.Serialize(ordered, JsonContext.IReadOnlyListFilterPreset));
    }

    private static IReadOnlyList<FilterPreset> EnsureDefault(IEnumerable<FilterPreset> presets)
    {
        var items = presets.ToList();
        if (items.All(preset => !preset.Name.Equals(DefaultFilterName, StringComparison.OrdinalIgnoreCase)))
        {
            items.Add(DefaultFilter);
        }

        return items;
    }

    private static string PathToStore()
    {
        var overrideRoot = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        if (!string.IsNullOrWhiteSpace(overrideRoot))
        {
            return Path.Combine(overrideRoot, "filter-presets.json");
        }

        var root = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        return Path.Combine(root, "Podlord", "filter-presets.json");
    }
}

[JsonSerializable(typeof(FilterPreset))]
[JsonSerializable(typeof(IReadOnlyList<FilterPreset>))]
internal sealed partial class FilterPresetJsonContext : JsonSerializerContext;
