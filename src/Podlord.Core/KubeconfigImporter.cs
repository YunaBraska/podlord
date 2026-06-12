using YamlDotNet.Serialization;
using YamlDotNet.Serialization.NamingConventions;

namespace Podlord.Core;

public sealed class KubeconfigImporter
{
    private readonly IPodlordClock clock;
    private readonly IDeserializer deserializer;
    private readonly ISerializer serializer;

    public KubeconfigImporter(IPodlordClock? clock = null)
    {
        this.clock = clock ?? new SystemPodlordClock();
        deserializer = new DeserializerBuilder()
            .IgnoreUnmatchedProperties()
            .WithNamingConvention(CamelCaseNamingConvention.Instance)
            .Build();
        serializer = new SerializerBuilder()
            .ConfigureDefaultValuesHandling(DefaultValuesHandling.OmitNull)
            .Build();
    }

    public KubeconfigImportSummary ImportFile(string path)
    {
        try
        {
            var raw = File.ReadAllText(path);
            return ImportText(path, raw);
        }
        catch (IOException ex)
        {
            throw PodlordException.ReadFile(path, ex);
        }
        catch (UnauthorizedAccessException ex)
        {
            throw PodlordException.ReadFile(path, ex);
        }
    }

    public KubeconfigImportSummary ImportText(string sourcePath, string raw)
    {
        if (string.IsNullOrWhiteSpace(raw))
        {
            throw PodlordException.EmptyKubeconfig(sourcePath);
        }

        KubeconfigDocument document;
        try
        {
            document = deserializer.Deserialize<KubeconfigDocument>(raw) ?? new KubeconfigDocument();
        }
        catch (Exception ex) when (ex is YamlDotNet.Core.YamlException or InvalidOperationException)
        {
            throw PodlordException.KubeconfigParse(sourcePath, ex.Message, ex);
        }

        if (document.Contexts.Count == 0)
        {
            throw PodlordException.EmptyKubeconfig(sourcePath);
        }

        var clusters = document.Clusters.ToDictionary(cluster => cluster.Name, StringComparer.Ordinal);
        var users = document.Users.ToDictionary(user => user.Name, StringComparer.Ordinal);
        var warnings = DuplicateWarnings("context", document.Contexts.Select(context => context.Name))
            .Concat(DuplicateWarnings("cluster", document.Clusters.Select(cluster => cluster.Name)))
            .Concat(DuplicateWarnings("user", document.Users.Select(user => user.Name)))
            .Concat(CurrentContextWarnings(document))
            .ToList();
        var importedAt = PodlordText.NowUtcString(clock);
        var contexts = document.Contexts.Select(context =>
        {
            var clusterName = context.Context.Cluster ?? string.Empty;
            var userName = context.Context.User ?? string.Empty;
            clusters.TryGetValue(clusterName, out var cluster);
            users.TryGetValue(userName, out var user);
            var broken = new List<string>();

            if (clusterName.Length == 0)
            {
                broken.Add("missing cluster reference");
            }
            else if (cluster is null)
            {
                broken.Add($"cluster '{clusterName}' is not defined");
            }

            if (userName.Length == 0)
            {
                broken.Add("missing user reference");
            }
            else if (user is null)
            {
                broken.Add($"user '{userName}' is not defined");
            }

            return new ImportedContext(
                ContextId(sourcePath, context.Name),
                sourcePath,
                null,
                context.Name,
                context.Name,
                clusterName,
                userName,
                context.Context.Namespace,
                cluster?.Cluster.Server,
                user is null ? "missing-user" : AuthType(user.User),
                SafetyLevel.Unknown,
                broken,
                importedAt);
        }).ToList();

        return new KubeconfigImportSummary(sourcePath, contexts, warnings, 0);
    }

    public string SnapshotForOwnedStore(string sourcePath, string raw)
    {
        var document = deserializer.Deserialize<Dictionary<object, object?>>(raw);
        if (document is null)
        {
            throw PodlordException.KubeconfigParse(sourcePath, "document was empty");
        }

        var sourceDirectory = Path.GetDirectoryName(Path.GetFullPath(sourcePath));
        if (string.IsNullOrWhiteSpace(sourceDirectory))
        {
            return raw;
        }

        NormalizeRelativePathEntries(document, sourceDirectory);
        return serializer.Serialize(document);
    }

    public static string ContextId(string sourcePath, string contextName)
    {
        return $"ctx:{PodlordText.StableSlug(sourcePath)}:{PodlordText.StableSlug(contextName)}";
    }

    public static string ContextSnapshotId(string sourcePath, string contentHash, string contextName)
    {
        var normalizedHash = string.IsNullOrWhiteSpace(contentHash)
            ? "unknown"
            : PodlordText.StableSlug(contentHash);
        return $"ctx:{PodlordText.StableSlug(sourcePath)}:{normalizedHash}:{PodlordText.StableSlug(contextName)}";
    }

    public static string AuthType(IReadOnlyDictionary<object, object?> user)
    {
        if (user.ContainsKey("exec"))
        {
            return "exec";
        }

        if (user.TryGetValue("auth-provider", out var provider) && provider is IReadOnlyDictionary<object, object?> providerMap)
        {
            return providerMap.TryGetValue("name", out var name) && name is not null
                ? $"auth-provider:{name}"
                : "auth-provider";
        }

        if (user.ContainsKey("token") || user.ContainsKey("tokenFile"))
        {
            return "token";
        }

        if (user.ContainsKey("client-certificate-data") || user.ContainsKey("client-certificate"))
        {
            return "client-certificate";
        }

        if (user.ContainsKey("username") || user.ContainsKey("password"))
        {
            return "basic-auth";
        }

        return "unknown";
    }

    private static IReadOnlyList<string> DuplicateWarnings(string kind, IEnumerable<string> names)
    {
        var seen = new HashSet<string>(StringComparer.Ordinal);
        var duplicates = new SortedSet<string>(StringComparer.Ordinal);
        foreach (var name in names)
        {
            if (!seen.Add(name))
            {
                duplicates.Add(name);
            }
        }

        return duplicates.Select(name => $"duplicate {kind} name: {name}").ToList();
    }

    private static IReadOnlyList<string> CurrentContextWarnings(KubeconfigDocument document)
    {
        return document.CurrentContext is { Length: > 0 } current
               && document.Contexts.All(context => context.Name != current)
            ? [$"current context '{current}' is not defined in contexts"]
            : Array.Empty<string>();
    }

    private static void NormalizeRelativePathEntries(IDictionary<object, object?> node, string sourceDirectory)
    {
        foreach (var key in node.Keys.ToList())
        {
            var value = node[key];
            if (value is IDictionary<object, object?> child)
            {
                NormalizeRelativePathEntries(child, sourceDirectory);
                continue;
            }

            if (value is IList<object?> list)
            {
                foreach (var item in list.OfType<IDictionary<object, object?>>())
                {
                    NormalizeRelativePathEntries(item, sourceDirectory);
                }

                continue;
            }

            if (value is string path && IsRelativePathKey(key.ToString() ?? string.Empty) && !Path.IsPathRooted(path))
            {
                node[key] = Path.GetFullPath(Path.Combine(sourceDirectory, path));
            }
        }
    }

    private static bool IsRelativePathKey(string key)
    {
        return key is "certificate-authority" or "client-certificate" or "client-key" or "tokenFile";
    }

    private sealed class KubeconfigDocument
    {
        [YamlMember(Alias = "clusters")]
        public List<NamedCluster> Clusters { get; init; } = [];

        [YamlMember(Alias = "contexts")]
        public List<NamedContext> Contexts { get; init; } = [];

        [YamlMember(Alias = "users")]
        public List<NamedUser> Users { get; init; } = [];

        [YamlMember(Alias = "current-context", ApplyNamingConventions = false)]
        public string? CurrentContext { get; init; }
    }

    private sealed class NamedCluster
    {
        [YamlMember(Alias = "name")]
        public string Name { get; init; } = string.Empty;

        [YamlMember(Alias = "cluster")]
        public ClusterBody Cluster { get; init; } = new();
    }

    private sealed class ClusterBody
    {
        [YamlMember(Alias = "server")]
        public string? Server { get; init; }
    }

    private sealed class NamedContext
    {
        [YamlMember(Alias = "name")]
        public string Name { get; init; } = string.Empty;

        [YamlMember(Alias = "context")]
        public ContextBody Context { get; init; } = new();
    }

    private sealed class ContextBody
    {
        [YamlMember(Alias = "cluster")]
        public string? Cluster { get; init; }

        [YamlMember(Alias = "user")]
        public string? User { get; init; }

        [YamlMember(Alias = "namespace")]
        public string? Namespace { get; init; }
    }

    private sealed class NamedUser
    {
        [YamlMember(Alias = "name")]
        public string Name { get; init; } = string.Empty;

        [YamlMember(Alias = "user")]
        public Dictionary<object, object?> User { get; init; } = [];
    }
}
