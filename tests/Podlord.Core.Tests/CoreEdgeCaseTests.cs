using Podlord.Core;

namespace Podlord.Core.Tests;

public sealed class CoreEdgeCaseTests
{
    [Fact]
    public void Namespace_scope_trims_deduplicates_and_labels_scopes()
    {
        Assert.Same(NamespaceScope.All, NamespaceScope.One(" "));
        Assert.Same(NamespaceScope.All, NamespaceScope.Many([" ", ""]));

        var one = NamespaceScope.Many([" payments ", "", "payments"]);
        var many = NamespaceScope.Many(["payments", "orders"]);
        var unknown = new NamespaceScope((NamespaceScopeKind)99, []);

        Assert.Equal(NamespaceScopeKind.One, one.Kind);
        Assert.Equal("payments", one.Label);
        Assert.Equal(NamespaceScopeKind.Many, many.Kind);
        Assert.Equal("payments,orders", many.Label);
        Assert.Equal("all sectors", unknown.Label);
    }

    [Fact]
    public void Command_safety_covers_empty_mutating_readonly_none_and_protected_contexts()
    {
        Assert.Equal(CommandRiskLevel.None, CommandSafety.Classify(" ", SafetyLevel.Dev).Level);
        Assert.Equal(CommandRiskLevel.None, CommandSafety.Classify("echo hello", SafetyLevel.Dev).Level);
        Assert.Equal(CommandRiskLevel.ReadOnly, CommandSafety.Classify("kubectl describe pod api", SafetyLevel.Dev).Level);
        var mutating = CommandSafety.Classify("kubectl create configmap app", SafetyLevel.Staging);

        Assert.Equal(CommandRiskLevel.Mutating, mutating.Level);
        Assert.True(mutating.RequiresConfirmation);
        Assert.Contains("create", mutating.MatchedTerms);
    }

    [Fact]
    public void Resource_filter_handles_invalid_regex_escaped_quotes_prefix_suffix_and_number_operators()
    {
        var rows = new[]
        {
            Row("Running", "api \"blue\"", 0, "1/1"),
            Row("Running", "worker-green", 2, "1/1"),
            Row("Pending", "job-red", 5, "0/1")
        };

        Assert.Empty(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Name: "/[/")));
        Assert.Single(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Name: "\"api \\\"blue\\\"\"")));
        Assert.Single(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Name: "~worker")));
        Assert.Single(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Name: "red~")));
        Assert.Equal(2, ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Restarts: "<5")).Count);
        Assert.Equal(2, ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Restarts: "<=2")).Count);
        Assert.Single(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Restarts: ">=5")));
        Assert.Single(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Restarts: ">2")));
        Assert.Equal(256, ResourceFilterMatcher.NormalizeLimit(0));
        Assert.Equal(5_000, ResourceFilterMatcher.NormalizeLimit(50_000));
    }

    [Fact]
    public void Resource_filter_parses_age_duration_comparisons_and_activity()
    {
        var rows = new[]
        {
            Row("Succeeded", "fresh-job", 0, "1/1") with { Age = "45s", LastChange = "45s" },
            Row("Succeeded", "middle-job", 0, "1/1") with { Age = "30m", LastChange = "30m" },
            Row("Succeeded", "old-job", 0, "1/1") with { Age = "6d7h", LastChange = "6d7h" }
        };

        Assert.Single(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Age: "<=1m")));
        Assert.Equal("middle-job", Assert.Single(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Age: ">5m <=1h"))).Name);
        Assert.Equal("old-job", Assert.Single(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Age: ">5d"))).Name);
        Assert.Equal("fresh-job", Assert.Single(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(ActivityOnly: true))).Name);
        Assert.Equal(TimeSpan.FromDays(6) + TimeSpan.FromHours(7), ResourceFilterMatcher.ParseHumanDuration("6d7h"));
    }

    [Fact]
    public void Resource_filter_covers_age_grammar_text_fallbacks_and_activity_windows()
    {
        var rows = new[]
        {
            Row("Succeeded", "instant", 0, "1/1") with { Age = "now", LastChange = "2h" },
            Row("Succeeded", "seconds", 0, "1/1") with { Age = "5secs", LastChange = "30m" },
            Row("Succeeded", "minutes", 0, "1/1") with { Age = "7mins", LastChange = "45m" },
            Row("Succeeded", "hours", 0, "1/1") with { Age = "2hrs", LastChange = "2hrs" },
            Row("Succeeded", "days", 0, "1/1") with { Age = "3days", LastChange = "3days" },
            Row("Succeeded", "weeks", 0, "1/1") with { Age = "2weeks", LastChange = "2weeks" },
            Row("Succeeded", "text-age", 0, "1/1") with { Age = "unknown-age", LastChange = "stale" },
            Row("Succeeded", "event-row", 0, "-") with { Kind = "Event", Age = "90days", LastChange = "90days" },
            Row("Progressing", "rollout", 0, "1/1") with { Age = "90days", LastChange = "90days" }
        };

        Assert.Equal(["instant", "seconds"], Names(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Age: "5s"))));
        Assert.Equal(["seconds"], Names(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Age: "=5s"))));
        Assert.Equal(["seconds"], Names(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Age: ">=5s <=5s"))));
        Assert.Equal(["seconds"], Names(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Age: "=>5s =<5s"))));
        Assert.Contains(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Age: "<3h")), row => row.Name == "hours");
        Assert.Contains(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Age: ">1w")), row => row.Name == "weeks");
        Assert.Equal("text-age", Assert.Single(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Age: "unknown"))).Name);
        Assert.Equal("seconds", Assert.Single(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Age: "secs"))).Name);
        Assert.Equal(["instant", "seconds", "minutes", "rollout"], Names(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(ActivityOnly: true))));
    }

    [Fact]
    public void Resource_filter_matches_cpu_memory_and_storage_quantity_ranges()
    {
        var rows = new[]
        {
            Row("Running", "small", 0, "1/1") with
            {
                Pulse = ResourcePulse.Empty with
                {
                    CpuMillicores = 125,
                    CpuLimitMillicores = 500,
                    MemoryBytes = 128L * 1024 * 1024,
                    MemoryLimitBytes = 512L * 1024 * 1024,
                    StorageLimitBytes = 1L * 1024 * 1024 * 1024
                }
            },
            Row("Running", "large", 0, "1/1") with
            {
                Pulse = ResourcePulse.Empty with
                {
                    CpuMillicores = 1500,
                    MemoryBytes = 2L * 1024 * 1024 * 1024,
                    StorageUsedBytes = 7L * 1024 * 1024 * 1024,
                    StorageLimitBytes = 10L * 1024 * 1024 * 1024
                }
            },
            Row("Running", "unknown", 0, "1/1")
        };

        Assert.Equal(["small"], Names(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Cpu: ">=100m <1c"))));
        Assert.Equal(["large"], Names(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Cpu: ">1core"))));
        Assert.Equal(["large"], Names(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Memory: ">1Gi"))));
        Assert.Equal(["small"], Names(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Memory: ">=128Mi <200Mi"))));
        Assert.Equal(["large"], Names(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Storage: ">5Gi"))));
        Assert.Equal(["small"], Names(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Storage: "1Gi"))));
        Assert.Empty(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Storage: ">20Gi")));
    }

    [Fact]
    public void Event_activity_and_problem_state_uses_event_ttls()
    {
        var rows = new[]
        {
            Row("Warning", "recent-warning", 0, "-") with { Kind = "Event", LastChange = "10m", Age = "10m", EventReason = "BackOff" },
            Row("Warning", "old-warning", 0, "-") with { Kind = "Event", LastChange = "31m", Age = "31m", EventReason = "BackOff" },
            Row("Normal", "recent-normal", 0, "-") with { Kind = "Event", LastChange = "4m", Age = "4m", EventReason = "Pulled" },
            Row("Normal", "old-normal", 0, "-") with { Kind = "Event", LastChange = "6m", Age = "6m", EventReason = "Pulled" },
            Row("Succeeded", "recent-resolved", 0, "-") with { Kind = "Event", LastChange = "5m", Age = "5m", EventReason = "Recovered" },
            Row("Succeeded", "old-resolved", 0, "-") with { Kind = "Event", LastChange = "6m", Age = "6m", EventReason = "Recovered" },
            Row("Observed", "historical", 0, "-") with { Kind = "Event", LastChange = "now", Age = "now", EventReason = "OldNews" }
        };

        Assert.Equal(["recent-warning", "recent-normal", "recent-resolved"], Names(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(ActivityOnly: true))));
        Assert.Equal(["recent-warning"], Names(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(ProblemsOnly: true))));
        Assert.Equal("BackOff", ResourceFilterMatcher.ProblemReason(rows[0]));
        Assert.Equal(string.Empty, ResourceFilterMatcher.ProblemReason(rows[1]));
        Assert.Equal(string.Empty, ResourceFilterMatcher.ProblemReason(rows[4]));
    }

    [Theory]
    [InlineData(null, null)]
    [InlineData("", null)]
    [InlineData("   ", null)]
    [InlineData("now", 0)]
    [InlineData("7", 7_000)]
    [InlineData("15ms", 15)]
    [InlineData("1sec", 1_000)]
    [InlineData("2m 3s", 123_000)]
    [InlineData("2hr", 7_200_000)]
    [InlineData("1day 2h", 93_600_000)]
    [InlineData("1week", 604_800_000)]
    [InlineData("abc", null)]
    [InlineData("1fortnight", null)]
    [InlineData("1s trailing", 1_000)]
    public void Human_duration_parser_handles_known_units_invalid_input_and_trailing_text(string? input, int? expectedMilliseconds)
    {
        var parsed = ResourceFilterMatcher.ParseHumanDuration(input);

        if (expectedMilliseconds is null)
        {
            Assert.Null(parsed);
            return;
        }

        Assert.Equal(TimeSpan.FromMilliseconds(expectedMilliseconds.Value), parsed);
    }

    [Fact]
    public void Resource_filter_covers_text_number_search_exact_terms_and_problem_statuses()
    {
        var rows = new[]
        {
            Row("Running", "api-blue", 0, "1/1") with { Id = "cluster:ns:pod:api-blue", Namespace = "payments", Cluster = "prod", Node = "node-a", ImageSummary = "ghcr.io/acme/api:1", Owner = "deploy/api", LastChange = "20m" },
            Row("OOMKilled", "api-red", 0, "1/1") with { Namespace = null, Cluster = "prod", Node = null, ImageSummary = "registry.local/api:2", Owner = null, LastChange = "2h" },
            Row("Warning", "worker", 0, "-") with { Kind = "Event", Namespace = "ops", Cluster = "mgmt", ImageSummary = "", LastChange = "1m" },
            Row("Running", "numeric", 0, "1/1") with { Ready = "12", LastChange = "1h" }
        };

        Assert.True(ResourceFilterMatcher.MatchesText("production", "=production"));
        Assert.False(ResourceFilterMatcher.MatchesText("production", "=prod"));
        Assert.True(ResourceFilterMatcher.MatchesText("production", "/prod.*tion/"));
        Assert.True(ResourceFilterMatcher.MatchesText("production", "~prod"));
        Assert.True(ResourceFilterMatcher.MatchesText("production", "tion~"));
        Assert.True(ResourceFilterMatcher.MatchesText("12", "12"));
        Assert.True(ResourceFilterMatcher.MatchesText("12", ">10"));
        Assert.False(ResourceFilterMatcher.MatchesText("abc", ">10"));
        Assert.True(ResourceFilterMatcher.MatchesNumber(5, "=5"));
        Assert.True(ResourceFilterMatcher.MatchesNumber(5, "=>5"));
        Assert.True(ResourceFilterMatcher.MatchesNumber(5, "=<5"));
        Assert.False(ResourceFilterMatcher.MatchesNumber(5, ">x"));
        Assert.Equal(["literal"], ResourceFilterMatcher.ExactTerms("\"literal\" literal \"\""));
        Assert.Equal(["api-blue"], Names(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Search: "deploy/api"))));
        Assert.Equal(["api-red"], Names(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Namespace: "\"cluster\""))));
        Assert.Equal(["numeric"], Names(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(Ready: ">10"))));
        Assert.Equal(["api-red", "worker"], Names(ResourceFilterMatcher.FilterRows(rows, new ResourceQuery(ProblemsOnly: true))));
        Assert.Equal("OOMKilled", ResourceFilterMatcher.ProblemReason(rows[1]));
        Assert.Equal("Warning", ResourceFilterMatcher.ProblemReason(rows[2]));
    }

    [Fact]
    public void Detail_item_filter_removes_unavailable_optional_rows_but_keeps_known_limits()
    {
        var filtered = DetailItemFilter.Available(
        [
            new DetailItem("Kind", "Secret"),
            new DetailItem("Name", "cloud-config"),
            new DetailItem("CPU", "-"),
            new DetailItem("CPU %", "-"),
            new DetailItem("Memory", "-/256Mi"),
            new DetailItem("Network", "-"),
            new DetailItem("Storage", "-"),
            new DetailItem("Metric source", "API"),
            new DetailItem("Restarts", "0"),
            new DetailItem("Issue", "none"),
            new DetailItem("Image", "metadata only"),
            new DetailItem("UID", "abc")
        ]);

        var labels = filtered.Select(item => item.Label).ToList();

        Assert.DoesNotContain("CPU", labels);
        Assert.DoesNotContain("CPU %", labels);
        Assert.DoesNotContain("Network", labels);
        Assert.DoesNotContain("Storage", labels);
        Assert.DoesNotContain("Metric source", labels);
        Assert.DoesNotContain("Restarts", labels);
        Assert.DoesNotContain("Issue", labels);
        Assert.Contains("Memory", labels);
        Assert.Contains("Image", labels);
        Assert.Contains("UID", labels);
    }

    [Fact]
    public void Resource_pulse_suggests_resource_limits_from_live_usage()
    {
        var pulse = ResourcePulse.Empty with
        {
            CpuMillicores = 125,
            MemoryBytes = 128L * 1024 * 1024,
            SourceBadge = "API LIVE"
        };

        Assert.Equal("160m request / 250m limit", pulse.CpuLimitSuggestion);
        Assert.Equal("160Mi request / 256Mi limit", pulse.MemoryLimitSuggestion);
        Assert.Equal("CPU: 125m\nUsage: -\nSuggestion: 160m request / 250m limit", pulse.CpuMetricDetail);
        Assert.DoesNotContain("LIVE", pulse.CpuMetricDetail, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("API", pulse.CpuMetricDetail, StringComparison.OrdinalIgnoreCase);
        Assert.Equal("-", ResourcePulse.Empty.CpuLimitSuggestion);
        Assert.Equal("-", ResourcePulse.Empty.MemoryLimitSuggestion);
    }

    [Fact]
    public void Resource_pulse_formats_usage_limits_network_storage_and_live_updates()
    {
        var overloaded = new ResourcePulse(
            CpuMillicores: 1_250,
            CpuLimitMillicores: 1_000,
            MemoryBytes: 2L * 1024 * 1024 * 1024,
            MemoryLimitBytes: 1024 * 1024,
            NetworkInBytesPerSecond: null,
            NetworkOutBytesPerSecond: 2 * 1024,
            StorageUsedBytes: 1024L * 1024 * 1024,
            StorageLimitBytes: 2L * 1024 * 1024 * 1024,
            SourceBadge: "api live",
            Tooltip: "sample");

        Assert.Equal("1.3c", overloaded.CpuDisplay);
        Assert.Equal("1c", overloaded.CpuLimitDisplay);
        Assert.Equal("1.3c / 1c", overloaded.CpuSummaryDisplay);
        Assert.Equal("125%", overloaded.CpuPercentDisplay);
        Assert.Equal("125%", overloaded.CpuCompactDisplay);
        Assert.Equal(100, overloaded.CpuPercent);
        Assert.Equal("2Gi", overloaded.MemoryDisplay);
        Assert.Equal("1Mi", overloaded.MemoryLimitDisplay);
        Assert.Equal("2Gi / 1Mi", overloaded.MemorySummaryDisplay);
        Assert.Equal("999%", overloaded.MemoryPercentDisplay);
        Assert.Equal("999%", overloaded.MemoryCompactDisplay);
        Assert.Equal(100, overloaded.MemoryPercent);
        Assert.Equal("↓0B/s ↑2Ki/s", overloaded.NetworkDisplay);
        Assert.Equal("1Gi / 2Gi", overloaded.StorageDisplay);
        Assert.Equal("50%", overloaded.StoragePercentDisplay);
        Assert.Equal("50%", overloaded.StorageCompactDisplay);
        Assert.Equal(50, overloaded.StoragePercent);
        Assert.True(overloaded.HasLiveMetrics);
        Assert.Equal("sample", overloaded.Tooltip);
        Assert.Contains("Suggestion:", overloaded.MemoryMetricDetail, StringComparison.Ordinal);

        var updated = overloaded.WithLiveUsage(cpuMillicores: null, memoryBytes: 512, sourceBadge: "metrics live", tooltip: "fresh");
        Assert.Equal(1_250, updated.CpuMillicores);
        Assert.Equal(512, updated.MemoryBytes);
        Assert.Equal("metrics live", updated.SourceBadge);
        Assert.Equal("fresh", updated.Tooltip);

        var limitsOnly = ResourcePulse.Empty with
        {
            CpuLimitMillicores = 250,
            MemoryLimitBytes = 512 * 1024 * 1024,
            StorageLimitBytes = 10L * 1024 * 1024 * 1024,
            SourceBadge = "API"
        };

        Assert.Equal("-/250m", limitsOnly.CpuSummaryDisplay);
        Assert.Equal("-/512Mi", limitsOnly.MemorySummaryDisplay);
        Assert.Equal("-/10Gi", limitsOnly.StorageDisplay);
        Assert.Equal("-", limitsOnly.CpuCompactDisplay);
        Assert.Equal("-", limitsOnly.MemoryCompactDisplay);
        Assert.Equal("-", limitsOnly.StorageCompactDisplay);
        Assert.False(limitsOnly.HasLiveMetrics);

        var empty = ResourcePulse.Empty;
        Assert.Equal(0, empty.CpuPercent);
        Assert.Equal(0, empty.MemoryPercent);
        Assert.Equal(0, empty.StoragePercent);

        var zeroCpu = ResourcePulse.Empty with
        {
            CpuMillicores = 0,
            CpuLimitMillicores = 100
        };
        Assert.Equal("0m", zeroCpu.CpuDisplay);
        Assert.Equal("0%", zeroCpu.CpuPercentDisplay);
    }

    [Fact]
    public void Flat_resource_row_exposes_metric_and_optional_field_presence_for_ui_tables()
    {
        var empty = Row("Succeeded", "done", 0, "-") with
        {
            Kind = "Job",
            Node = "-",
            ImageSummary = "-",
            Owner = "-",
            Pulse = ResourcePulse.Empty
        };
        var rich = Row("Running", "api", 2, "1/1") with
        {
            Kind = "Pod",
            Node = "node-a",
            ImageSummary = "api:v1",
            Owner = "ReplicaSet/api-123",
            Pulse = new ResourcePulse(25, 100, 64 * 1024 * 1024, 128 * 1024 * 1024, 1, 2, 4, 8, "LIVE", "ok")
        };

        Assert.False(empty.HasCpuMetricInfo);
        Assert.False(empty.HasCpuMetricBar);
        Assert.False(empty.HasCpuMetricTextOnly);
        Assert.False(empty.HasMemoryMetricInfo);
        Assert.False(empty.HasMemoryMetricBar);
        Assert.False(empty.HasMemoryMetricTextOnly);
        Assert.False(empty.HasStorageMetricInfo);
        Assert.False(empty.HasStorageMetricBar);
        Assert.False(empty.HasStorageMetricTextOnly);
        Assert.False(empty.HasNetworkMetricInfo);
        Assert.False(empty.HasReadyInfo);
        Assert.False(empty.HasRestartInfo);
        Assert.False(empty.HasNodeInfo);
        Assert.False(empty.HasImageInfo);
        Assert.False(empty.HasOwnerInfo);
        Assert.Equal("-", empty.NetworkDisplay);
        Assert.Equal("API", empty.MetricSourceBadge);

        Assert.True(rich.HasCpuMetricInfo);
        Assert.True(rich.HasCpuMetricBar);
        Assert.False(rich.HasCpuMetricTextOnly);
        Assert.True(rich.HasMemoryMetricInfo);
        Assert.True(rich.HasMemoryMetricBar);
        Assert.False(rich.HasMemoryMetricTextOnly);
        Assert.True(rich.HasStorageMetricInfo);
        Assert.True(rich.HasStorageMetricBar);
        Assert.False(rich.HasStorageMetricTextOnly);
        Assert.True(rich.HasNetworkMetricInfo);
        Assert.True(rich.HasReadyInfo);
        Assert.True(rich.HasRestartInfo);
        Assert.True(rich.HasNodeInfo);
        Assert.True(rich.HasImageInfo);
        Assert.True(rich.HasOwnerInfo);
        Assert.Equal("25%", rich.CpuDisplay is "25m" ? rich.CpuPercentDisplay : rich.CpuPercentDisplay);
        Assert.Equal("25%", rich.CpuCompactDisplay);
        Assert.Contains("CPU:", rich.CpuMetricDetail, StringComparison.Ordinal);
        Assert.Equal("50%", rich.MemoryCompactDisplay);
        Assert.Contains("Memory:", rich.MemoryMetricDetail, StringComparison.Ordinal);
        Assert.Equal("50%", rich.StorageCompactDisplay);
        Assert.Contains("Storage:", rich.StorageMetricDetail, StringComparison.Ordinal);
        Assert.Equal("LIVE", rich.MetricSourceBadge);
        Assert.Equal("ok", rich.MetricTooltip);
    }

    [Fact]
    public void Bootstrap_from_store_returns_stable_shell_sections()
    {
        var bootstrap = AppBootstrap.FromStore(AppStore.Empty);

        Assert.Same(AppStore.Empty, bootstrap.Store);
        Assert.Equal(["resources", "sources", "settings"], bootstrap.ShellSections.Select(section => section.Id));
        Assert.Equal(["Resources", "Sources", "Settings"], bootstrap.ShellSections.Select(section => section.Label));
    }

    [Fact]
    public void Resource_filter_covers_restart_percentile_and_duration_overflow_edges()
    {
        var rows = new[]
        {
            Row("Running", "r0", 0, "1/1"),
            Row("Running", "r1", 1, "1/1"),
            Row("Running", "r2", 2, "1/1"),
            Row("Running", "r3", 3, "1/1"),
            Row("Running", "r4", 100, "1/1")
        };

        Assert.True(ResourceFilterMatcher.RestartOutlierThreshold(rows) > ResourceFilterMatcher.DefaultRestartOutlierThreshold);
        Assert.Null(ResourceFilterMatcher.ParseHumanDuration("999999999999999999999999999999s"));
        Assert.True(ResourceFilterMatcher.MatchesText(" spaced ", " spaced "));
    }

    [Fact]
    public void Resource_problem_reason_covers_forbidden_ready_status_and_restart_threshold_paths()
    {
        var forbidden = Row("Running", "hidden", 0, "1/1") with { Freshness = FreshnessState.Forbidden };
        var notReady = Row("Running", "not-ready", 0, "1/2");
        var pending = Row("Pending", "pending", 0, "-");
        var quietRestart = Row("Running", "quiet", 2, "1/1");
        var completed = Row("Succeeded", "completed", 0, "0/1");

        Assert.Equal("RBAC hidden", ResourceFilterMatcher.ProblemReason(forbidden));
        Assert.Equal("Ready 1/2", ResourceFilterMatcher.ProblemReason(notReady));
        Assert.Equal("Pending", ResourceFilterMatcher.ProblemReason(pending));
        Assert.Equal(string.Empty, ResourceFilterMatcher.ProblemReason(quietRestart, restartOutlierThreshold: 2));
        Assert.Equal(string.Empty, ResourceFilterMatcher.ProblemReason(completed));
        Assert.Equal(ResourceFilterMatcher.DefaultRestartOutlierThreshold, ResourceFilterMatcher.RestartOutlierThreshold([]));
    }

    [Fact]
    public void Health_calculator_rejects_negative_infrastructure_warnings()
    {
        var error = Assert.Throws<ArgumentOutOfRangeException>(() =>
            ResourceHealthCalculator.Calculate([], infrastructureWarnings: -1));

        Assert.Equal("infrastructureWarnings", error.ParamName);
    }

    [Fact]
    public void App_state_reports_session_and_context_failures_and_removes_active_contexts()
    {
        var state = AppState.InMemoryWithConfigDirectory(TempDirectory(), FixedClock.Instance);

        Assert.Equal(PodlordErrorKind.NoActiveSession, Assert.Throws<PodlordException>(() => state.SessionConnection(null)).Kind);

        state.ImportKubeconfigText("multi", TwoContextKubeconfig());
        var sessions = state.ListSessions();
        var first = sessions[0];
        var second = sessions[1];
        var renamedBlank = state.SetSessionDisplayName(first.Id, " ");

        Assert.Equal(first.DisplayName, renamedBlank.DisplayName);
        Assert.Equal(second.Id, state.SwitchActiveSession(second.Id).Id);
        Assert.Equal(second.Id, state.SessionConnection(null).Session.Id);

        state.RemoveImportedContext(second.ContextId);
        var snapshot = state.Snapshot();

        Assert.DoesNotContain(snapshot.Sessions, session => session.ContextId == second.ContextId);
        Assert.Contains(snapshot.Sessions, session => session.Id == snapshot.ActiveSessionId);
        Assert.Equal(PodlordErrorKind.ContextNotFound, Assert.Throws<PodlordException>(() => state.RenameImportedContext("missing", "x")).Kind);
        state.RemoveImportedContext("missing");
    }

    [Fact]
    public void App_state_persists_settings_and_renames_imported_contexts()
    {
        var state = AppState.InMemoryWithConfigDirectory(TempDirectory(), FixedClock.Instance);
        state.ImportKubeconfigText("one", OneContextKubeconfig("dev"));
        var context = state.Snapshot().ImportedContexts[0];
        var layout = new TableColumnLayout("ResourceGrid", "2:Name", 1, false);
        var pinned = new TableColumnLayout("ResourceGrid", "Status", 0, true, 120d, Pinned: true);
        var settings = Settings.Default with
        {
            Theme = "Ironwood Warroom",
            ScreensaverEnabled = false,
            TableColumnLayouts = [layout, pinned]
        };

        Assert.Equal(settings, state.SaveSettings(settings));
        Assert.Equal("Ironwood Warroom", state.Settings().Theme);
        var stored = state.Settings().TableColumnLayouts ?? Array.Empty<TableColumnLayout>();
        Assert.Equal(2, stored.Count);
        Assert.Contains(stored, item => item.ColumnId == "2:Name" && item.Pinned is false);
        Assert.Contains(stored, item => item.ColumnId == "Status" && item.Pinned);
        Assert.Equal(context.DisplayName, state.RenameImportedContext(context.ContextId, " ").DisplayName);
        Assert.Equal("Production", state.RenameImportedContext(context.ContextId, "Production").DisplayName);
    }

    [Fact]
    public void Human_timestamp_formats_local_zone_with_abbreviation_and_handles_null()
    {
        Assert.Equal("-", PodlordText.HumanTimestamp(null));

        var berlin = TimeZoneInfo.CreateCustomTimeZone(
            "Europe/Berlin",
            TimeSpan.FromHours(1),
            "Central European",
            "Central European Standard Time",
            "Central European Summer Time",
            adjustmentRules: null,
            disableDaylightSavingTime: true);

        var moment = new DateTimeOffset(2026, 3, 2, 14, 32, 0, TimeSpan.Zero);
        Assert.Equal("2026-03-02 15:32 CEST", PodlordText.HumanTimestamp(moment, berlin));
        Assert.Equal("2026-03-02 14:32 UTC", PodlordText.HumanTimestamp(moment, TimeZoneInfo.Utc));
    }

    [Fact]
    public void Build_zone_abbreviation_compacts_word_initials_and_handles_unknown()
    {
        Assert.Equal("CEST", PodlordText.BuildZoneAbbreviation("Central European Summer Time"));
        Assert.Equal("PST", PodlordText.BuildZoneAbbreviation("Pacific Standard Time"));
        Assert.Equal(string.Empty, PodlordText.BuildZoneAbbreviation(string.Empty));
    }

    [Fact]
    public void Zone_abbreviation_falls_back_to_offset_when_name_unparseable()
    {
        var custom = TimeZoneInfo.CreateCustomTimeZone(
            "Etc/CustomNarnia",
            TimeSpan.FromMinutes(345),
            "Narnia",
            "Narnia",
            "Narnia",
            adjustmentRules: null,
            disableDaylightSavingTime: true);
        var moment = new DateTimeOffset(2026, 3, 2, 14, 32, 0, TimeSpan.Zero);
        Assert.Equal("UTC+05:45", PodlordText.ZoneAbbreviation(custom, moment));
    }

    [Fact]
    public void App_state_load_default_uses_safe_config_override_and_recovers_from_bad_store()
    {
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        var directory = TempDirectory();
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var state = AppState.LoadDefault(FixedClock.Instance);
            state.SaveSettings(Settings.Default with { Theme = "Gunmetal Sector" });

            Assert.Equal("Gunmetal Sector", AppState.LoadDefault(FixedClock.Instance).Settings().Theme);

            File.WriteAllText(Path.Combine(directory, "store.json"), "{bad-json");
            Assert.Empty(AppState.LoadDefault(FixedClock.Instance).Snapshot().Sessions);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void App_state_imports_home_kubeconfig_from_safe_home_override()
    {
        var previous = Environment.GetEnvironmentVariable("PODLORD_HOME");
        var home = TempDirectory();
        var kubeDirectory = Path.Combine(home, ".kube");
        Directory.CreateDirectory(kubeDirectory);
        File.WriteAllText(Path.Combine(kubeDirectory, "config"), OneContextKubeconfig("home-dev"));
        Environment.SetEnvironmentVariable("PODLORD_HOME", home);
        try
        {
            var state = AppState.InMemoryWithConfigDirectory(TempDirectory(), FixedClock.Instance);
            var summary = state.ImportHomeKubeconfig();

            Assert.Single(summary.Contexts);
            Assert.Equal("home-dev", state.ListSessions()[0].DisplayName);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_HOME", previous);
            Directory.Delete(home, recursive: true);
        }
    }

    [Fact]
    public void App_state_reports_unknown_session_failures_for_update_and_duplicate()
    {
        var state = AppState.InMemory(FixedClock.Instance);

        Assert.Equal(PodlordErrorKind.SessionNotFound, Assert.Throws<PodlordException>(() => state.SetSessionSafety("missing", SafetyLevel.Dev)).Kind);
        Assert.Equal(PodlordErrorKind.SessionNotFound, Assert.Throws<PodlordException>(() => state.DuplicateSession("missing")).Kind);
        Assert.Equal(PodlordErrorKind.SessionNotFound, Assert.Throws<PodlordException>(() => state.SetSessionNamespaceScope("missing", NamespaceScope.All)).Kind);
    }

    [Fact]
    public void Kubeconfig_importer_covers_auth_types_missing_references_and_parse_errors()
    {
        var importer = new KubeconfigImporter(FixedClock.Instance);

        var summary = importer.ImportText("auth.yaml", AuthKindsKubeconfig());
        var parse = Assert.Throws<PodlordException>(() => importer.ImportText("bad.yaml", "contexts: ["));
        var missing = Assert.Throws<PodlordException>(() => importer.ImportFile(Path.Combine(TempDirectory(), "missing.yaml")));

        Assert.Contains(summary.Contexts, context => context.AuthType == "auth-provider:oidc");
        Assert.Contains(summary.Contexts, context => context.AuthType == "client-certificate");
        Assert.Contains(summary.Contexts, context => context.AuthType == "basic-auth");
        Assert.Contains(summary.Contexts, context => context.AuthType == "unknown");
        Assert.Contains(summary.Contexts, context => context.BrokenReferences.Contains("missing user reference"));
        Assert.Equal(PodlordErrorKind.KubeconfigParse, parse.Kind);
        Assert.Equal(PodlordErrorKind.ReadFile, missing.Kind);
    }

    [Fact]
    public void Kubeconfig_importer_rejects_documents_without_contexts_and_missing_cluster_names()
    {
        var importer = new KubeconfigImporter(FixedClock.Instance);

        var empty = Assert.Throws<PodlordException>(() => importer.ImportText("no-contexts.yaml", "apiVersion: v1\n"));
        var summary = importer.ImportText("missing-cluster.yaml", """
apiVersion: v1
contexts:
- name: broken
  context:
    user: dev
users:
- name: dev
  user:
    token: token
""");
        var snapshot = Assert.Throws<PodlordException>(() => importer.SnapshotForOwnedStore("empty.yaml", " "));

        Assert.Equal(PodlordErrorKind.EmptyKubeconfig, empty.Kind);
        Assert.Contains("missing cluster reference", summary.Contexts[0].BrokenReferences);
        Assert.Equal(PodlordErrorKind.KubeconfigParse, snapshot.Kind);
    }

    [Fact]
    public void Podlord_exception_factories_are_structured_for_ui_boundaries()
    {
        var io = new IOException("denied");
        var cases = new[]
        {
            PodlordException.ReadFile("/tmp/config", io),
            PodlordException.WriteFile("/tmp/store", io),
            PodlordException.KubeconfigParse("bad", "broken", io),
            PodlordException.MissingHomeDirectory(),
            PodlordException.MissingConfigDirectory(),
            PodlordException.ContextNotFound("s1", "ctx1"),
            PodlordException.KubernetesConfig("ctx", "bad auth", io),
            PodlordException.KubernetesApi("ctx", "Pod", "forbidden", io),
            PodlordException.UnsupportedResourceKind("Widget"),
            PodlordException.InvalidInput("bad", "fix it")
        };

        Assert.All(cases, error =>
        {
            Assert.NotEmpty(error.Message);
            Assert.NotEmpty(error.NextAction);
            Assert.Equal(error.Message, error.ToCommandError().Message);
            Assert.Equal(error.NextAction, error.ToCommandError().NextAction);
        });
    }

    [Fact]
    public void Podlord_text_formats_stable_ids_hashes_utc_and_human_ages()
    {
        var clock = FixedClock.Instance;

        Assert.Equal("2026-06-10T10:00:00.0000000Z", PodlordText.NowUtcString(clock));
        Assert.Equal("prod-eu-payments", PodlordText.StableSlug("Prod EU / Payments"));
        Assert.Equal(16, PodlordText.StableHash("same-input").Length);
        Assert.Equal(PodlordText.StableHash("same-input"), PodlordText.StableHash("same-input"));
        Assert.Equal("-", PodlordText.HumanAge(null, clock));
        Assert.Equal("0s", PodlordText.HumanAge(clock.Now.AddSeconds(10), clock));
        Assert.Equal("20s", PodlordText.HumanAge(clock.Now.AddSeconds(-20), clock));
        Assert.Equal("2m", PodlordText.HumanAge(clock.Now.AddMinutes(-2), clock));
        Assert.Equal("3h15m", PodlordText.HumanAge(clock.Now.AddHours(-3).AddMinutes(-15), clock));
        Assert.Equal("2d5h", PodlordText.HumanAge(clock.Now.AddDays(-2).AddHours(-5), clock));
    }

    private static string[] Names(IEnumerable<FlatResourceRow> rows)
    {
        return rows.Select(row => row.Name).ToArray();
    }

    private static FlatResourceRow Row(string status, string name, int restarts, string ready)
    {
        return new FlatResourceRow(
            name,
            status,
            "Pod",
            name,
            "default",
            "cluster-a",
            "1m",
            ready,
            restarts,
            "node-a",
            "api:1",
            null,
            "now",
            FreshnessState.Fresh);
    }

    private static string OneContextKubeconfig(string name)
    {
        return $$"""
apiVersion: v1
clusters:
- name: {{name}}
  cluster:
    server: https://127.0.0.1:6443
contexts:
- name: {{name}}
  context:
    cluster: {{name}}
    user: {{name}}
users:
- name: {{name}}
  user:
    token: token
""";
    }

    private static string TwoContextKubeconfig()
    {
        return """
apiVersion: v1
clusters:
- name: one
  cluster:
    server: https://127.0.0.1:6443
- name: two
  cluster:
    server: https://127.0.0.1:6443
contexts:
- name: one
  context:
    cluster: one
    user: user
- name: two
  context:
    cluster: two
    user: user
users:
- name: user
  user:
    token: token
""";
    }

    private static string AuthKindsKubeconfig()
    {
        return """
apiVersion: v1
clusters:
- name: dev
  cluster:
    server: https://127.0.0.1:6443
contexts:
- name: oidc
  context:
    cluster: dev
    user: oidc
- name: cert
  context:
    cluster: dev
    user: cert
- name: basic
  context:
    cluster: dev
    user: basic
- name: anonymous
  context:
    cluster: dev
    user: anonymous
- name: missing-user
  context:
    cluster: dev
users:
- name: oidc
  user:
    auth-provider:
      name: oidc
      config:
        access-token: token
- name: cert
  user:
    client-certificate: cert.pem
- name: basic
  user:
    username: user
    password: pass
- name: anonymous
  user: {}
""";
    }

    private static string TempDirectory()
    {
        var path = Path.Combine(Path.GetTempPath(), $"podlord-core-test-{Guid.NewGuid():N}");
        Directory.CreateDirectory(path);
        return path;
    }

    private sealed class FixedClock : IPodlordClock
    {
        public static FixedClock Instance { get; } = new();

        public DateTimeOffset Now { get; } = new(2026, 6, 10, 10, 0, 0, TimeSpan.Zero);
    }
}
