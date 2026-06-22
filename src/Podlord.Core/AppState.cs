using System.Text.Json;

namespace Podlord.Core;

public sealed class AppState
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web)
    {
        WriteIndented = true
    };

    private readonly object sync = new();
    private readonly string? storePath;
    private readonly string? configDirectory;
    private readonly KubeconfigImporter importer;
    private AppStore store;

    private AppState(string? storePath, string? configDirectory, AppStore store, KubeconfigImporter importer)
    {
        this.storePath = storePath;
        this.configDirectory = configDirectory;
        this.store = store;
        this.importer = importer;
    }

    public static AppState InMemory(IPodlordClock? clock = null)
    {
        return new AppState(null, null, AppStore.Empty, new KubeconfigImporter(clock));
    }

    public static AppState InMemoryWithConfigDirectory(string configDirectory, IPodlordClock? clock = null)
    {
        Directory.CreateDirectory(configDirectory);
        return new AppState(null, configDirectory, AppStore.Empty, new KubeconfigImporter(clock));
    }

    public static AppState LoadDefault(IPodlordClock? clock = null)
    {
        var configRoot = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        var hasConfigOverride = !string.IsNullOrWhiteSpace(configRoot);
        if (string.IsNullOrWhiteSpace(configRoot))
        {
            configRoot = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        }

        if (string.IsNullOrWhiteSpace(configRoot))
        {
            throw PodlordException.MissingConfigDirectory();
        }

        var configDirectory = hasConfigOverride
            ? configRoot
            : Path.Combine(configRoot, "podlord");
        Directory.CreateDirectory(configDirectory);
        var storePath = Path.Combine(configDirectory, "store.json");
        var store = File.Exists(storePath)
            ? LoadStore(storePath)
            : AppStore.Empty;
        var state = new AppState(storePath, configDirectory, NormalizeStore(store, configDirectory), new KubeconfigImporter(clock));
        state.Persist();
        return state;
    }

    public AppStore Snapshot()
    {
        lock (sync)
        {
            return store;
        }
    }

    public Settings Settings()
    {
        lock (sync)
        {
            return store.Settings;
        }
    }

    public Settings SaveSettings(Settings settings)
    {
        lock (sync)
        {
            store = store with { Settings = settings };
            Persist();
            return settings;
        }
    }

    public KubeconfigImportSummary ImportHomeKubeconfig()
    {
        var home = Environment.GetEnvironmentVariable("PODLORD_HOME");
        if (string.IsNullOrWhiteSpace(home))
        {
            home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        }

        if (string.IsNullOrWhiteSpace(home))
        {
            throw PodlordException.MissingHomeDirectory();
        }

        return ImportKubeconfig(Path.Combine(home, ".kube", "config"));
    }

    public KubeconfigImportSummary ImportKubeconfig(string path)
    {
        var fullPath = Path.GetFullPath(path);
        string raw;
        try
        {
            raw = File.ReadAllText(fullPath);
        }
        catch (IOException ex)
        {
            throw PodlordException.ReadFile(fullPath, ex);
        }
        catch (UnauthorizedAccessException ex)
        {
            throw PodlordException.ReadFile(fullPath, ex);
        }

        var contentHash = PodlordText.StableHash(raw);
        var sourceName = SourceName(fullPath);
        var summary = importer.ImportText(fullPath, raw);
        var ownedPath = CopyKubeconfigToOwnedStore(fullPath, raw, contentHash);
        var contexts = summary.Contexts
            .Select(context => context with
            {
                ContextId = KubeconfigImporter.ContextId(fullPath, context.Name),
                OwnedKubeconfigPath = ownedPath,
                SourceName = sourceName,
                SourceContentHash = contentHash
            })
            .ToList();
        return ImportContexts(summary with { Contexts = contexts }, replaceFileSourcePath: fullPath);
    }

    public KubeconfigImportSummary ImportKubeconfigText(string displayName, string raw)
    {
        var sourcePath = string.IsNullOrWhiteSpace(displayName)
            ? $"podlord-paste://{PodlordText.StableHash(raw)}"
            : $"podlord-paste://{PodlordText.StableHash(displayName)}-{PodlordText.StableHash(raw)}";
        return ImportVirtualKubeconfigText(sourcePath, raw);
    }

    public KubeconfigImportSummary ImportGeneratedKubeconfigText(string sourceName, string raw)
    {
        var normalized = string.Join("-", sourceName
            .Trim()
            .Split(Path.GetInvalidFileNameChars().Concat(['/', '\\', ':']).ToArray(), StringSplitOptions.RemoveEmptyEntries));
        var sourcePath = $"podlord-generated://{(normalized.Length == 0 ? PodlordText.StableHash(raw) : normalized)}";
        return ImportVirtualKubeconfigText(sourcePath, raw);
    }

    private KubeconfigImportSummary ImportVirtualKubeconfigText(string sourcePath, string raw)
    {
        var contentHash = PodlordText.StableHash(raw);
        var summary = importer.ImportText(sourcePath, raw);
        var ownedPath = CopyPastedKubeconfigToOwnedStore(sourcePath, raw, contentHash);
        var contexts = summary.Contexts
            .Select(context => context with
            {
                ContextId = KubeconfigImporter.ContextSnapshotId(sourcePath, contentHash, context.Name),
                OwnedKubeconfigPath = ownedPath,
                SourceName = SourceName(sourcePath),
                SourceContentHash = contentHash
            })
            .ToList();
        return ImportContexts(summary with { Contexts = contexts });
    }

    public IReadOnlyList<KubeconfigImportSummary> RefreshImportedKubeconfigs()
    {
        var paths = Snapshot()
            .ImportedContexts
            .Select(context => context.SourcePath)
            .Distinct(StringComparer.Ordinal)
            .Where(path => !IsVirtualSource(path))
            .Where(File.Exists)
            .ToList();

        return paths.Select(ImportKubeconfig).ToList();
    }

    public IReadOnlyList<PodlordSession> ListSessions()
    {
        lock (sync)
        {
            return store.Sessions;
        }
    }

    public PodlordSession SwitchActiveSession(string sessionId)
    {
        lock (sync)
        {
            if (store.Sessions.All(session => session.Id != sessionId))
            {
                throw PodlordException.SessionNotFound(sessionId);
            }

            var sessions = store.Sessions
                .Select(session => session with { Active = session.Id == sessionId })
                .ToList();
            var active = sessions.Single(session => session.Active);
            store = store with { Sessions = sessions, ActiveSessionId = active.Id };
            Persist();
            return active;
        }
    }

    public PodlordSession SetSessionDisplayName(string sessionId, string displayName)
    {
        return UpdateSession(sessionId, session =>
        {
            var trimmed = displayName.Trim();
            return trimmed.Length == 0 ? session : session with { DisplayName = trimmed };
        });
    }

    public PodlordSession SetSessionSafety(string sessionId, SafetyLevel safetyLevel)
    {
        lock (sync)
        {
            PodlordSession? updated = null;
            var sessions = store.Sessions
                .Select(session =>
                {
                    if (session.Id != sessionId)
                    {
                        return session;
                    }

                    updated = session with { SafetyLevel = safetyLevel };
                    return updated;
                })
                .ToList();
            if (updated is null)
            {
                throw PodlordException.SessionNotFound(sessionId);
            }

            var contexts = store.ImportedContexts
                .Select(context => context.ContextId == updated.ContextId ? context with { SafetyLevel = safetyLevel } : context)
                .ToList();
            store = store with { Sessions = sessions, ImportedContexts = contexts };
            Persist();
            return updated;
        }
    }

    public PodlordSession SetSessionNamespaceScope(string sessionId, NamespaceScope namespaceScope)
    {
        return UpdateSession(sessionId, session => session with { NamespaceScope = namespaceScope });
    }

    public PodlordSession DuplicateSession(string sessionId)
    {
        lock (sync)
        {
            var source = store.Sessions.FirstOrDefault(session => session.Id == sessionId);
            if (source is null)
            {
                throw PodlordException.SessionNotFound(sessionId);
            }

            var copy = source with
            {
                Id = $"session-{Guid.NewGuid():N}",
                DisplayName = $"{source.DisplayName} copy",
                Active = false,
                CreatedAt = DateTimeOffset.UtcNow.ToString("yyyy-MM-dd'T'HH:mm:ss.fffffff'Z'")
            };
            store = store with { Sessions = store.Sessions.Concat([copy]).ToList() };
            Persist();
            return copy;
        }
    }

    public void RemoveImportedContext(string contextId)
    {
        lock (sync)
        {
            var context = store.ImportedContexts.FirstOrDefault(candidate => candidate.ContextId == contextId);
            if (context is null)
            {
                return;
            }

            var contexts = store.ImportedContexts.Where(candidate => candidate.ContextId != contextId).ToList();
            var sessions = store.Sessions.Where(session => session.ContextId != contextId).ToList();
            var activeSessionId = sessions.Any(session => session.Id == store.ActiveSessionId)
                ? store.ActiveSessionId
                : sessions.FirstOrDefault()?.Id;
            store = NormalizeStore(store with { ImportedContexts = contexts, Sessions = sessions, ActiveSessionId = activeSessionId }, configDirectory);
            Persist();
        }
    }

    public ImportedContext RenameImportedContext(string contextId, string displayName)
    {
        var trimmed = displayName.Trim();
        lock (sync)
        {
            var index = -1;
            for (var i = 0; i < store.ImportedContexts.Count; i++)
            {
                if (store.ImportedContexts[i].ContextId == contextId)
                {
                    index = i;
                    break;
                }
            }

            if (index < 0)
            {
                throw PodlordException.ContextNotFound(string.Empty, contextId);
            }

            if (trimmed.Length == 0)
            {
                var reset = store.ImportedContexts.ToList();
                reset[index] = reset[index] with { DisplayName = reset[index].Name };
                store = store with { ImportedContexts = reset };
                Persist();
                return reset[index];
            }

            var contexts = store.ImportedContexts.ToList();
            contexts[index] = contexts[index] with { DisplayName = trimmed };
            store = store with { ImportedContexts = contexts };
            Persist();
            return contexts[index];
        }
    }

    public ImportedContext SetImportedContextFilter(string contextId, string filterName)
    {
        var normalized = string.IsNullOrWhiteSpace(filterName) ? "default" : filterName.Trim();
        lock (sync)
        {
            var contexts = store.ImportedContexts.ToList();
            var index = contexts.FindIndex(context => context.ContextId == contextId);
            if (index < 0)
            {
                throw PodlordException.ContextNotFound(string.Empty, contextId);
            }

            contexts[index] = contexts[index] with { FilterName = normalized };
            store = store with { ImportedContexts = contexts };
            Persist();
            return contexts[index];
        }
    }

    public SessionConnection SessionConnection(string? sessionId)
    {
        lock (sync)
        {
            var session = sessionId is { Length: > 0 }
                ? store.Sessions.FirstOrDefault(candidate => candidate.Id == sessionId)
                  ?? throw PodlordException.SessionNotFound(sessionId)
                : ActiveSession(store) ?? throw PodlordException.NoActiveSession();
            var context = store.ImportedContexts.FirstOrDefault(candidate => candidate.ContextId == session.ContextId)
                          ?? throw PodlordException.ContextNotFound(session.Id, session.ContextId);
            var kubeconfigPath = context.OwnedKubeconfigPath is { Length: > 0 } owned && File.Exists(owned)
                ? owned
                : context.SourcePath;
            return new SessionConnection(session, context, kubeconfigPath);
        }
    }

    private KubeconfigImportSummary ImportContexts(KubeconfigImportSummary summary, string? replaceFileSourcePath = null)
    {
        lock (sync)
        {
            var contexts = DeduplicateImportedContexts(store.ImportedContexts);
            var sessions = store.Sessions.ToList();
            var created = 0;
            if (!string.IsNullOrWhiteSpace(replaceFileSourcePath))
            {
                var sourceIdentity = PathIdentity(replaceFileSourcePath);
                var incomingNames = summary.Contexts
                    .Select(context => context.Name)
                    .ToHashSet(StringComparer.Ordinal);
                var removedContextIds = contexts
                    .Where(context => PathIdentity(context.SourcePath).Equals(sourceIdentity, StringComparison.Ordinal)
                                      && !incomingNames.Contains(context.Name))
                    .Select(context => context.ContextId)
                    .ToHashSet(StringComparer.Ordinal);
                if (removedContextIds.Count > 0)
                {
                    contexts = contexts
                        .Where(context => !removedContextIds.Contains(context.ContextId))
                        .ToList();
                    sessions = sessions
                        .Where(session => !removedContextIds.Contains(session.ContextId))
                        .ToList();
                }
            }

            foreach (var context in summary.Contexts)
            {
                var contextId = UpsertContext(contexts, context);
                if (sessions.All(session => session.ContextId != contextId))
                {
                    sessions.Add(SessionFromContext(contexts.First(candidate => candidate.ContextId == contextId)));
                    created++;
                }
            }

            string? activeSessionId = store.ActiveSessionId;
            if (activeSessionId is not null && sessions.All(session => session.Id != activeSessionId))
            {
                activeSessionId = null;
            }

            if (activeSessionId is null && sessions.Count > 0)
            {
                var first = sessions[0] with { Active = true };
                sessions[0] = first;
                activeSessionId = first.Id;
            }

            store = NormalizeStore(store with
            {
                ImportedContexts = contexts,
                Sessions = sessions,
                ActiveSessionId = activeSessionId
            }, configDirectory);
            Persist();
            return summary with { CreatedSessionCount = created };
        }
    }

    private PodlordSession UpdateSession(string sessionId, Func<PodlordSession, PodlordSession> update)
    {
        lock (sync)
        {
            PodlordSession? updated = null;
            var sessions = store.Sessions
                .Select(session =>
                {
                    if (session.Id != sessionId)
                    {
                        return session;
                    }

                    updated = update(session);
                    return updated;
                })
                .ToList();
            if (updated is null)
            {
                throw PodlordException.SessionNotFound(sessionId);
            }

            store = store with { Sessions = sessions };
            Persist();
            return updated;
        }
    }

    private void Persist()
    {
        if (storePath is null)
        {
            return;
        }

        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(storePath) ?? ".");
            File.WriteAllText(storePath, JsonSerializer.Serialize(store, JsonOptions));
        }
        catch (IOException ex)
        {
            throw PodlordException.WriteFile(storePath, ex);
        }
        catch (UnauthorizedAccessException ex)
        {
            throw PodlordException.WriteFile(storePath, ex);
        }
    }

    private string? CopyKubeconfigToOwnedStore(string path, string raw, string contentHash)
    {
        if (configDirectory is null)
        {
            return null;
        }

        try
        {
            var snapshot = importer.SnapshotForOwnedStore(path, raw);
            var targetDirectory = Path.Combine(configDirectory, "kubeconfigs");
            Directory.CreateDirectory(targetDirectory);
            var target = Path.Combine(targetDirectory, $"{PodlordText.StableHash(path)}-{contentHash}.yaml");
            if (!File.Exists(target) || File.ReadAllText(target) != snapshot)
            {
                File.WriteAllText(target, snapshot);
            }

            return target;
        }
        catch (IOException ex)
        {
            throw PodlordException.WriteFile(path, ex);
        }
        catch (UnauthorizedAccessException ex)
        {
            throw PodlordException.WriteFile(path, ex);
        }
    }

    private string? CopyPastedKubeconfigToOwnedStore(string sourcePath, string raw, string contentHash)
    {
        if (configDirectory is null)
        {
            return null;
        }

        try
        {
            var targetDirectory = Path.Combine(configDirectory, "kubeconfigs");
            Directory.CreateDirectory(targetDirectory);
            var target = Path.Combine(targetDirectory, $"{PodlordText.StableHash(sourcePath)}-{contentHash}.yaml");
            if (!File.Exists(target) || File.ReadAllText(target) != raw)
            {
                File.WriteAllText(target, raw);
            }

            return target;
        }
        catch (IOException ex)
        {
            throw PodlordException.WriteFile(sourcePath, ex);
        }
        catch (UnauthorizedAccessException ex)
        {
            throw PodlordException.WriteFile(sourcePath, ex);
        }
    }

    private static AppStore LoadStore(string path)
    {
        try
        {
            var raw = File.ReadAllText(path);
            return JsonSerializer.Deserialize<AppStore>(raw, JsonOptions) ?? AppStore.Empty;
        }
        catch (JsonException)
        {
            return AppStore.Empty;
        }
        catch (NotSupportedException)
        {
            return AppStore.Empty;
        }
    }

    private static AppStore NormalizeStore(AppStore current, string? configDirectory)
    {
        var contexts = DeduplicateImportedContexts(current.ImportedContexts);
        var redirect = current.ImportedContexts
            .Select(context => new
            {
                Old = context.ContextId,
                New = contexts.FirstOrDefault(candidate => SameImportedContextIdentity(candidate, context))?.ContextId
            })
            .Where(item => item.New is not null && !item.Old.Equals(item.New, StringComparison.Ordinal))
            .ToDictionary(item => item.Old, item => item.New!, StringComparer.Ordinal);
        var sessions = current.Sessions
            .Select(session => redirect.TryGetValue(session.ContextId, out var contextId)
                ? session with { ContextId = contextId }
                : session)
            .ToList();
        var activeSessionId = current.ActiveSessionId;
        if (activeSessionId is not null && sessions.All(session => session.Id != activeSessionId))
        {
            activeSessionId = sessions.FirstOrDefault()?.Id;
        }

        CleanupOwnedKubeconfigs(configDirectory, contexts);
        return current with { ImportedContexts = contexts, Sessions = sessions, ActiveSessionId = activeSessionId };
    }

    private static List<ImportedContext> DeduplicateImportedContexts(IEnumerable<ImportedContext> importedContexts)
    {
        var contexts = new List<ImportedContext>();
        foreach (var context in importedContexts.OrderByDescending(ContextPreference))
        {
            UpsertContext(contexts, context);
        }

        return contexts
            .OrderByDescending(context => ParseImportedAt(context.ImportedAt))
            .ThenBy(context => context.SourcePath, StringComparer.Ordinal)
            .ThenBy(context => context.Name, StringComparer.Ordinal)
            .ToList();
    }

    private static string UpsertContext(IList<ImportedContext> contexts, ImportedContext context)
    {
        context = NormalizeImportedContext(context);
        var index = contexts.ToList().FindIndex(existing => existing.ContextId == context.ContextId);
        if (index >= 0)
        {
            contexts[index] = MergeImportedContexts(contexts[index], context);
            return contexts[index].ContextId;
        }

        var duplicate = contexts.ToList().FindIndex(existing => SameImportedContextIdentity(existing, context));
        if (duplicate >= 0)
        {
            var existingContextId = contexts[duplicate].ContextId;
            contexts[duplicate] = MergeImportedContexts(contexts[duplicate], context) with { ContextId = existingContextId };
            return contexts[duplicate].ContextId;
        }
        else
        {
            contexts.Add(context);
            return context.ContextId;
        }
    }

    private static ImportedContext MergeImportedContexts(ImportedContext existing, ImportedContext incoming)
    {
        var comparison = Comparer<(int HasHash, DateTimeOffset ImportedAt)>.Default.Compare(
            ContextPreference(incoming),
            ContextPreference(existing));
        return comparison >= 0
            ? PreserveBestUserMetadata(incoming, existing)
            : PreserveBestUserMetadata(existing, incoming);
    }

    private static ImportedContext NormalizeImportedContext(ImportedContext context)
    {
        return context with
        {
            FilterName = string.IsNullOrWhiteSpace(context.FilterName) ? "default" : context.FilterName.Trim()
        };
    }

    private static ImportedContext PreserveBestUserMetadata(ImportedContext preferred, ImportedContext duplicate)
    {
        var displayName = CustomDisplayName(preferred)
            ? preferred.DisplayName
            : CustomDisplayName(duplicate)
                ? duplicate.DisplayName
                : preferred.DisplayName;
        var safety = preferred.SafetyLevel == SafetyLevel.Unknown && duplicate.SafetyLevel != SafetyLevel.Unknown
            ? duplicate.SafetyLevel
            : preferred.SafetyLevel;
        var filterName = CustomFilterName(preferred)
            ? preferred.FilterName
            : CustomFilterName(duplicate)
                ? duplicate.FilterName
                : !string.IsNullOrWhiteSpace(preferred.FilterName)
                    ? preferred.FilterName
                    : "default";
        return preferred with { DisplayName = displayName, SafetyLevel = safety, FilterName = filterName };
    }

    private static bool CustomFilterName(ImportedContext context)
    {
        return !string.IsNullOrWhiteSpace(context.FilterName)
               && !context.FilterName.Equals("default", StringComparison.OrdinalIgnoreCase);
    }

    private static bool CustomDisplayName(ImportedContext context)
    {
        return !string.IsNullOrWhiteSpace(context.DisplayName)
               && !context.DisplayName.Equals(context.Name, StringComparison.Ordinal);
    }

    private static bool SameImportedContextIdentity(ImportedContext left, ImportedContext right)
    {
        if (PathIdentity(left.SourcePath).Equals(PathIdentity(right.SourcePath), StringComparison.Ordinal)
            && left.Name.Equals(right.Name, StringComparison.Ordinal))
        {
            return true;
        }

        if (!left.Name.Equals(right.Name, StringComparison.Ordinal)
            || !left.ClusterName.Equals(right.ClusterName, StringComparison.Ordinal)
            || !left.UserName.Equals(right.UserName, StringComparison.Ordinal)
            || !string.Equals(left.Server ?? string.Empty, right.Server ?? string.Empty, StringComparison.Ordinal))
        {
            return false;
        }

        if (!string.IsNullOrWhiteSpace(left.SourceContentHash) && !string.IsNullOrWhiteSpace(right.SourceContentHash))
        {
            return left.SourceContentHash.Equals(right.SourceContentHash, StringComparison.Ordinal);
        }

        return false;
    }

    private static (int HasHash, DateTimeOffset ImportedAt) ContextPreference(ImportedContext context)
    {
        return (string.IsNullOrWhiteSpace(context.SourceContentHash) ? 0 : 1, ParseImportedAt(context.ImportedAt));
    }

    private static DateTimeOffset ParseImportedAt(string importedAt)
    {
        return DateTimeOffset.TryParse(importedAt, out var parsed) ? parsed : DateTimeOffset.MinValue;
    }

    private static string PathIdentity(string sourcePath)
    {
        if (IsVirtualSource(sourcePath))
        {
            return sourcePath;
        }

        return Path.GetFullPath(sourcePath);
    }

    private static void CleanupOwnedKubeconfigs(string? configDirectory, IReadOnlyList<ImportedContext> contexts)
    {
        if (configDirectory is null)
        {
            return;
        }

        var targetDirectory = Path.Combine(configDirectory, "kubeconfigs");
        if (!Directory.Exists(targetDirectory))
        {
            return;
        }

        var referenced = contexts
            .Select(context => context.OwnedKubeconfigPath)
            .Where(path => !string.IsNullOrWhiteSpace(path))
            .Select(path => Path.GetFullPath(path!))
            .ToHashSet(StringComparer.Ordinal);
        foreach (var file in Directory.EnumerateFiles(targetDirectory, "*.yaml"))
        {
            if (referenced.Contains(Path.GetFullPath(file)))
            {
                continue;
            }

            try
            {
                File.Delete(file);
            }
            catch (IOException)
            {
            }
            catch (UnauthorizedAccessException)
            {
            }
        }
    }

    private static PodlordSession SessionFromContext(ImportedContext context)
    {
        var scope = context.Namespace is { Length: > 0 }
            ? NamespaceScope.One(context.Namespace)
            : NamespaceScope.All;
        return new PodlordSession(
            $"session-{Guid.NewGuid():N}",
            context.DisplayName,
            context.ContextId,
            context.ClusterName,
            scope,
            context.SafetyLevel,
            null,
            null,
            false,
            context.ImportedAt);
    }

    private static PodlordSession? ActiveSession(AppStore store)
    {
        if (store.ActiveSessionId is { Length: > 0 } activeId)
        {
            var active = store.Sessions.FirstOrDefault(session => session.Id == activeId);
            if (active is not null)
            {
                return active;
            }
        }

        return store.Sessions.FirstOrDefault(session => session.Active)
               ?? store.Sessions.FirstOrDefault();
    }

    private static bool IsVirtualSource(string path)
    {
        return path.StartsWith("podlord-paste://", StringComparison.Ordinal)
               || path.StartsWith("podlord-generated://", StringComparison.Ordinal);
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
}
