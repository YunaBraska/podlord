using Podlord.App;
using Podlord.Core;

namespace Podlord.App.Tests;

public sealed class VisualAlgorithmTests
{
    [Fact]
    public void Radar_water_layer_exposes_state_properties_without_owning_pointer_input()
    {
        var layer = new RadarWaterLayer
        {
            PanX = 12.5,
            PanY = -8.25,
            Zoom = 2.5,
            ActivityRate = 42,
            SpeedPercent = 70,
            PauseAnimation = true,
            IsVisible = false
        };

        Assert.False(layer.IsHitTestVisible);
        Assert.Equal(12.5, layer.PanX);
        Assert.Equal(-8.25, layer.PanY);
        Assert.Equal(2.5, layer.Zoom);
        Assert.Equal(42, layer.ActivityRate);
        Assert.Equal(70, layer.SpeedPercent);
        Assert.True(layer.PauseAnimation);
        Assert.False(layer.IsVisible);
    }

    [Fact]
    public void Radar_water_tiles_are_deterministic_clamped_and_disabled_when_speed_is_zero()
    {
        var first = RadarWaterModel.BuildTiles(
            width: 640,
            height: 360,
            panX: -23.5,
            panY: 41.25,
            zoomValue: 99,
            activityRate: 999,
            speedPercentValue: 999,
            phaseValue: 17);
        var second = RadarWaterModel.BuildTiles(
            width: 640,
            height: 360,
            panX: -23.5,
            panY: 41.25,
            zoomValue: 99,
            activityRate: 999,
            speedPercentValue: 999,
            phaseValue: 17);

        Assert.NotEmpty(first);
        Assert.Equal(first, second);
        Assert.All(first, tile =>
        {
            Assert.InRange(tile.Kind, 0, 2);
            Assert.True(tile.Bounds.Width > 0);
            Assert.True(tile.Bounds.Height > 0);
        });
        Assert.Empty(RadarWaterModel.BuildTiles(140, 92, 0, 0, 1, 10, 0, 1));
        Assert.Empty(RadarWaterModel.BuildTiles(0, 92, 0, 0, 1, 10, 50, 1));
        Assert.Empty(RadarWaterModel.BuildTiles(140, -1, 0, 0, 1, 10, 50, 1));
    }

    [Fact]
    public void Radar_water_interval_and_value_logic_cover_idle_active_and_negative_offsets()
    {
        Assert.Equal(TimeSpan.FromMilliseconds(3_000), RadarWaterModel.WaterIntervalFor(120, 0));
        Assert.Equal(TimeSpan.FromMilliseconds(456.2), RadarWaterModel.WaterIntervalFor(0, 1));
        Assert.Equal(TimeSpan.FromMilliseconds(60), RadarWaterModel.WaterIntervalFor(240, 100));
        Assert.Equal(TimeSpan.FromMilliseconds(270), RadarWaterModel.WaterIntervalFor(-20, 50));
        Assert.Equal(17.5, RadarWaterModel.PositiveMod(-0.5, 18));
        Assert.Equal(0.5, RadarWaterModel.PositiveMod(18.5, 18));

        var values = Enumerable.Range(-16, 33)
            .SelectMany(column => Enumerable.Range(-16, 33), (column, row) => RadarWaterModel.StableWaterValue(column, row, 9))
            .ToHashSet();

        Assert.Contains(0, values);
        Assert.Contains(1, values);
        Assert.Contains(2, values);
        Assert.Contains(9, values);
    }

    [Fact]
    public void Yaml_analyzer_tokenizes_keys_scalars_comments_markers_numbers_and_keywords()
    {
        Assert.Empty(YamlSyntaxAnalyzer.AnalyzeLine(string.Empty));
        Assert.Empty(YamlSyntaxAnalyzer.AnalyzeLine("    "));

        Assert.Equal(
            [
                new YamlToken(2, 7, YamlTokenKind.Key),
                new YamlToken(9, 19, YamlTokenKind.Scalar),
                new YamlToken(19, 27, YamlTokenKind.Comment)
            ],
            YamlSyntaxAnalyzer.AnalyzeLine("  image: nginx:1.2 # deploy"));
        Assert.Equal(
            [
                new YamlToken(0, 1, YamlTokenKind.Marker),
                new YamlToken(2, 6, YamlTokenKind.Keyword)
            ],
            YamlSyntaxAnalyzer.AnalyzeLine("- true"));
        Assert.Equal(
            [
                new YamlToken(0, 5, YamlTokenKind.Key),
                new YamlToken(7, 11, YamlTokenKind.Number)
            ],
            YamlSyntaxAnalyzer.AnalyzeLine("count: 12.5"));
        Assert.Equal(
            [
                new YamlToken(0, 1, YamlTokenKind.Marker),
                new YamlToken(4, 15, YamlTokenKind.Comment)
            ],
            YamlSyntaxAnalyzer.AnalyzeLine("-   # only item"));
    }

    [Fact]
    public void Yaml_analyzer_ignores_comment_and_colon_markers_inside_quotes()
    {
        Assert.Equal(
            [
                new YamlToken(0, 3, YamlTokenKind.Key),
                new YamlToken(5, 24, YamlTokenKind.Scalar)
            ],
            YamlSyntaxAnalyzer.AnalyzeLine("url: \"https://x:443/#ok\""));
        Assert.Equal(
            [
                new YamlToken(0, 4, YamlTokenKind.Key),
                new YamlToken(6, 17, YamlTokenKind.Scalar),
                new YamlToken(17, 23, YamlTokenKind.Comment)
            ],
            YamlSyntaxAnalyzer.AnalyzeLine("note: 'a # b: c' # real"));
        Assert.Equal(
            [new YamlToken(0, 4, YamlTokenKind.Keyword)],
            YamlSyntaxAnalyzer.AnalyzeLine("NULL"));
        Assert.Equal(
            [new YamlToken(0, 8, YamlTokenKind.Number)],
            YamlSyntaxAnalyzer.AnalyzeLine("-1.25e+2"));
    }

    [Fact]
    public void Yaml_analyzer_marks_resource_reference_values_and_empty_values()
    {
        Assert.False(YamlSyntaxAnalyzer.IsResourceRefKey(null));
        Assert.False(YamlSyntaxAnalyzer.IsResourceRefKey(string.Empty));
        Assert.True(YamlSyntaxAnalyzer.IsResourceRefKey("name"));
        Assert.True(YamlSyntaxAnalyzer.IsResourceRefKey("namespace"));
        Assert.True(YamlSyntaxAnalyzer.IsResourceRefKey("kind"));

        Assert.Equal(
            [
                new YamlToken(0, 4, YamlTokenKind.Key),
                new YamlToken(6, 11, YamlTokenKind.ResourceRef)
            ],
            YamlSyntaxAnalyzer.AnalyzeLine("name: pod-a"));
        Assert.Equal(
            [new YamlToken(0, 4, YamlTokenKind.Key)],
            YamlSyntaxAnalyzer.AnalyzeLine("name:   "));
        Assert.Equal(
            [
                new YamlToken(0, 4, YamlTokenKind.Key),
                new YamlToken(8, 17, YamlTokenKind.Comment)
            ],
            YamlSyntaxAnalyzer.AnalyzeLine("kind:   # comment"));
        Assert.Equal(
            [
                new YamlToken(0, 4, YamlTokenKind.Key),
                new YamlToken(6, 16, YamlTokenKind.ResourceRef)
            ],
            YamlSyntaxAnalyzer.AnalyzeLine("kind: Deployment"));
    }

    [Fact]
    public void Source_rows_normalize_names_filters_and_emit_change_notifications()
    {
        var removed = false;
        var renamed = "";
        var activated = false;
        var imported = new ImportedContextRowViewModel(
            "ctx",
            "/tmp/config",
            "old",
            _ => removed = true,
            (_, value) => renamed = value,
            _ => activated = true,
            isActive: true,
            sourceName: "home",
            hash: "abc");
        var changed = new List<string?>();
        imported.PropertyChanged += (_, args) => changed.Add(args.PropertyName);

        imported.DisplayName = "new";
        imported.DisplayName = "new";
        imported.RenameCommand.Execute(null);
        imported.RemoveCommand.Execute(null);
        imported.ActivateCommand.Execute(null);

        Assert.Equal("ACTIVE", imported.ActiveMark);
        Assert.Equal(["DisplayName"], changed);
        Assert.Equal("new", renamed);
        Assert.True(removed);
        Assert.True(activated);
        Assert.Equal("home", imported.SourceName);
        Assert.Equal("abc", imported.Hash);

        var filters = new List<string>();
        var source = new SourceStatusRow(
            name: "source",
            hash: "hash",
            importedAt: "now",
            source: "/tmp/config",
            context: "ctx",
            cluster: "cluster",
            user: "user",
            authType: "token",
            status: "ok",
            detail: "ready",
            filterName: "",
            renameAction: (_, value) => value.Trim().ToUpperInvariant(),
            filterAction: (_, value) =>
            {
                filters.Add(value);
                return value.Trim().ToLowerInvariant();
            });
        changed.Clear();
        source.PropertyChanged += (_, args) => changed.Add(args.PropertyName);

        source.Context = " next ";
        source.Context = "NEXT";
        source.FilterName = " Pods ";
        source.FilterName = "";

        Assert.Equal("NEXT", source.Context);
        Assert.Equal("default", source.FilterName);
        Assert.Equal(["Context", "FilterName", "FilterName"], changed);
        Assert.Equal([" Pods ", "default"], filters);
    }

    [Fact]
    public void Workspace_visual_models_update_only_when_state_changes()
    {
        AppThemeCatalog.Apply("Sirocco Command");
        var resource = new FlatResourceRow(
            "id",
            "Running",
            "Pod",
            "api",
            "default",
            "cluster",
            "1m",
            "1/1",
            0,
            "node",
            "image:v1",
            "owner",
            "now",
            FreshnessState.Fresh);
        var graph = new GraphNodeViewModel("Pod", "api", "default", "Running", resource);
        var graphChanges = new List<string?>();
        graph.PropertyChanged += (_, args) => graphChanges.Add(args.PropertyName);

        graph.IsSearchMatch = true;
        graph.IsSearchMatch = true;
        graph.IsCurrentSearchMatch = true;

        Assert.True(graph.HasResource);
        Assert.Equal(["IsSearchMatch", "BorderBrush", "BackgroundBrush", "IsCurrentSearchMatch", "BorderBrush", "BackgroundBrush"], graphChanges);
        Assert.NotNull(graph.BorderBrush);
        Assert.NotNull(graph.BackgroundBrush);

        var idle = new RadarIdleCellViewModel(1, 2, 3, 4, AppThemeCatalog.StatusBrush("HEALTHY"));
        var idleChanges = new List<string?>();
        idle.PropertyChanged += (_, args) => idleChanges.Add(args.PropertyName);
        idle.UpdateFrom(new RadarIdleCellViewModel(1, 9, 3, 7, AppThemeCatalog.StatusBrush("WARNING")));

        Assert.Equal(1, idle.X);
        Assert.Equal(9, idle.Y);
        Assert.Equal(7, idle.Height);
        Assert.Contains("Y", idleChanges);
        Assert.Contains("Height", idleChanges);
        Assert.Contains("Brush", idleChanges);
    }

    [Fact]
    public void Radar_blocks_selection_update_and_tooltip_state_are_stable()
    {
        var resource = new FlatResourceRow(
            "id",
            "Warning",
            "Pod",
            "api",
            null,
            "cluster",
            "1m",
            "0/1",
            3,
            null,
            "-",
            null,
            "now",
            FreshnessState.Stale);
        var block = new RadarBlockViewModel(
            resource,
            "group",
            1,
            2,
            3,
            4,
            AppThemeCatalog.StatusBrush("HEALTHY"),
            "problem",
            "metrics",
            isDimmed: true);
        var changes = new List<string?>();
        block.PropertyChanged += (_, args) => changes.Add(args.PropertyName);

        Assert.Equal("Pod/api", block.ToolTipTitle);
        Assert.Equal("cluster", block.ToolTipNamespace);
        Assert.Equal(0.72, block.Opacity);
        Assert.Equal(0.5, block.BorderThickness);

        block.SetSelected(true);
        block.SetSelected(true);
        Assert.True(block.IsSelected);
        Assert.Equal(2, block.BorderThickness);
        Assert.Contains("IsSelected", changes);
        Assert.Contains("BorderThickness", changes);

        var updated = new RadarBlockViewModel(
            resource with { Name = "api-2" },
            "group-2",
            5,
            6,
            7,
            8,
            AppThemeCatalog.StatusBrush("WARNING"),
            "",
            "",
            isPlaceholder: true,
            isSelected: false,
            displayKind: "Idle",
            displayName: "cell",
            isClickable: true,
            isDimmed: false);
        block.UpdateFrom(updated);

        Assert.Equal("group-2", block.Group);
        Assert.Equal("RADAR IDLE CELL", block.ToolTipTitle);
        Assert.Equal("group-2", block.ToolTipNamespace);
        Assert.Equal(1, block.Opacity);
        Assert.Contains(string.Empty, changes);
    }

    [Fact]
    public void Resource_value_and_port_forward_rows_keep_secret_and_running_state_explicit()
    {
        var secret = new ResourceValueRow("password", Convert.ToBase64String(System.Text.Encoding.UTF8.GetBytes("s3cr3t")), sensitive: true, base64Encoded: true);
        var visible = new ResourceValueRow("config", new string('x', 190), sensitive: false, base64Encoded: false);
        var changes = new List<string?>();
        secret.PropertyChanged += (_, args) => changes.Add(args.PropertyName);

        Assert.Equal("base64", secret.Encoding);
        Assert.Equal("••••••••••••", secret.DisplayValue);
        Assert.Equal("s3cr3t", secret.PreferredCopyValue);
        Assert.False(secret.HasPlainCopy);
        Assert.True(secret.HasBase64Copy);
        Assert.Equal("REVEAL", secret.RevealLabel);

        secret.RevealTemporarily();
        secret.RevealTemporarily();
        secret.Hide();

        Assert.Equal("••••••••••••", secret.DisplayValue);
        Assert.Contains(nameof(ResourceValueRow.IsRevealed), changes);
        Assert.Contains(nameof(ResourceValueRow.DisplayValue), changes);
        Assert.Contains(nameof(ResourceValueRow.RevealLabel), changes);
        Assert.EndsWith("...", visible.DisplayValue, StringComparison.Ordinal);
        Assert.Equal("plain", visible.Encoding);
        Assert.True(visible.HasPlainCopy);
        Assert.False(visible.HasBase64Copy);

        var rawStructuredValue = ".:53 {\r\n\terrors\n\thealth {\r\n\t\tlameduck 5s\r\n\t}\n}\u0001";
        var structured = new ResourceValueRow("Corefile", rawStructuredValue, sensitive: false, base64Encoded: false);

        Assert.Equal(".:53 {\n    errors\n    health {\n        lameduck 5s\n    }\n}\\u0001", structured.DisplayValue);
        Assert.Equal(rawStructuredValue, structured.PreferredCopyValue);

        var port = new PortForwardTaskViewModel("pf", "session", "Pod", "api", "default", 8080, 18080, "native", "starting");
        changes.Clear();
        port.PropertyChanged += (_, args) => changes.Add(args.PropertyName);

        port.Status = "running";
        port.Status = "running";
        port.Process = null;
        port.Stop();

        Assert.Equal("running", port.Status);
        Assert.Null(port.Forwarder);
        Assert.Contains(nameof(PortForwardTaskViewModel.Status), changes);
        Assert.Contains(nameof(PortForwardTaskViewModel.IsRunning), changes);
    }

    [Fact]
    public void Focus_metric_rows_place_suggestion_marker_from_suggestion_percent()
    {
        var suggested = new FocusMetricRow("CPU", "125m / 500m", 25, true, "160m request / 250m limit", 50);
        var plain = new FocusMetricRow("Memory", "128Mi", 0, false);

        Assert.True(suggested.HasSuggestion);
        Assert.Equal(77, suggested.SuggestionLeft);
        Assert.Contains("Suggestion: 160m request / 250m limit", suggested.MetricTooltip, StringComparison.Ordinal);
        Assert.False(plain.HasSuggestion);
        Assert.Equal("Memory: 128Mi", plain.MetricTooltip);
    }

    [Fact]
    public void Readiness_bar_is_healthy_when_full_while_utilization_bar_is_critical_when_full()
    {
        // Utilization: high is bad.
        Assert.Equal("HEALTHY", FocusMetricRow.BarStateFor(10, healthyWhenFull: false));
        Assert.Equal("WARNING", FocusMetricRow.BarStateFor(75, healthyWhenFull: false));
        Assert.Equal("CRITICAL", FocusMetricRow.BarStateFor(100, healthyWhenFull: false));

        // Readiness/availability: full is good (regression: 1/1 ready must not be red).
        Assert.Equal("HEALTHY", FocusMetricRow.BarStateFor(100, healthyWhenFull: true));
        Assert.Equal("WARNING", FocusMetricRow.BarStateFor(50, healthyWhenFull: true));
        Assert.Equal("CRITICAL", FocusMetricRow.BarStateFor(0, healthyWhenFull: true));

        var ready = new FocusMetricRow("Ready", "1/1", 100, true, HealthyWhenFull: true);
        Assert.Equal("HEALTHY", ready.BarState);
        Assert.True(MainWindowViewModel.IsReadinessLabel("Ready"));
        Assert.True(MainWindowViewModel.IsReadinessLabel("Available"));
        Assert.False(MainWindowViewModel.IsReadinessLabel("CPU"));
    }

    [Fact]
    public void Import_path_expansion_resolves_tilde_to_user_profile()
    {
        var home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);

        Assert.Equal(Path.Combine(home, ".kube"), MainWindowViewModel.ExpandUserPath("~/.kube"));
        Assert.Equal(home, MainWindowViewModel.ExpandUserPath("~"));
        Assert.Equal("/etc/kube", MainWindowViewModel.ExpandUserPath("/etc/kube"));
        Assert.Equal(string.Empty, MainWindowViewModel.ExpandUserPath(string.Empty));
    }

    [Fact]
    public void Directory_scan_matches_kubeconfig_files_not_just_yaml()
    {
        // Regression: a ~/.kube folder full of *.kubeconfig / config files imported nothing.
        Assert.True(MainWindowViewModel.LooksLikeKubeconfigFileName("/h/.kube/cluster-1.kubeconfig"));
        Assert.True(MainWindowViewModel.LooksLikeKubeconfigFileName("/h/.kube/mgmt-osl2-0.kubeconfig"));
        Assert.True(MainWindowViewModel.LooksLikeKubeconfigFileName("/h/.kube/config"));
        Assert.True(MainWindowViewModel.LooksLikeKubeconfigFileName("/h/.kube/prod.yaml"));
        Assert.True(MainWindowViewModel.LooksLikeKubeconfigFileName("/h/.kube/staging.yml"));

        Assert.False(MainWindowViewModel.LooksLikeKubeconfigFileName("/h/.kube/download-safespring-kubeconfigs.sh"));
        Assert.False(MainWindowViewModel.LooksLikeKubeconfigFileName("/h/.kube/.hidden"));
        Assert.False(MainWindowViewModel.LooksLikeKubeconfigFileName("/h/.kube/notes.md"));
    }

    [Fact]
    public void Localized_chrome_keeps_diacritics_for_latin_locales()
    {
        Assert.Equal("Språk", PodlordLocalizer.Text("settings.language", "sv"));
        Assert.Equal("INSTÄLLNINGAR", PodlordLocalizer.Text("settings.title", "sv"));
        Assert.Equal("Sprache", PodlordLocalizer.Text("settings.language", "de"));
        Assert.Equal("PARAMÈTRES", PodlordLocalizer.Text("settings.title", "fr"));
        Assert.Equal("Configurações salvas.", PodlordLocalizer.Text("status.settingsSaved", "pt-BR"));

        // Unknown keys fall back to English, never the raw key.
        Assert.Equal("RESOURCES", PodlordLocalizer.Text("nav.resources", "en"));
    }
}
