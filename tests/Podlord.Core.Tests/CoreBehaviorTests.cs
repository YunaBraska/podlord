using System.Text.Json;
using Podlord.Core;

namespace Podlord.Core.Tests;

public sealed class CoreBehaviorTests
{
    private static readonly FixedClock Clock = new(new DateTimeOffset(2026, 6, 9, 20, 0, 0, TimeSpan.Zero));

    [Fact]
    public void Kubeconfig_import_rejects_empty_input()
    {
        var importer = new KubeconfigImporter(Clock);

        var error = Assert.Throws<PodlordException>(() => importer.ImportText("empty.yaml", " "));

        Assert.Equal(PodlordErrorKind.EmptyKubeconfig, error.Kind);
        Assert.Equal("Import a kubeconfig that contains contexts.", error.NextAction);
    }

    [Fact]
    public void Kubeconfig_import_detects_contexts_auth_types_duplicates_and_broken_references()
    {
        var importer = new KubeconfigImporter(Clock);

        var summary = importer.ImportText("sample.yaml", SampleKubeconfig("https://cluster.example"));

        Assert.Equal("sample.yaml", summary.SourcePath);
        Assert.Contains(summary.Warnings, warning => warning == "duplicate context name: dev");
        Assert.Contains(summary.Warnings, warning => warning == "current context 'missing' is not defined in contexts");
        Assert.Contains(summary.Contexts, context => context.Name == "dev" && context.AuthType == "token");
        Assert.Contains(summary.Contexts, context => context.Name == "exec-dev" && context.AuthType == "exec");
        Assert.Contains(summary.Contexts, context => context.BrokenReferences.Contains("cluster 'ghost' is not defined"));
    }

    [Fact]
    public void Kubeconfig_snapshot_normalizes_relative_file_references()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, """
apiVersion: v1
clusters:
- name: dev
  cluster:
    server: http://localhost
    certificate-authority: ca.crt
contexts:
- name: dev
  context:
    cluster: dev
    user: dev
users:
- name: dev
  user:
    client-certificate: cert.pem
    client-key: key.pem
""");
        var importer = new KubeconfigImporter(Clock);

        var snapshot = importer.SnapshotForOwnedStore(kubeconfig, File.ReadAllText(kubeconfig));

        Assert.Contains(Path.Combine(directory, "ca.crt"), snapshot);
        Assert.Contains(Path.Combine(directory, "cert.pem"), snapshot);
        Assert.Contains(Path.Combine(directory, "key.pem"), snapshot);
    }

    [Fact]
    public void App_state_imports_kubeconfig_creates_session_and_keeps_original_file_external()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, SampleKubeconfig("http://127.0.0.1:1"));
        var state = AppState.InMemoryWithConfigDirectory(directory, Clock);

        var summary = state.ImportKubeconfig(kubeconfig);
        var snapshot = state.Snapshot();

        Assert.True(summary.CreatedSessionCount > 0);
        Assert.NotEmpty(snapshot.ImportedContexts);
        Assert.NotEmpty(snapshot.Sessions);
        Assert.NotNull(snapshot.ActiveSessionId);
        Assert.All(snapshot.ImportedContexts, context => Assert.NotEqual(kubeconfig, context.OwnedKubeconfigPath));
        Assert.True(File.Exists(snapshot.ImportedContexts[0].OwnedKubeconfigPath));
    }

    [Fact]
    public void App_state_updates_sessions_and_errors_explicitly_for_unknown_session()
    {
        var state = AppState.InMemory(Clock);
        state.ImportKubeconfigText("paste", SampleKubeconfig("http://127.0.0.1:1"));
        var session = state.ListSessions()[0];

        var renamed = state.SetSessionDisplayName(session.Id, "Payments");
        var scoped = state.SetSessionNamespaceScope(session.Id, NamespaceScope.One("payments"));
        var safe = state.SetSessionSafety(session.Id, SafetyLevel.Production);
        var copy = state.DuplicateSession(session.Id);

        Assert.Equal("Payments", renamed.DisplayName);
        Assert.Equal("payments", scoped.NamespaceScope.Label);
        Assert.Equal(SafetyLevel.Production, safe.SafetyLevel);
        Assert.NotEqual(session.Id, copy.Id);
        var error = Assert.Throws<PodlordException>(() => state.SwitchActiveSession("missing"));
        Assert.Equal(PodlordErrorKind.SessionNotFound, error.Kind);
    }

    [Fact]
    public void App_state_refreshes_imported_kubeconfig_sources_after_file_change()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("dev", "http://127.0.0.1:1"));
        var state = AppState.InMemoryWithConfigDirectory(directory, Clock);
        state.ImportKubeconfig(kubeconfig);
        var first = Assert.Single(state.Snapshot().ImportedContexts);
        var ownedStore = Path.Combine(directory, "kubeconfigs");

        state.RefreshImportedKubeconfigs();
        Assert.Single(state.Snapshot().ImportedContexts);
        Assert.Single(Directory.EnumerateFiles(ownedStore, "*.yaml"));

        File.WriteAllText(kubeconfig, OneContextKubeconfig("dev", "http://127.0.0.1:2"));
        var refreshed = state.RefreshImportedKubeconfigs();
        var snapshot = state.Snapshot();
        var contexts = snapshot.ImportedContexts
            .Where(context => context.SourcePath == Path.GetFullPath(kubeconfig))
            .ToList();

        Assert.Single(refreshed);
        Assert.Equal(2, contexts.Count);
        Assert.Contains(contexts, context => context.Server == "http://127.0.0.1:1");
        Assert.Contains(contexts, context => context.Server == "http://127.0.0.1:2");
        Assert.Equal(2, contexts.Select(context => context.SourceContentHash).Distinct(StringComparer.Ordinal).Count());
        Assert.Equal(2, contexts.Select(context => context.OwnedKubeconfigPath).Distinct(StringComparer.Ordinal).Count());
        Assert.All(contexts, context => Assert.Equal("config.yaml", context.SourceName));
        Assert.Equal(contexts.Count, snapshot.Sessions.Count(session => contexts.Any(context => context.ContextId == session.ContextId)));
        Assert.NotEqual(first.ContextId, contexts.Single(context => context.Server == "http://127.0.0.1:2").ContextId);
    }

    [Fact]
    public void App_state_deduplicates_identical_kubeconfig_content_and_preserves_user_metadata()
    {
        var directory = TempDirectory();
        var firstPath = Path.Combine(directory, "first.yaml");
        var secondPath = Path.Combine(directory, "second.yaml");
        var raw = OneContextKubeconfig("dev", "http://127.0.0.1:1");
        File.WriteAllText(firstPath, raw);
        File.WriteAllText(secondPath, raw);
        var state = AppState.InMemoryWithConfigDirectory(directory, Clock);
        state.ImportKubeconfig(firstPath);
        var context = Assert.Single(state.Snapshot().ImportedContexts);
        var session = Assert.Single(state.Snapshot().Sessions);

        state.RenameImportedContext(context.ContextId, "Dev Alias");
        state.SetImportedContextFilter(context.ContextId, "team-a");
        state.SetSessionSafety(session.Id, SafetyLevel.Production);
        var second = state.ImportKubeconfig(secondPath);
        var snapshot = state.Snapshot();

        var imported = Assert.Single(snapshot.ImportedContexts);
        Assert.Equal(0, second.CreatedSessionCount);
        Assert.Equal("Dev Alias", imported.DisplayName);
        Assert.Equal(SafetyLevel.Production, imported.SafetyLevel);
        Assert.Equal("team-a", imported.FilterName);
        Assert.Equal(secondPath, imported.SourcePath);
        Assert.Single(snapshot.Sessions);
        Assert.Single(Directory.EnumerateFiles(Path.Combine(directory, "kubeconfigs"), "*.yaml"));
    }

    [Fact]
    public void App_state_deduplicates_same_source_context_when_yaml_noise_changes()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        var clock = new MutableClock(new DateTimeOffset(2026, 6, 12, 8, 0, 0, TimeSpan.Zero));
        var state = AppState.InMemoryWithConfigDirectory(directory, clock);
        File.WriteAllText(kubeconfig, OneContextKubeconfig("dev", "http://127.0.0.1:1"));
        state.ImportKubeconfig(kubeconfig);
        var first = Assert.Single(state.Snapshot().ImportedContexts);
        var firstHash = first.SourceContentHash;

        clock.Now = clock.Now.AddMinutes(5);
        File.WriteAllText(kubeconfig, OneContextKubeconfig("dev", "http://127.0.0.1:1").Replace("secret-token", "rotated-token", StringComparison.Ordinal));
        state.ImportKubeconfig(kubeconfig);

        var current = Assert.Single(state.Snapshot().ImportedContexts);
        Assert.NotEqual(firstHash, current.SourceContentHash);
        Assert.Equal("2026-06-12T08:05:00.0000000Z", current.ImportedAt);
        Assert.Single(state.Snapshot().Sessions);
        Assert.Single(Directory.EnumerateFiles(Path.Combine(directory, "kubeconfigs"), "*.yaml"));
    }

    [Fact]
    public void App_state_reimport_with_whitespace_only_yaml_changes_keeps_context_identity()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        var state = AppState.InMemoryWithConfigDirectory(directory, Clock);
        File.WriteAllText(kubeconfig, OneContextKubeconfig("dev", "http://127.0.0.1:1") + "\n");
        state.ImportKubeconfig(kubeconfig);
        var first = Assert.Single(state.Snapshot().ImportedContexts);

        File.WriteAllText(kubeconfig, OneContextKubeconfig("dev", "http://127.0.0.1:1") + "\n\n");
        state.ImportKubeconfig(kubeconfig);

        var current = Assert.Single(state.Snapshot().ImportedContexts);
        Assert.Equal(first.ContextId, current.ContextId);
        Assert.Single(state.Snapshot().Sessions);
    }

    [Fact]
    public void App_state_load_default_backfills_default_filter_for_legacy_sources()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("dev", "http://127.0.0.1:1"));
        var seed = AppState.InMemoryWithConfigDirectory(directory, Clock);
        seed.ImportKubeconfig(kubeconfig);
        var legacy = seed.Snapshot() with
        {
            ImportedContexts = seed.Snapshot().ImportedContexts
                .Select(context => context with { FilterName = "" })
                .ToList()
        };
        File.WriteAllText(
            Path.Combine(directory, "store.json"),
            JsonSerializer.Serialize(legacy, new JsonSerializerOptions(JsonSerializerDefaults.Web)));
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);

        try
        {
            var loaded = AppState.LoadDefault(Clock);

            Assert.All(loaded.Snapshot().ImportedContexts, context => Assert.Equal("default", context.FilterName));
            Assert.Contains("\"filterName\": \"default\"", File.ReadAllText(Path.Combine(directory, "store.json")), StringComparison.Ordinal);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void App_state_imports_generated_k3d_source_without_trying_to_file_refresh_it()
    {
        var directory = TempDirectory();
        var state = AppState.InMemoryWithConfigDirectory(directory, Clock);

        var summary = state.ImportGeneratedKubeconfigText("k3d/podlord-test", SampleKubeconfig("https://127.0.0.1:6443"));
        var refreshed = state.RefreshImportedKubeconfigs();
        var contexts = state.Snapshot().ImportedContexts
            .Where(item => item.SourcePath.StartsWith("podlord-generated://", StringComparison.Ordinal))
            .ToList();

        Assert.NotEmpty(summary.Contexts);
        Assert.Empty(refreshed);
        Assert.NotEmpty(contexts);
        Assert.Contains(contexts, context => context.Server == "https://127.0.0.1:6443");
        Assert.All(contexts, context => Assert.True(File.Exists(context.OwnedKubeconfigPath)));
    }

    [Fact]
    public void App_state_refresh_keeps_prior_snapshots_when_source_file_changes_shape()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, TwoContextKubeconfig("http://127.0.0.1:1"));
        var state = AppState.InMemoryWithConfigDirectory(directory, Clock);
        state.ImportKubeconfig(kubeconfig);

        File.WriteAllText(kubeconfig, OneContextKubeconfig("only-b", "http://127.0.0.1:2"));
        state.RefreshImportedKubeconfigs();
        var snapshot = state.Snapshot();

        Assert.Contains(snapshot.ImportedContexts, context => context.Name == "only-a");
        Assert.Contains(snapshot.Sessions, session => session.DisplayName == "only-a");
        Assert.Contains(snapshot.ImportedContexts, context => context.Name == "only-b");
        Assert.Contains(snapshot.Sessions, session => session.DisplayName == "only-b");
        Assert.Equal(3, snapshot.ImportedContexts.Count);
        Assert.Equal(3, snapshot.Sessions.Count);
        Assert.Equal(2, snapshot.ImportedContexts.Select(context => context.SourceContentHash).Distinct(StringComparer.Ordinal).Count());
        Assert.Contains(snapshot.Sessions, session => session.Id == snapshot.ActiveSessionId);
    }

    [Fact]
    public void Command_safety_classifies_readonly_mutating_and_production_confirmation()
    {
        Assert.Equal(CommandRiskLevel.ReadOnly, CommandSafety.Classify("kubectl get pods", SafetyLevel.Dev).Level);
        Assert.False(CommandSafety.Classify("kubectl apply -f app.yaml", SafetyLevel.Dev).RequiresConfirmation);
        Assert.True(CommandSafety.Classify("kubectl apply -f app.yaml", SafetyLevel.Production).RequiresConfirmation);
        Assert.Equal(CommandRiskLevel.Critical, CommandSafety.Classify("kubectl delete namespace prod", SafetyLevel.Production).Level);
    }

    [Fact]
    public void Bootstrap_is_resource_first()
    {
        var bootstrap = AppBootstrap.FromStore(AppStore.Empty);

        Assert.Equal(["resources", "sources", "settings"], bootstrap.ShellSections.Select(section => section.Id).ToArray());
        Assert.Equal("resources", bootstrap.Store.Settings.DefaultLandingView);
    }

    [Fact]
    public void Resource_filter_matches_contains_exact_regex_and_multiselect_tokens()
    {
        var rows = SampleRows();

        var exact = ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Kind: "\"Pod\"", Namespace: "\"payments\""));
        var regex = ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Search: "/pay.*api/"));
        var id = ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Id: "2"));
        var starts = ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Name: "~front"));
        var ends = ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Image: "5.0~"));
        var multi = ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Status: "\"CrashLoopBackOff\" \"Unavailable\""));

        Assert.Single(exact);
        Assert.Equal("payment-api-7d9", exact[0].Name);
        Assert.Single(regex);
        Assert.Equal("payment-api-7d9", regex[0].Name);
        Assert.Single(id);
        Assert.Equal("Deployment", id[0].Kind);
        Assert.Single(starts);
        Assert.Equal("frontend", starts[0].Name);
        Assert.Single(ends);
        Assert.Equal("frontend", ends[0].Name);
        Assert.Equal(["Deployment", "Pod"], multi.Select(row => row.Kind).Order().ToArray());
    }

    [Fact]
    public void Resource_filter_matches_number_ranges_problems_and_limit()
    {
        var rows = SampleRows();

        var restarted = ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Restarts: ">2"));
        var equals = ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Restarts: "4"));
        var lowerEquals = ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Restarts: "=<0"));
        var greaterEquals = ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Restarts: "=>4"));
        var healthyLimit = ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(ProblemsOnly: false, Limit: 1));
        var problems = ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(ProblemsOnly: true));
        var active = ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(ActivityOnly: true));

        Assert.Single(restarted);
        Assert.Equal("payment-api-7d9", restarted[0].Name);
        Assert.Single(equals);
        Assert.Equal("payment-api-7d9", equals[0].Name);
        Assert.Equal(2, lowerEquals.Count);
        Assert.Single(greaterEquals);
        Assert.Equal("Restarted 4", ResourceFilterMatcher.ProblemReason(restarted[0]));
        Assert.Single(healthyLimit);
        Assert.Equal(2, problems.Count);
        Assert.DoesNotContain(problems, row => row.Name == "frontend");
        Assert.Contains(active, row => row.Name == "payment-api-7d9");
    }

    [Fact]
    public void Age_filter_expressions_parse_directly_and_fallback_to_text_without_stale_state()
    {
        var rows = new[]
        {
            HealthyRow("fresh") with { Age = "1m" },
            HealthyRow("old") with { Age = "12m" },
            HealthyRow("stale") with { Age = "2h" },
            HealthyRow("raw") with { Age = "weird" }
        };

        var older = ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Age: ">5m"));
        var newest = ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Age: "<2m"));
        var exact = ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Age: "=1m"));
        var exactRaw = ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Age: "\"weird\""));
        var broken = ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Age: "1x"));

        Assert.Equal(["old", "stale"], older.Select(row => row.Name).ToArray());
        Assert.Single(newest);
        Assert.Equal("fresh", newest[0].Name);
        Assert.Single(exact);
        Assert.Equal("fresh", exact[0].Name);
        Assert.Single(exactRaw);
        Assert.Equal("raw", exactRaw[0].Name);
        Assert.Empty(broken);
        Assert.Equal(["old", "stale"], ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Age: ">5m")).Select(row => row.Name).ToArray());
    }

    [Fact]
    public void Resource_problem_filter_does_not_treat_stale_healthy_rows_as_workload_issues()
    {
        var healthyStale = new FlatResourceRow(
            "stale",
            "Running",
            "Pod",
            "scheduler",
            "kube-system",
            "dev",
            "1d",
            "1/1",
            0,
            "node-a",
            "scheduler:v1",
            null,
            "1m",
            FreshnessState.Stale);

        var problems = ResourceFilterMatcher.FilterRows([healthyStale], new ResourceQuery(ProblemsOnly: true));

        Assert.Empty(problems);
    }

    [Fact]
    public void Running_row_with_low_restart_count_is_not_flagged_as_problem()
    {
        var calm = HealthyRow("calm") with { Restarts = 2 };
        var noisy = HealthyRow("noisy") with { Restarts = 50 };
        var rows = Enumerable.Range(0, 30).Select(i => HealthyRow($"pod-{i}")).Concat([calm, noisy]).ToList();

        var threshold = ResourceFilterMatcher.RestartOutlierThreshold(rows);

        Assert.Equal(string.Empty, ResourceFilterMatcher.ProblemReason(calm, threshold));
        Assert.Equal("Restarted 50", ResourceFilterMatcher.ProblemReason(noisy, threshold));
    }

    [Fact]
    public void Non_running_row_keeps_restart_flag_regardless_of_threshold()
    {
        var pending = HealthyRow("pending") with { Status = "Pending", Restarts = 1 };

        Assert.Equal("Restarted 1", ResourceFilterMatcher.ProblemReason(pending, restartOutlierThreshold: 100));
    }

    [Fact]
    public void Restart_outlier_threshold_uses_default_floor_for_small_samples()
    {
        var rows = new[] { HealthyRow("a"), HealthyRow("b") with { Restarts = 1 } };

        Assert.Equal(ResourceFilterMatcher.DefaultRestartOutlierThreshold, ResourceFilterMatcher.RestartOutlierThreshold(rows));
    }

    [Fact]
    public void Resource_health_bar_uses_proportional_segments_without_warning_drama()
    {
        var rows = Enumerable.Range(0, 98)
            .Select(index => HealthyRow($"pod-{index}"))
            .Concat([WarningRow("warm-1"), WarningRow("warm-2")])
            .ToList();

        var summary = ResourceHealthCalculator.Calculate(rows, infrastructureWarnings: 0);

        Assert.Equal(98, summary.Healthy);
        Assert.Equal(2, summary.Warning);
        Assert.Equal(0, summary.Critical);
        Assert.Equal(100, summary.Total);
        Assert.Equal(98, summary.Segments.Single(segment => segment.State == "HEALTHY").Percent);
        Assert.Equal(2, summary.Segments.Single(segment => segment.State == "WARNING").Percent);
    }

    [Fact]
    public void Resource_health_bar_counts_api_warnings_but_does_not_hide_critical_resources()
    {
        var rows = new[]
        {
            HealthyRow("healthy"),
            WarningRow("warming"),
            CriticalRow("broken")
        };

        var summary = ResourceHealthCalculator.Calculate(rows, infrastructureWarnings: 2);

        Assert.Equal(1, summary.Healthy);
        Assert.Equal(3, summary.Warning);
        Assert.Equal(1, summary.Critical);
        Assert.Equal(5, summary.Total);
        Assert.Equal(20, summary.Segments.Single(segment => segment.State == "HEALTHY").Percent);
        Assert.Equal(60, summary.Segments.Single(segment => segment.State == "WARNING").Percent);
        Assert.Equal(20, summary.Segments.Single(segment => segment.State == "CRITICAL").Percent);
    }

    [Fact]
    public void Resource_health_bar_reports_unknown_when_cache_is_empty()
    {
        var summary = ResourceHealthCalculator.Calculate([], infrastructureWarnings: 0);

        Assert.Equal(0, summary.Total);
        Assert.Equal(1, summary.Unknown);
        var segment = Assert.Single(summary.Segments);
        Assert.Equal("UNKNOWN", segment.State);
        Assert.Equal(100, segment.Percent);
    }

    [Fact]
    public void Resource_health_bar_counts_api_warnings_before_cache_has_rows()
    {
        var summary = ResourceHealthCalculator.Calculate([], infrastructureWarnings: 2);

        Assert.Equal(0, summary.Healthy);
        Assert.Equal(2, summary.Warning);
        Assert.Equal(0, summary.Critical);
        Assert.Equal(2, summary.Total);
        var segment = Assert.Single(summary.Segments);
        Assert.Equal("WARNING", segment.State);
        Assert.Equal(100, segment.Percent);
    }

    public static string SampleKubeconfig(string server)
    {
        return $$"""
apiVersion: v1
current-context: missing
clusters:
- name: dev
  cluster:
    server: {{server}}
- name: ghosted
  cluster:
    server: {{server}}
contexts:
- name: dev
  context:
    cluster: dev
    user: dev
    namespace: payments
- name: dev
  context:
    cluster: ghosted
    user: dev
- name: exec-dev
  context:
    cluster: dev
    user: exec-user
- name: broken
  context:
    cluster: ghost
    user: nobody
users:
- name: dev
  user:
    token: secret-token
- name: exec-user
  user:
    exec:
      apiVersion: client.authentication.k8s.io/v1
      command: oidc-login
""";
    }

    private static string TwoContextKubeconfig(string server)
    {
        return $$"""
apiVersion: v1
clusters:
- name: only-a
  cluster:
    server: {{server}}
- name: only-b
  cluster:
    server: {{server}}
contexts:
- name: only-a
  context:
    cluster: only-a
    user: dev
- name: only-b
  context:
    cluster: only-b
    user: dev
users:
- name: dev
  user:
    token: secret-token
""";
    }

    private static string OneContextKubeconfig(string name, string server)
    {
        return $$"""
apiVersion: v1
clusters:
- name: {{name}}
  cluster:
    server: {{server}}
contexts:
- name: {{name}}
  context:
    cluster: {{name}}
    user: dev
users:
- name: dev
  user:
    token: secret-token
""";
    }

    private static string TempDirectory()
    {
        var path = Path.Combine(Path.GetTempPath(), $"podlord-test-{Guid.NewGuid():N}");
        Directory.CreateDirectory(path);
        return path;
    }

    private static IReadOnlyList<FlatResourceRow> SampleRows()
    {
        return
        [
            new FlatResourceRow(
                "1",
                "CrashLoopBackOff",
                "Pod",
                "payment-api-7d9",
                "payments",
                "prod",
                "2m",
                "0/1",
                4,
                "node-a",
                "payment-api:1.2.3",
                "ReplicaSet/payment-api-7d9",
                "1m",
                FreshnessState.Fresh),
            new FlatResourceRow(
                "2",
                "Available",
                "Deployment",
                "frontend",
                "frontend",
                "prod",
                "1h",
                "3/3",
                0,
                null,
                "frontend:5.0",
                null,
                "30m",
                FreshnessState.Fresh),
            new FlatResourceRow(
                "3",
                "Unavailable",
                "Deployment",
                "orders",
                "orders",
                "prod",
                "5m",
                "1/3",
                0,
                null,
                "orders:9.0",
                null,
                "2m",
                FreshnessState.Fresh)
        ];
    }

    [Fact]
    public void Alert_rule_catalog_has_locked_defaults_for_old_radar_behavior()
    {
        Assert.Equal(
            ["default-problem-color", "default-recent-change-color", "default-active-view-pulse"],
            AlertRuleCatalog.DefaultRules.Select(rule => rule.Id).ToArray());
        Assert.Contains(AlertRuleCatalog.DefaultRules, rule => rule.Id == "default-problem-color" && rule.BuiltIn && !rule.CanEdit);
        Assert.Contains(AlertRuleCatalog.DefaultRules, rule => rule.Id == "default-problem-color"
            && rule.Actions.RadarColorValue == "status"
            && !rule.Actions.RadarBlink
            && rule.Actions.RadarZoom
            && rule.Actions.RadarFocus
            && rule.Actions.RadarZoomPercent == 100
            && rule.Actions.PlaySound
            && rule.Actions.SoundMinimumMatches == 1
            && rule.SoundId == "warning-ping");
        Assert.Contains(AlertRuleCatalog.DefaultRules, rule => rule.Id == "default-recent-change-color" && rule.Actions.RadarColorValue == "fresh" && !rule.Actions.RadarBlink);
        Assert.Contains(AlertRuleCatalog.DefaultRules, rule => rule.Id == "default-active-view-pulse"
            && rule.Actions.RadarBlink
            && rule.Actions.RadarAnimation == "pulse"
            && rule.Actions.RadarAnimationUntilMode == AlertUntilModes.NewInView
            && rule.Actions.RadarAnimationUntilDuration == "5s"
            && rule.MatcherGroups?.Single().Criteria.Select(criterion => criterion.Field).Order(StringComparer.Ordinal).ToArray() is ["Active", "New in view"]);
        Assert.All(AlertRuleCatalog.DefaultRules.Where(rule => rule.Id != "default-problem-color"), rule => Assert.Equal("none", rule.SoundId));
        Assert.All(AlertRuleCatalog.DefaultRules, rule => Assert.True(rule.Enabled));
        var groupIds = AlertRuleCatalog.DefaultRules.SelectMany(rule => rule.MatcherGroups ?? []).Select(group => group.Id).ToArray();
        var criterionIds = AlertRuleCatalog.DefaultRules.SelectMany(rule => rule.MatcherGroups ?? []).SelectMany(group => group.Criteria).Select(criterion => criterion.Id).ToArray();
        Assert.Equal(groupIds.Length, groupIds.Distinct(StringComparer.Ordinal).Count());
        Assert.Equal(criterionIds.Length, criterionIds.Distinct(StringComparer.Ordinal).Count());
        Assert.All(groupIds, id => Assert.StartsWith("default-", id, StringComparison.Ordinal));
        Assert.All(criterionIds, id => Assert.StartsWith("default-", id, StringComparison.Ordinal));
    }

    [Fact]
    public void Alert_rule_evaluator_reuses_filter_matchers_and_respects_disabled_rules()
    {
        var rows = SampleRows();
        var rule = new AlertRule(
            "custom-pod",
            "Pod restarts",
            "Pod restart alert",
            true,
            false,
            "warning",
            new AlertRuleMatchers(Kind: "\"Pod\"", Restarts: ">1", Namespace: "\"payments\""),
            new AlertRuleActions(),
            new AlertRuleUntil());

        var active = AlertRuleEvaluator.EvaluateRule(rows, rule);
        var disabled = AlertRuleEvaluator.EvaluateRule(rows, rule with { Enabled = false });

        Assert.True(active.Triggered);
        Assert.Equal(["payment-api-7d9"], active.Matches.Select(row => row.Name).ToArray());
        Assert.False(disabled.Triggered);
        Assert.Empty(disabled.Matches);
    }

    [Fact]
    public void Alert_rule_evaluator_supports_active_alias_for_activity_matcher()
    {
        var rows = new[]
        {
            HealthyRow("old") with { Status = "Succeeded", Age = "2h", LastChange = "2h" },
            HealthyRow("fresh") with { Status = "Succeeded", Age = "20s", LastChange = "20s" },
            HealthyRow("rollout") with { Status = "Progressing", Age = "2h", LastChange = "2h" }
        };
        var activeRule = new AlertRule(
            "active",
            "Active rows",
            "",
            true,
            false,
            "",
            new AlertRuleMatchers(),
            new AlertRuleActions(),
            new AlertRuleUntil(),
            MatcherGroups: [new AlertMatcherGroup("active", [new AlertMatcherCriterion("active", "Active", "true")])]);

        var result = AlertRuleEvaluator.EvaluateRule(rows, activeRule);

        Assert.Equal(["fresh", "rollout"], result.Matches.Select(row => row.Name).Order(StringComparer.Ordinal).ToArray());
    }

    [Fact]
    public void Alert_rule_evaluator_supports_old_radar_virtual_matchers()
    {
        var rows = new[]
        {
            new FlatResourceRow(
                "recent",
                "Running",
                "Pod",
                "recent",
                "payments",
                "prod",
                "2h",
                "1/1",
                0,
                null,
                "api:1",
                null,
                "now",
                FreshnessState.Fresh),
            new FlatResourceRow(
                "error",
                "CrashLoopBackOff",
                "Pod",
                "broken",
                "payments",
                "prod",
                "2h",
                "0/1",
                1,
                null,
                "api:1",
                null,
                "2h",
                FreshnessState.Fresh)
        };
        var recent = new AlertRule(
            "recent",
            "Recent",
            "",
            true,
            false,
            "",
            new AlertRuleMatchers(),
            new AlertRuleActions(),
            new AlertRuleUntil(),
            MatcherGroups: [new AlertMatcherGroup("recent", [new AlertMatcherCriterion("recent", "Recently changed", "true")])]);
        var error = recent with
        {
            Id = "error",
            MatcherGroups = [new AlertMatcherGroup("error", [new AlertMatcherCriterion("error", "Error", "true")])]
        };
        var newInView = recent with
        {
            Id = "new-in-view",
            MatcherGroups = [new AlertMatcherGroup("new-in-view", [new AlertMatcherCriterion("new-in-view", "New in view", "true")])]
        };

        Assert.Equal(["recent"], AlertRuleEvaluator.EvaluateRule(rows, recent).Matches.Select(row => row.Name).ToArray());
        Assert.Equal(["broken"], AlertRuleEvaluator.EvaluateRule(rows, error).Matches.Select(row => row.Name).ToArray());
        Assert.Equal(["recent", "broken"], AlertRuleEvaluator.EvaluateRule(rows, newInView).Matches.Select(row => row.Name).ToArray());
    }

    [Fact]
    public void Alert_rule_evaluator_supports_outlier_and_p95_matchers()
    {
        var rows = new[]
        {
            HealthyRow("quiet-a") with { Restarts = 0, Age = "1m", Pulse = ResourcePulse.Empty with { CpuMillicores = 20 } },
            HealthyRow("quiet-b") with { Restarts = 1, Age = "2m", Pulse = ResourcePulse.Empty with { CpuMillicores = 30 } },
            HealthyRow("quiet-c") with { Restarts = 2, Age = "3m", Pulse = ResourcePulse.Empty with { CpuMillicores = 40 } },
            HealthyRow("noisy") with { Restarts = 80, Age = "2h", Pulse = ResourcePulse.Empty with { CpuMillicores = 900 } }
        };
        var restartRule = new AlertRule(
            "restart-outlier",
            "Restart outlier",
            "",
            true,
            false,
            "warning",
            new AlertRuleMatchers(Restarts: "outlier"),
            new AlertRuleActions(),
            new AlertRuleUntil());
        var cpuRule = restartRule with { Id = "cpu-p95", Matchers = new AlertRuleMatchers(Cpu: "p95") };
        var ageRule = restartRule with { Id = "age-p95", Matchers = new AlertRuleMatchers(Age: "p95") };

        Assert.Equal(["noisy"], AlertRuleEvaluator.EvaluateRule(rows, restartRule).Matches.Select(row => row.Name).ToArray());
        Assert.Equal(["noisy"], AlertRuleEvaluator.EvaluateRule(rows, cpuRule).Matches.Select(row => row.Name).ToArray());
        Assert.Equal(["noisy"], AlertRuleEvaluator.EvaluateRule(rows, ageRule).Matches.Select(row => row.Name).ToArray());
    }

    [Fact]
    public void Alert_rule_evaluator_supports_or_groups_with_and_criteria()
    {
        var rows = new[]
        {
            HealthyRow("api-a") with { Namespace = "payments", Restarts = 0, Pulse = ResourcePulse.Empty with { CpuMillicores = 50 } },
            HealthyRow("api-b") with { Namespace = "payments", Restarts = 4, Pulse = ResourcePulse.Empty with { CpuMillicores = 90 } },
            HealthyRow("worker-a") with { Namespace = "jobs", Restarts = 0, Pulse = ResourcePulse.Empty with { CpuMillicores = 900 } },
            HealthyRow("worker-b") with { Namespace = "jobs", Restarts = 0, Pulse = ResourcePulse.Empty with { CpuMillicores = 40 } }
        };
        var rule = new AlertRule(
            "custom-groups",
            "Grouped alert",
            "",
            true,
            false,
            "",
            new AlertRuleMatchers(Kind: "\"NeverUsed\""),
            new AlertRuleActions(),
            new AlertRuleUntil(),
            MatcherGroups:
            [
                new AlertMatcherGroup("restart-payments", [
                    new AlertMatcherCriterion("restart-kind", "Kind", "\"Pod\""),
                    new AlertMatcherCriterion("restart-ns", "Namespace", "\"payments\""),
                    new AlertMatcherCriterion("restart-count", "Restarts", ">1")
                ]),
                new AlertMatcherGroup("cpu-jobs", [
                    new AlertMatcherCriterion("cpu-ns", "Namespace", "\"jobs\""),
                    new AlertMatcherCriterion("cpu-value", "CPU", ">500m")
                ])
            ]);

        var result = AlertRuleEvaluator.EvaluateRule(rows, rule);

        Assert.True(result.Triggered);
        Assert.Equal(["api-b", "worker-a"], result.Matches.Select(row => row.Name).ToArray());
    }

    [Fact]
    public void Alert_rule_evaluator_supports_false_boolean_virtual_matchers()
    {
        var rows = new[]
        {
            HealthyRow("old") with { Status = "Succeeded", Age = "2h", LastChange = "2h" },
            HealthyRow("fresh") with { Status = "Running", Age = "15s", LastChange = "15s" },
            HealthyRow("broken") with { Status = "CrashLoopBackOff", Ready = "0/1", Restarts = 10, LastChange = "2h" }
        };
        var rule = new AlertRule(
            "quiet",
            "Quiet rows",
            "",
            true,
            false,
            "",
            new AlertRuleMatchers(),
            new AlertRuleActions(),
            new AlertRuleUntil(),
            MatcherGroups:
            [
                new AlertMatcherGroup("quiet", [
                    new AlertMatcherCriterion("no-problem", "Problems", "false"),
                    new AlertMatcherCriterion("no-error", "Errors", "no"),
                    new AlertMatcherCriterion("not-recent", "Recently changed", "0"),
                    new AlertMatcherCriterion("not-active", "Active", "false")
                ])
            ]);

        var result = AlertRuleEvaluator.EvaluateRule(rows, rule);

        Assert.True(result.Triggered);
        Assert.Equal(["old"], result.Matches.Select(row => row.Name).ToArray());
    }

    [Fact]
    public void Alert_rule_evaluator_supports_unknown_field_fallbacks()
    {
        var row = HealthyRow("api") with
        {
            LastChange = "12s",
            EventReason = "BackOff",
            EventMessage = "container restarted"
        };
        var rows = new[] { row };
        var rule = new AlertRule(
            "fallbacks",
            "Fallback fields",
            "",
            true,
            false,
            "",
            new AlertRuleMatchers(),
            new AlertRuleActions(),
            new AlertRuleUntil(),
            MatcherGroups:
            [
                new AlertMatcherGroup("fallbacks", [
                    new AlertMatcherCriterion("last-change", "Last Change", "12s"),
                    new AlertMatcherCriterion("event-reason", "Event Reason", "\"BackOff\""),
                    new AlertMatcherCriterion("event-message", "Event Message", "restarted"),
                    new AlertMatcherCriterion("missing", "Not a real field", "")
                ]),
                new AlertMatcherGroup("owner", [
                    new AlertMatcherCriterion("owner", "Owner", "")
                ])
            ]);

        var result = AlertRuleEvaluator.EvaluateRule(rows, rule);

        Assert.True(result.Triggered);
        Assert.Equal(["api"], result.Matches.Select(item => item.Name).ToArray());
    }

    [Fact]
    public void Alert_rule_evaluator_handles_missing_special_stat_values()
    {
        var rows = new[]
        {
            HealthyRow("without-age") with { Age = "unknown" },
            HealthyRow("without-metrics") with { Pulse = ResourcePulse.Empty },
            HealthyRow("with-metrics") with
            {
                Age = "10m",
                Pulse = ResourcePulse.Empty with
                {
                    MemoryBytes = 512 * 1024 * 1024,
                    MemoryLimitBytes = 1024 * 1024 * 1024,
                    StorageUsedBytes = 5L * 1024 * 1024 * 1024,
                    StorageLimitBytes = 10L * 1024 * 1024 * 1024
                }
            }
        };
        var age = RuleWithGroup("age", "Age", "p95");
        var memory = RuleWithGroup("memory", "Memory", "p95");
        var storage = RuleWithGroup("storage", "Storage", "outlier");

        Assert.Equal(["with-metrics"], AlertRuleEvaluator.EvaluateRule(rows, age).Matches.Select(row => row.Name).ToArray());
        Assert.Equal(["with-metrics"], AlertRuleEvaluator.EvaluateRule(rows, memory).Matches.Select(row => row.Name).ToArray());
        Assert.Equal(["with-metrics"], AlertRuleEvaluator.EvaluateRule(rows, storage).Matches.Select(row => row.Name).ToArray());
    }

    [Fact]
    public void Alert_rule_evaluator_treats_blank_groups_as_legacy_matchers()
    {
        var rows = new[]
        {
            HealthyRow("pod") with { Kind = "Pod" },
            HealthyRow("deployment") with { Kind = "Deployment" }
        };
        var rule = new AlertRule(
            "legacy",
            "Legacy fallback",
            "",
            true,
            false,
            "",
            new AlertRuleMatchers(Kind: "\"Pod\""),
            new AlertRuleActions(),
            new AlertRuleUntil(),
            MatcherGroups:
            [
                new AlertMatcherGroup("blank", [
                    new AlertMatcherCriterion("blank-field", "", "Pod"),
                    new AlertMatcherCriterion("blank-expression", "Kind", "")
                ])
            ]);

        var result = AlertRuleEvaluator.EvaluateRule(rows, rule);

        Assert.Equal(["pod"], result.Matches.Select(row => row.Name).ToArray());
    }

    private static AlertRule RuleWithGroup(string id, string field, string expression)
    {
        return new AlertRule(
            id,
            id,
            "",
            true,
            false,
            "",
            new AlertRuleMatchers(),
            new AlertRuleActions(),
            new AlertRuleUntil(),
            MatcherGroups: [new AlertMatcherGroup(id, [new AlertMatcherCriterion(id, field, expression)])]);
    }

    private static FlatResourceRow HealthyRow(string name)
    {
        return new FlatResourceRow(
            name,
            "Running",
            "Pod",
            name,
            "default",
            "test",
            "1m",
            "1/1",
            0,
            "node-a",
            "app:1",
            null,
            "now",
            FreshnessState.Fresh);
    }

    private static FlatResourceRow WarningRow(string name)
    {
        return HealthyRow(name) with { Ready = "1/2" };
    }

    private static FlatResourceRow CriticalRow(string name)
    {
        return HealthyRow(name) with { Status = "CrashLoopBackOff", Ready = "0/1" };
    }

    private sealed class FixedClock(DateTimeOffset now) : IPodlordClock
    {
        public DateTimeOffset Now { get; } = now;
    }

    private sealed class MutableClock(DateTimeOffset now) : IPodlordClock
    {
        public DateTimeOffset Now { get; set; } = now;
    }
}
