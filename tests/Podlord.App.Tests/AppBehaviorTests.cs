using System.Globalization;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using System.Text.Json;
using Avalonia.Media;
using Podlord.App;
using Podlord.Core;
using Podlord.Kubernetes;

namespace Podlord.App.Tests;

public sealed class AppBehaviorTests
{
    [Fact]
    public void Theme_catalog_normalizes_names_and_can_apply_without_running_avalonia_app()
    {
        Assert.Contains("Imperial Ledger", AppThemeCatalog.ThemeNames);
        Assert.Contains("Sirocco Command", AppThemeCatalog.ThemeNames);
        Assert.Contains("Ironwood Warroom", AppThemeCatalog.ThemeNames);
        Assert.Contains("Gunmetal Sector", AppThemeCatalog.ThemeNames);
        Assert.Contains("Chitin Brood", AppThemeCatalog.ThemeNames);
        Assert.Contains("Daylight Basic", AppThemeCatalog.ThemeNames);
        Assert.Equal(AppThemeCatalog.ThemeNames.Count, AppThemeCatalog.ThemeNames.Distinct(StringComparer.Ordinal).Count());
        Assert.Equal("Ironwood Warroom", AppThemeCatalog.Normalize("ironwood warroom"));
        Assert.Equal("Nocturne Basic", AppThemeCatalog.Normalize("graphite"));
        Assert.Equal("Sirocco Command", AppThemeCatalog.Normalize("missing-theme"));
        Assert.Equal(["subtle", "medium", "arcade"], AppThemeCatalog.ThemeIntensityNames);
        Assert.Equal(["dark", "light"], AppThemeCatalog.ThemeVariantNames);
        Assert.Equal("subtle", AppThemeCatalog.IntensityName(18));
        Assert.Equal("medium", AppThemeCatalog.IntensityName(56));
        Assert.Equal("arcade", AppThemeCatalog.IntensityName(86));
        Assert.Equal("dark", AppThemeCatalog.NormalizeVariant(null));
        Assert.Equal("light", AppThemeCatalog.NormalizeVariant("LIGHT"));
        Assert.Equal((byte)18, AppThemeCatalog.PixelEffectIntensity("subtle"));
        Assert.Equal((byte)56, AppThemeCatalog.PixelEffectIntensity("medium"));
        Assert.Equal((byte)86, AppThemeCatalog.PixelEffectIntensity("arcade"));
        Assert.False(Settings.Default.RadarWaterEnabled);
        Assert.Equal((byte)0, Settings.Default.RadarWaterSpeed);
        Assert.True(Settings.Default.RadarAutoFollowAlerts);
        Assert.Equal(0, Settings.Default.InactiveSyncMinutes);
        Assert.Equal(0, Settings.Default.RequestHardLimitPerMinute);
        Assert.Equal("dark", Settings.Default.ThemeVariant);
        Assert.Equal("system", Settings.Default.Language);

        AppThemeCatalog.Apply("Gunmetal Sector", 56, "light");
        AppThemeCatalog.Apply("missing-theme", 18, "unknown");
    }

    [Fact]
    public void Localizer_supports_user_selectable_languages_flags_and_falls_back_to_english()
    {
        Assert.Equal(22, PodlordLocalizer.SupportedLocales.Count);
        Assert.Equal(21, PodlordLocalizer.SupportedLocales.Count(option => option.Code != PodlordLocalizer.SystemLanguageCode));
        Assert.Equal(PodlordLocalizer.SupportedLocales.Count, PodlordLocalizer.SupportedLocales.Select(option => option.Code).Distinct(StringComparer.OrdinalIgnoreCase).Count());
        Assert.Contains(PodlordLocalizer.SupportedLocales, option => option.Code == "de");
        Assert.Contains(PodlordLocalizer.SupportedLocales, option => option.Code == "zh-Hans");
        Assert.Contains(PodlordLocalizer.SupportedLocales, option => option.Code == "sv" && option.DisplayName.StartsWith("🇸🇪", StringComparison.Ordinal));

        Assert.Equal("RESOURCES", PodlordLocalizer.Text("nav.resources", "en"));
        Assert.Equal("RESSOURCEN", PodlordLocalizer.Text("nav.resources", "de"));
        Assert.Equal("RESURSER", PodlordLocalizer.Text("nav.resources", "sv"));
        Assert.Equal("资源", PodlordLocalizer.Text("nav.resources", "zh-Hans"));
        Assert.Equal("KUBECONFIG SOURCES", PodlordLocalizer.Text("sources.title", "de"));
        foreach (var locale in PodlordLocalizer.SupportedLocales.Where(option => option.Code is not PodlordLocalizer.SystemLanguageCode and not PodlordLocalizer.DefaultLanguageCode))
        {
            Assert.NotEqual(PodlordLocalizer.Text("settings.radarWater", "en"), PodlordLocalizer.Text("settings.radarWater", locale.Code));
            Assert.NotEqual(PodlordLocalizer.Text("settings.inactiveBackgroundSync", "en"), PodlordLocalizer.Text("settings.inactiveBackgroundSync", locale.Code));
            Assert.NotEqual(PodlordLocalizer.Text("settings.runtimeDiagnosticsTitle", "en"), PodlordLocalizer.Text("settings.runtimeDiagnosticsTitle", locale.Code));
            Assert.NotEqual(PodlordLocalizer.Text("diagnostics.managedHeap", "en"), PodlordLocalizer.Text("diagnostics.managedHeap", locale.Code));
            Assert.NotEqual(PodlordLocalizer.Text("diagnostics.requestsDescription", "en"), PodlordLocalizer.Text("diagnostics.requestsDescription", locale.Code));
            Assert.NotEqual(PodlordLocalizer.Text("action.applyServerSide", "en"), PodlordLocalizer.Text("action.applyServerSide", locale.Code));
            Assert.NotEqual(PodlordLocalizer.Text("inspector.overview", "en"), PodlordLocalizer.Text("inspector.overview", locale.Code));
            Assert.NotEqual(PodlordLocalizer.Text("port.containerPort", "en"), PodlordLocalizer.Text("port.containerPort", locale.Code));
        }

        Assert.Equal("RESOURCES", PodlordLocalizer.Text("nav.resources", "definitely-not-real"));
        Assert.Equal("missing.key", PodlordLocalizer.Text("missing.key", "de"));
    }

    [Theory]
    [InlineData(0, "0B")]
    [InlineData(500, "500B")]
    [InlineData(1024, "1KB")]
    [InlineData(1536, "1.5KB")]
    [InlineData(524288000, "500MB")]
    [InlineData(1073741824, "1GB")]
    public void Cache_size_footer_uses_human_readable_units(long bytes, string expected)
    {
        Assert.Equal(expected, MainWindowViewModel.FormatCacheSize(bytes));
    }

    [Fact]
    public void Localizer_catalog_localization_source_and_runtime_have_complete_key_coverage()
    {
        var source = File.ReadAllText(LocatePodlordLocalizerSource());
        var english = EnglishLocaleKeysFromSource(source);
        var catalogLocales = CatalogLocaleCodesFromSource(source);

        var supportedLocales = PodlordLocalizer.SupportedLocales
            .Select(option => option.Code)
            .Where(code => !code.Equals(PodlordLocalizer.SystemLanguageCode, StringComparison.OrdinalIgnoreCase))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToArray();
        var missingCatalogLocales = supportedLocales
            .Except(catalogLocales, StringComparer.OrdinalIgnoreCase)
            .ToArray();

        Assert.True(
            missingCatalogLocales.Length == 0,
            $"Catalog is missing locales: {string.Join(", ", missingCatalogLocales)}.");
        Assert.All(supportedLocales, code => Assert.Contains(code, catalogLocales, StringComparer.OrdinalIgnoreCase));

        foreach (var locale in supportedLocales)
        {
            if (!string.Equals(locale, PodlordLocalizer.DefaultLanguageCode, StringComparison.OrdinalIgnoreCase))
            {
                Assert.NotEqual(PodlordLocalizer.Text("nav.search", "en"), PodlordLocalizer.Text("nav.search", locale));
            }

            Assert.All(english, key => Assert.NotEqual(key, PodlordLocalizer.Text(key, locale)));
        }
        Assert.NotEqual("Missing", PodlordLocalizer.Text("missing.key", "de"));
    }

    [Fact]
    public void Theme_catalog_covers_intensity_and_status_aliases()
    {
        AppThemeCatalog.Apply("Stellar Senate");
        AppThemeCatalog.Apply("Sirocco Command", 86);

        Assert.Equal((byte)18, AppThemeCatalog.PixelEffectIntensity("unknown"));
        Assert.Equal(AppThemeCatalog.StatusBrush("HEALTHY").ToString(), AppThemeCatalog.StatusBrush("success").ToString());
        Assert.Equal(AppThemeCatalog.StatusBrush("WARNING").ToString(), AppThemeCatalog.StatusBrush("warning").ToString());
        Assert.Equal(AppThemeCatalog.StatusBrush("CRITICAL").ToString(), AppThemeCatalog.StatusBrush("danger").ToString());
        Assert.Equal(AppThemeCatalog.StatusBrush("UNKNOWN").ToString(), AppThemeCatalog.StatusBrush("anything-else").ToString());
    }

    [Fact]
    public void Ui_converters_cover_status_problem_restart_metric_identity_and_port_forward_states()
    {
        var culture = CultureInfo.InvariantCulture;
        var status = new StatusBrushConverter();
        Assert.Equal(AppThemeCatalog.StatusBrush("HEALTHY").ToString(), status.Convert("Running", typeof(IBrush), null, culture).ToString());
        Assert.Equal(AppThemeCatalog.StatusBrush("WARNING").ToString(), status.Convert("Pending", typeof(IBrush), null, culture).ToString());
        Assert.Equal(AppThemeCatalog.StatusBrush("CRITICAL").ToString(), status.Convert("CrashLoopBackOff", typeof(IBrush), null, culture).ToString());
        Assert.Equal(AppThemeCatalog.StatusBrush("UNKNOWN").ToString(), status.Convert("strange", typeof(IBrush), null, culture).ToString());
        Assert.Throws<NotSupportedException>(() => status.ConvertBack(null, typeof(string), null, culture));

        var problem = new ProblemBrushConverter();
        Assert.Equal(AppThemeCatalog.StatusBrush("HEALTHY").ToString(), problem.Convert(null, typeof(IBrush), null, culture).ToString());
        Assert.Equal(AppThemeCatalog.StatusBrush("HEALTHY").ToString(), problem.Convert(Row("Running", "ok", 0, "1/1"), typeof(IBrush), null, culture).ToString());
        Assert.Equal(AppThemeCatalog.StatusBrush("WARNING").ToString(), problem.Convert(Row("Pending", "wait", 0, "-"), typeof(IBrush), null, culture).ToString());
        Assert.Equal(AppThemeCatalog.StatusBrush("CRITICAL").ToString(), problem.Convert(Row("Failed", "bad", 0, "0/1"), typeof(IBrush), null, culture).ToString());
        Assert.Throws<NotSupportedException>(() => problem.ConvertBack(null, typeof(string), null, culture));

        var restart = new RestartBrushConverter();
        Assert.Equal(AppThemeCatalog.TextBrush().ToString(), restart.Convert("1", typeof(IBrush), null, culture).ToString());
        Assert.Equal(AppThemeCatalog.StatusBrush("WARNING").ToString(), restart.Convert(ResourceFilterMatcher.DefaultRestartOutlierThreshold + 1, typeof(IBrush), null, culture).ToString());
        Assert.Equal(AppThemeCatalog.TextBrush().ToString(), restart.Convert("nope", typeof(IBrush), null, culture).ToString());
        Assert.Throws<NotSupportedException>(() => restart.ConvertBack(null, typeof(string), null, culture));

        var metric = new MetricHealthBrushConverter();
        Assert.Equal(AppThemeCatalog.StatusBrush("HEALTHY").ToString(), metric.Convert("42%", typeof(IBrush), null, culture).ToString());
        Assert.Equal(AppThemeCatalog.StatusBrush("WARNING").ToString(), metric.Convert(75, typeof(IBrush), null, culture).ToString());
        Assert.Equal(AppThemeCatalog.StatusBrush("CRITICAL").ToString(), metric.Convert(95d, typeof(IBrush), null, culture).ToString());
        Assert.Equal(AppThemeCatalog.StatusBrush("HEALTHY").ToString(), metric.Convert(null, typeof(IBrush), null, culture).ToString());
        Assert.Throws<NotSupportedException>(() => metric.ConvertBack(null, typeof(string), null, culture));

        var reason = new ProblemReasonConverter();
        Assert.Equal("Ready 0/1", reason.Convert(Row("Running", "not-ready", 0, "0/1"), typeof(string), null, culture));
        Assert.Equal(string.Empty, reason.Convert("no-row", typeof(string), null, culture));
        Assert.Throws<NotSupportedException>(() => reason.ConvertBack(null, typeof(string), null, culture));

        var deterministic = new DeterministicBrushConverter();
        Assert.Equal(Brushes.Transparent, deterministic.Convert("-", typeof(IBrush), null, culture));
        Assert.NotEqual(Brushes.Transparent, deterministic.Convert("node-a", typeof(IBrush), null, culture));
        Assert.Throws<NotSupportedException>(() => deterministic.ConvertBack(null, typeof(string), null, culture));

        var hasValue = new HasValueConverter();
        Assert.True((bool)hasValue.Convert("x", typeof(bool), null, culture));
        Assert.False((bool)hasValue.Convert(" ", typeof(bool), null, culture));
        Assert.False((bool)hasValue.Convert(null, typeof(bool), null, culture));
        Assert.Throws<NotSupportedException>(() => hasValue.ConvertBack(null, typeof(string), null, culture));

        var radar = new RadarBrushConverter();
        Assert.Equal(Brushes.Transparent, radar.Convert("not-row", typeof(IBrush), null, culture));
        Assert.Equal(AppThemeCatalog.StatusBrush("WARNING").ToString(), radar.Convert(Row("Pending", "wait", 0, "-"), typeof(IBrush), null, culture).ToString());
        Assert.Equal(AppThemeCatalog.StatusBrush("CRITICAL").ToString(), radar.Convert(Row("Failed", "bad", 0, "0/1"), typeof(IBrush), null, culture).ToString());
        Assert.NotEqual(Brushes.Transparent, radar.Convert(Row("Running", "ok", 0, "1/1"), typeof(IBrush), null, culture));
        Assert.Throws<NotSupportedException>(() => radar.ConvertBack(null, typeof(string), null, culture));

        var eligibility = new PortForwardEligibilityConverter();
        Assert.True((bool)eligibility.Convert(Row("Running", "api", 0, "1/1"), typeof(bool), null, culture));
        Assert.False((bool)eligibility.Convert(Row("Succeeded", "done", 0, "0/1"), typeof(bool), null, culture));
        Assert.True((bool)eligibility.Convert(Row("Observed", "svc", 0, "-") with { Kind = "Service" }, typeof(bool), null, culture));
        Assert.False((bool)eligibility.Convert(Row("Running", "node", 0, "-") with { Kind = "Node", Namespace = null }, typeof(bool), null, culture));
        Assert.Throws<NotSupportedException>(() => eligibility.ConvertBack(null, typeof(string), null, culture));

        var badge = new PortForwardBadgeConverter();
        var pod = Row("Running", "api", 0, "1/1");
        var forwards = new[] { new PortForwardTaskViewModel("pf", "dev", "Pod", "api", "payments", 80, 18080, "native", "running") { Process = System.Diagnostics.Process.GetCurrentProcess() } };
        Assert.Equal("18080", badge.Convert([pod, forwards], typeof(string), null, culture));
        Assert.Equal(string.Empty, badge.Convert([pod, Array.Empty<PortForwardTaskViewModel>()], typeof(string), null, culture));
        Assert.Equal(string.Empty, badge.Convert([null, forwards], typeof(string), null, culture));
    }

    [Fact]
    public void About_section_exposes_donation_links_repo_actions_and_nerd_rotation()
    {
        var directory = TempDirectory();
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        Assert.Equal("https://github.com/YunaBraska/podlord", viewModel.AboutRepoUrl);
        Assert.Equal("https://github.com/YunaBraska/podlord/issues/new", viewModel.AboutIssueUrl);
        Assert.Equal("https://github.com/YunaBraska/podlord/stargazers", viewModel.AboutStarUrl);
        Assert.Equal("https://github.com/sponsors/YunaBraska", viewModel.AboutSponsorsUrl);
        Assert.Equal("https://buymeacoffee.com/YunaBraska", viewModel.AboutBuyMeACoffeeUrl);
        Assert.Equal("https://ko-fi.com/YunaBraska", viewModel.AboutKoFiUrl);
        Assert.Equal("https://liberapay.com/YunaBraska", viewModel.AboutLiberapayUrl);
        Assert.StartsWith("Version ", viewModel.AboutVersionText, StringComparison.Ordinal);
        Assert.True(viewModel.AboutVersionText.Length > "Version ".Length);

        Assert.True(MainWindowViewModel.AboutBlockCatalog.Count >= 32, $"AboutBlockCatalog should have at least 32 entries but has {MainWindowViewModel.AboutBlockCatalog.Count}.");
        Assert.All(MainWindowViewModel.AboutBlockCatalog, block =>
        {
            Assert.False(string.IsNullOrWhiteSpace(block));
            Assert.DoesNotContain("—", block, StringComparison.Ordinal);
            Assert.DoesNotContain("–", block, StringComparison.Ordinal);
        });
        var first = viewModel.AboutBlockText;
        Assert.Contains(first, MainWindowViewModel.AboutBlockCatalog);
        var changed = false;
        for (var attempt = 0; attempt < MainWindowViewModel.AboutBlockCatalog.Count * 4; attempt++)
        {
            viewModel.PickAboutBlock();
            if (!string.Equals(viewModel.AboutBlockText, first, StringComparison.Ordinal))
            {
                changed = true;
                break;
            }
        }
        Assert.True(changed, "PickAboutBlock should eventually move to a different catalog entry.");

        viewModel.OpenAboutUrl(null);
        viewModel.OpenAboutUrl(string.Empty);
        viewModel.OpenAboutUrl("not a url");
    }

    [Fact]
    public async Task Weekly_update_check_persists_available_release_and_skips_until_due()
    {
        var directory = TempDirectory();
        var state = AppState.InMemoryWithConfigDirectory(directory);
        var checker = new FakeReleaseUpdateChecker(new UpdateCheckState(
            DateTimeOffset.UtcNow.ToString("O"),
            "2026.6.19",
            "2026.6.20",
            "https://github.com/YunaBraska/podlord/releases/tag/2026.6.20",
            "https://github.com/YunaBraska/podlord/releases/download/2026.6.20/podlord-macos-arm64.zip",
            true));
        using var viewModel = new MainWindowViewModel(
            state,
            new KubernetesResourceService(state),
            releaseUpdateChecker: checker);

        await viewModel.CheckForUpdatesIfDueAsync();

        Assert.Equal(1, checker.Calls);
        Assert.True(viewModel.IsUpdateAvailable);
        Assert.Contains("2026.6.20", viewModel.UpdateDownloadTooltipText, StringComparison.Ordinal);
        Assert.Equal("https://github.com/YunaBraska/podlord/releases/download/2026.6.20/podlord-macos-arm64.zip", state.Settings().UpdateCheck?.DownloadUrl);

        await viewModel.CheckForUpdatesIfDueAsync();

        Assert.Equal(1, checker.Calls);
    }

    [Fact]
    public async Task Weekly_update_check_keeps_download_button_hidden_when_latest_is_not_newer()
    {
        var directory = TempDirectory();
        var state = AppState.InMemoryWithConfigDirectory(directory);
        var checker = new FakeReleaseUpdateChecker(new UpdateCheckState(
            DateTimeOffset.UtcNow.ToString("O"),
            "2026.6.19",
            "2026.6.19",
            "https://github.com/YunaBraska/podlord/releases/tag/2026.6.19",
            "https://github.com/YunaBraska/podlord/releases/download/2026.6.19/podlord-macos-arm64.zip",
            false));
        using var viewModel = new MainWindowViewModel(
            state,
            new KubernetesResourceService(state),
            releaseUpdateChecker: checker);

        await viewModel.CheckForUpdatesIfDueAsync();

        Assert.Equal(1, checker.Calls);
        Assert.False(viewModel.IsUpdateAvailable);
        Assert.Equal("2026.6.19", state.Settings().UpdateCheck?.LatestVersion);
        Assert.False(state.Settings().UpdateCheck?.IsNewer);
    }

    [Fact]
    public async Task Update_check_due_gate_runs_after_seven_days_and_preserves_known_update_on_failure()
    {
        var directory = TempDirectory();
        var state = AppState.InMemoryWithConfigDirectory(directory);
        var oldVisibleUpdate = new UpdateCheckState(
            DateTimeOffset.UtcNow.AddDays(-8).ToString("O"),
            "2026.6.19",
            "2026.6.20",
            "https://github.com/YunaBraska/podlord/releases/tag/2026.6.20",
            "https://github.com/YunaBraska/podlord/releases/download/2026.6.20/podlord-macos-arm64.zip",
            true);
        state.SaveSettings(state.Settings() with { UpdateCheck = oldVisibleUpdate });
        var checker = new FakeReleaseUpdateChecker(new UpdateCheckState(
            DateTimeOffset.UtcNow.ToString("O"),
            "2026.6.19",
            string.Empty,
            string.Empty,
            string.Empty,
            false,
            "network down"));
        using var viewModel = new MainWindowViewModel(
            state,
            new KubernetesResourceService(state),
            releaseUpdateChecker: checker);

        Assert.True(MainWindowViewModel.ShouldCheckForUpdates(oldVisibleUpdate, DateTimeOffset.UtcNow));
        await viewModel.CheckForUpdatesIfDueAsync();

        Assert.Equal(1, checker.Calls);
        Assert.True(viewModel.IsUpdateAvailable);
        Assert.Equal("2026.6.20", state.Settings().UpdateCheck?.LatestVersion);
        Assert.Equal("network down", state.Settings().UpdateCheck?.Error);
    }

    [Fact]
    public void Release_update_checker_compares_versions_and_selects_matching_asset()
    {
        Assert.True(GitHubReleaseUpdateChecker.IsNewerRelease("2026.6.19", "2026.6.20"));
        Assert.True(GitHubReleaseUpdateChecker.IsNewerRelease("2026.6.19-local+sha", "2026.7.1"));
        Assert.True(GitHubReleaseUpdateChecker.IsNewerRelease("2026.6", "2026.6.1"));
        Assert.False(GitHubReleaseUpdateChecker.IsNewerRelease("2026.6.19", "2026.6.19"));
        Assert.False(GitHubReleaseUpdateChecker.IsNewerRelease("2026.6.20", "2026.6.19"));
        Assert.False(GitHubReleaseUpdateChecker.IsNewerRelease("dev-build", "2026.6.19"));
        Assert.False(GitHubReleaseUpdateChecker.IsNewerRelease("2026.6.19", "dev-build"));
        Assert.False(GitHubReleaseUpdateChecker.IsNewerRelease(string.Empty, "2026.6.19"));

        var assets = new[]
        {
            new ReleaseAssetInfo("podlord-linux-x64.tar.gz", "linux"),
            new ReleaseAssetInfo("podlord-macos-arm64.zip", "mac"),
            new ReleaseAssetInfo("podlord-osx-x64.zip", "legacy-mac"),
            new ReleaseAssetInfo("podlord-win-x64.zip", "win")
        };

        Assert.Equal("mac", GitHubReleaseUpdateChecker.PreferredAssetUrl(assets, "podlord-macos-arm64.zip"));
        Assert.Equal("legacy-mac", GitHubReleaseUpdateChecker.PreferredAssetUrl(assets, "podlord-macos-x64.zip"));
        Assert.Equal("mac", GitHubReleaseUpdateChecker.PreferredAssetUrl(assets, "podlord-osx-arm64.zip"));
        Assert.Equal("linux", GitHubReleaseUpdateChecker.PreferredAssetUrl(assets, "podlord-linux-x64.tar.gz"));
        Assert.Equal(
            "mac-contained",
            GitHubReleaseUpdateChecker.PreferredAssetUrl(
                [new ReleaseAssetInfo("podlord-2026.6.20-macos-arm64.zip", "mac-contained")],
                "podlord-macos-arm64.zip"));
        Assert.Null(GitHubReleaseUpdateChecker.PreferredAssetUrl(assets, "podlord-macos-arm.zip"));
        Assert.Matches(@"^podlord-(macos|win|linux)-(arm64|arm|x86|x64)\.(zip|tar\.gz)$", GitHubReleaseUpdateChecker.RuntimeReleaseAssetName());
        Assert.Matches(@"^\d+\.\d+\.\d+", GitHubReleaseUpdateChecker.CurrentApplicationVersion());
    }

    [Fact]
    public async Task GitHub_release_update_checker_reads_latest_release_and_prefers_runtime_asset()
    {
        using var http = new HttpClient(new AppRecordingHandler(request =>
        {
            Assert.Equal("/repos/YunaBraska/podlord/releases/latest", request.RequestUri?.AbsolutePath);
            Assert.Contains(request.Headers.UserAgent, value => value.Product?.Name == "Podlord" && value.Product.Version == "2026.6.19-test");
            Assert.Contains(request.Headers.Accept, value => value.MediaType == "application/vnd.github+json");
            Assert.True(request.Headers.TryGetValues("X-GitHub-Api-Version", out var versions));
            Assert.Contains(GitHubReleaseUpdateChecker.GitHubApiVersion, versions);
            return JsonResponse("""
              {
                "tag_name": "2026.6.20",
                "html_url": "https://github.com/YunaBraska/podlord/releases/tag/2026.6.20",
                "assets": [
                  { "name": "podlord-macos-arm64.zip", "browser_download_url": "https://downloads/macos-arm64.zip" },
                  { "name": "podlord-osx-arm64.zip", "browser_download_url": "https://downloads/osx-arm64.zip" },
                  { "name": "podlord-linux-arm64.tar.gz", "browser_download_url": "https://downloads/linux-arm64.tar.gz" },
                  { "name": "podlord-linux-x64.tar.gz", "browser_download_url": "https://downloads/linux-x64.tar.gz" }
                ]
              }
              """);
        }));
        using var checker = new GitHubReleaseUpdateChecker(http);

        var result = await checker.CheckLatestAsync("2026.6.19 test", CancellationToken.None);

        Assert.Equal("2026.6.20", result.LatestVersion);
        Assert.True(result.IsNewer);
        Assert.Equal("https://github.com/YunaBraska/podlord/releases/tag/2026.6.20", result.ReleaseUrl);
        Assert.Equal(
            GitHubReleaseUpdateChecker.PreferredAssetUrl(
                [
                    new ReleaseAssetInfo("podlord-macos-arm64.zip", "https://downloads/macos-arm64.zip"),
                    new ReleaseAssetInfo("podlord-osx-arm64.zip", "https://downloads/osx-arm64.zip"),
                    new ReleaseAssetInfo("podlord-linux-arm64.tar.gz", "https://downloads/linux-arm64.tar.gz"),
                    new ReleaseAssetInfo("podlord-linux-x64.tar.gz", "https://downloads/linux-x64.tar.gz")
                ],
                GitHubReleaseUpdateChecker.RuntimeReleaseAssetName()),
            result.DownloadUrl);
        Assert.Equal(string.Empty, result.Error);
        Assert.NotEqual(string.Empty, result.LastCheckedAt);
    }

    [Fact]
    public async Task GitHub_release_update_checker_falls_back_to_release_url_when_no_asset_matches()
    {
        using var http = new HttpClient(new AppRecordingHandler(_ => JsonResponse("""
          {
            "tag_name": "2026.6.21",
            "html_url": "https://github.com/YunaBraska/podlord/releases/tag/2026.6.21",
            "assets": [
              { "name": "readme.txt", "browser_download_url": "https://downloads/readme.txt" }
            ]
          }
          """)));
        using var checker = new GitHubReleaseUpdateChecker(http);

        var result = await checker.CheckLatestAsync("2026.6.20", CancellationToken.None);

        Assert.True(result.IsNewer);
        Assert.Equal("2026.6.21", result.LatestVersion);
        Assert.Equal(result.ReleaseUrl, result.DownloadUrl);

        using var noAssetsHttp = new HttpClient(new AppRecordingHandler(_ => JsonResponse("""
          {
            "tag_name": "2026.6.22",
            "html_url": "https://github.com/YunaBraska/podlord/releases/tag/2026.6.22"
          }
          """)));
        using var noAssetsChecker = new GitHubReleaseUpdateChecker(noAssetsHttp);

        var noAssets = await noAssetsChecker.CheckLatestAsync("2026.6.21", CancellationToken.None);

        Assert.True(noAssets.IsNewer);
        Assert.Equal(noAssets.ReleaseUrl, noAssets.DownloadUrl);
    }

    [Fact]
    public async Task GitHub_release_update_checker_returns_failure_state_for_http_invalid_json_and_timeout()
    {
        using var failingHttp = new HttpClient(new AppRecordingHandler(_ => new HttpResponseMessage(HttpStatusCode.TooManyRequests)
        {
            ReasonPhrase = "Rate Limited"
        }));
        using var unreachableHttp = new HttpClient(new AppRecordingHandler(_ => throw new HttpRequestException("network down")));
        using var invalidJsonHttp = new HttpClient(new AppRecordingHandler(_ => new HttpResponseMessage(HttpStatusCode.OK)
        {
            Content = new StringContent("{not-json", Encoding.UTF8, "application/json")
        }));
        using var timeoutHttp = new HttpClient(new AppRecordingHandler(_ => throw new TaskCanceledException("timeout")));

        using var failingChecker = new GitHubReleaseUpdateChecker(failingHttp);
        using var unreachableChecker = new GitHubReleaseUpdateChecker(unreachableHttp);
        using var invalidJsonChecker = new GitHubReleaseUpdateChecker(invalidJsonHttp);
        using var timeoutChecker = new GitHubReleaseUpdateChecker(timeoutHttp);

        var httpFailure = await failingChecker.CheckLatestAsync("2026.6.20", CancellationToken.None);
        var networkFailure = await unreachableChecker.CheckLatestAsync("2026.6.20", CancellationToken.None);
        var jsonFailure = await invalidJsonChecker.CheckLatestAsync("2026.6.20", CancellationToken.None);
        var timeoutFailure = await timeoutChecker.CheckLatestAsync("2026.6.20", CancellationToken.None);

        Assert.False(httpFailure.IsNewer);
        Assert.Equal("2026.6.20", httpFailure.CurrentVersion);
        Assert.Contains("429", httpFailure.Error, StringComparison.Ordinal);
        Assert.Contains("Rate Limited", httpFailure.Error, StringComparison.Ordinal);
        Assert.False(networkFailure.IsNewer);
        Assert.Contains("network down", networkFailure.Error, StringComparison.Ordinal);
        Assert.False(jsonFailure.IsNewer);
        Assert.Contains("invalid JSON", jsonFailure.Error, StringComparison.OrdinalIgnoreCase);
        Assert.False(timeoutFailure.IsNewer);
        Assert.Equal("GitHub release check timed out.", timeoutFailure.Error);
    }

    [Fact]
    public void Release_update_checker_factory_respects_disabled_environment()
    {
        var previous = Environment.GetEnvironmentVariable("PODLORD_DISABLE_UPDATE_CHECK");
        try
        {
            Environment.SetEnvironmentVariable("PODLORD_DISABLE_UPDATE_CHECK", "1");
            using var disabled = ReleaseUpdateCheckerFactory.CreateDefault();

            Environment.SetEnvironmentVariable("PODLORD_DISABLE_UPDATE_CHECK", "false");
            using var enabled = ReleaseUpdateCheckerFactory.CreateDefault();

            Assert.IsType<NoOpReleaseUpdateChecker>(disabled);
            Assert.IsType<GitHubReleaseUpdateChecker>(enabled);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_DISABLE_UPDATE_CHECK", previous);
        }
    }

    [Fact]
    public async Task Noop_release_update_checker_returns_current_version_and_disabled_message()
    {
        using var checker = new NoOpReleaseUpdateChecker();

        var result = await checker.CheckLatestAsync("2026.6.20", CancellationToken.None);

        Assert.Equal("2026.6.20", result.CurrentVersion);
        Assert.Equal("2026.6.20", result.LatestVersion);
        Assert.False(result.IsNewer);
        Assert.Contains("disabled", result.Error, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void Release_packaging_uses_public_macos_asset_names_and_plain_archive_root()
    {
        var root = LocateProjectRoot();
        var workflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "release.yml"));
        var macOSBundleScript = File.ReadAllText(Path.Combine(root, "scripts", "build-macos-app.sh"));
        var publishScript = File.ReadAllText(Path.Combine(root, "scripts", "publish.sh"));

        Assert.Contains("asset: macos-arm64", workflow, StringComparison.Ordinal);
        Assert.Contains("asset: macos-x64", workflow, StringComparison.Ordinal);
        Assert.Contains("target = Path(\"dist\") / f\"podlord-{asset}.tar.gz\"", workflow, StringComparison.Ordinal);
        Assert.Contains("target = Path(\"dist\") / f\"podlord-{asset}.zip\"", workflow, StringComparison.Ordinal);
        Assert.Contains("archive.add(source, arcname=\"podlord\")", workflow, StringComparison.Ordinal);
        Assert.Contains("archive.write(path, Path(\"podlord\") / path.relative_to(source))", workflow, StringComparison.Ordinal);
        Assert.Contains("\"dist/podlord-$ASSET.zip\"", workflow, StringComparison.Ordinal);
        Assert.DoesNotContain("dist/podlord-$RID.zip", workflow, StringComparison.Ordinal);
        Assert.DoesNotContain("arcname=f\"podlord-{rid}\"", workflow, StringComparison.Ordinal);

        Assert.Contains("macos-arm64) RID=osx-arm64 ;;", macOSBundleScript, StringComparison.Ordinal);
        Assert.Contains("macos-x64) RID=osx-x64 ;;", macOSBundleScript, StringComparison.Ordinal);
        Assert.Contains("BUNDLE_DIR=\"$ROOT_DIR/out/$APP_NAME.app\"", macOSBundleScript, StringComparison.Ordinal);
        Assert.DoesNotContain("BUNDLE_DIR=\"$ROOT_DIR/out/$APP_NAME-$RID.app\"", macOSBundleScript, StringComparison.Ordinal);

        Assert.Contains("RIDS=\"macos-arm64 macos-x64", publishScript, StringComparison.Ordinal);
        Assert.Contains("macos-arm64) dotnet_rid=osx-arm64 ;;", publishScript, StringComparison.Ordinal);
        Assert.Contains("-r \"$dotnet_rid\"", publishScript, StringComparison.Ordinal);
    }

    [Fact]
    public void Yaml_editor_restores_to_new_resource_yaml_when_yaml_tab_inactive()
    {
        var directory = TempDirectory();
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));
        viewModel.SelectedInspectorTabIndex = 0;

        var first = new ResourceDetail(
            new ResourceIdentity(null, "Pod", "ns", "first"),
            "Running",
            FreshnessState.Fresh,
            "apiVersion: v1\nkind: Pod\nmetadata:\n  name: first\n",
            Array.Empty<DetailItem>(),
            Array.Empty<DetailItem>(),
            Array.Empty<EventSummary>(),
            Array.Empty<ResourceValueItem>());
        viewModel.RenderDetailForTesting(first);
        Assert.Contains("name: first", viewModel.EditableYaml, StringComparison.Ordinal);

        viewModel.EditableYaml = viewModel.EditableYaml + "\n# user scribble\n";
        var dirty = viewModel.EditableYaml;
        Assert.Contains("# user scribble", dirty, StringComparison.Ordinal);

        var second = first with
        {
            Identity = new ResourceIdentity(null, "Pod", "ns", "second"),
            Yaml = "apiVersion: v1\nkind: Pod\nmetadata:\n  name: second\n"
        };
        viewModel.RenderDetailForTesting(second);

        Assert.Contains("name: second", viewModel.EditableYaml, StringComparison.Ordinal);
        Assert.DoesNotContain("# user scribble", viewModel.EditableYaml, StringComparison.Ordinal);
    }

    [Fact]
    public void Yaml_editor_preserves_unsaved_edits_when_yaml_tab_active_and_resource_changes()
    {
        var directory = TempDirectory();
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));
        viewModel.SelectedInspectorTabIndex = 0;

        var first = new ResourceDetail(
            new ResourceIdentity(null, "Pod", "ns", "first"),
            "Running",
            FreshnessState.Fresh,
            "apiVersion: v1\nkind: Pod\nmetadata:\n  name: first\n",
            Array.Empty<DetailItem>(),
            Array.Empty<DetailItem>(),
            Array.Empty<EventSummary>(),
            Array.Empty<ResourceValueItem>());
        viewModel.RenderDetailForTesting(first);

        viewModel.SelectedInspectorTabIndex = 1;
        viewModel.EditableYaml = viewModel.EditableYaml + "\n# scribble\n";

        var second = first with
        {
            Identity = new ResourceIdentity(null, "Pod", "ns", "second"),
            Yaml = "apiVersion: v1\nkind: Pod\nmetadata:\n  name: second\n"
        };
        viewModel.RenderDetailForTesting(second);

        Assert.Contains("# scribble", viewModel.EditableYaml, StringComparison.Ordinal);
        Assert.DoesNotContain("name: second", viewModel.EditableYaml, StringComparison.Ordinal);
    }

    [Fact]
    public void Open_known_resource_reference_returns_false_for_unmatched_value_and_sets_status()
    {
        var directory = TempDirectory();
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        Assert.False(viewModel.HasKnownResourceReference("Pod/missing"));
        Assert.Null(viewModel.ResolveResourceReferenceForPreview("Pod/missing"));
        Assert.False(viewModel.OpenKnownResourceReference("Pod/missing"));
        Assert.Contains("Pod/missing", viewModel.StatusLine, StringComparison.Ordinal);
    }

    [Fact]
    public void Inspector_history_inserts_after_cursor_and_caps_at_thirty_two()
    {
        var directory = TempDirectory();
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        viewModel.PushInspectorHistoryForTesting("a");
        viewModel.PushInspectorHistoryForTesting("b");
        viewModel.PushInspectorHistoryForTesting("c");
        Assert.Equal(new[] { "a", "b", "c" }, viewModel.InspectorHistoryIdsForTesting);
        Assert.Equal(2, viewModel.InspectorHistoryCursorForTesting);

        var historyField = typeof(MainWindowViewModel).GetField("inspectorHistoryCursor", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic);
        historyField!.SetValue(viewModel, 1);
        viewModel.PushInspectorHistoryForTesting("d");
        Assert.Equal(new[] { "a", "b", "d", "c" }, viewModel.InspectorHistoryIdsForTesting);
        Assert.Equal(2, viewModel.InspectorHistoryCursorForTesting);

        for (var index = 0; index < 64; index++)
        {
            viewModel.PushInspectorHistoryForTesting($"id{index}");
        }
        Assert.Equal(32, viewModel.InspectorHistoryIdsForTesting.Count);
        Assert.InRange(viewModel.InspectorHistoryCursorForTesting, 0, 31);
    }

    [Fact]
    public void Resource_sort_cycles_descending_ascending_none_and_glyph_clears_when_switching_column()
    {
        var directory = TempDirectory();
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        Assert.Equal(string.Empty, viewModel.ResourceSortGlyphFor("CPU"));
        viewModel.SortResourcesBy("CPU");
        Assert.Equal("▼", viewModel.ResourceSortGlyphFor("CPU"));
        viewModel.SortResourcesBy("CPU");
        Assert.Equal("▲", viewModel.ResourceSortGlyphFor("CPU"));
        viewModel.SortResourcesBy("CPU");
        Assert.Equal(string.Empty, viewModel.ResourceSortGlyphFor("CPU"));

        viewModel.SortResourcesBy("Memory");
        Assert.Equal("▼", viewModel.ResourceSortGlyphFor("Memory"));
        Assert.Equal(string.Empty, viewModel.ResourceSortGlyphFor("CPU"));
    }

    [Fact]
    public void About_block_never_repeats_the_same_entry_in_consecutive_picks()
    {
        var directory = TempDirectory();
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        var last = viewModel.AboutBlockText;
        for (var iteration = 0; iteration < 25; iteration++)
        {
            viewModel.PickAboutBlock();
            var current = viewModel.AboutBlockText;
            Assert.NotEqual(last, current);
            last = current;
        }
    }

    [Fact]
    public void Graphics_settings_update_effects_and_radar_options()
    {
        var directory = TempDirectory();
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        viewModel.GraphicsQualitySetting = "arcade";
        viewModel.ThemeVariantSetting = "light";
        viewModel.AnimationIntensitySetting = 35;
        viewModel.RadarWaterEnabledSetting = false;
        viewModel.RadarWaterSpeedSetting = 70;

        var settings = state.Settings();
        Assert.Equal((byte)86, settings.PixelEffectIntensity);
        Assert.Equal("light", settings.ThemeVariant);
        Assert.Equal((byte)35, settings.AnimationIntensity);
        Assert.False(settings.RadarWaterEnabled);
        Assert.Equal((byte)70, settings.RadarWaterSpeed);
        Assert.Equal("arcade", viewModel.GraphicsQualitySetting);
        Assert.Equal("light", viewModel.ThemeVariantSetting);
        Assert.Equal("35%", viewModel.AnimationIntensityLabel);
        Assert.Equal("70%", viewModel.RadarWaterSpeedLabel);
    }

    [Fact]
    public void Language_setting_updates_visible_shell_labels_without_changing_cluster_names()
    {
        var directory = TempDirectory();
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        Assert.Equal("RESOURCES", viewModel.NavResourcesText);
        Assert.Equal("🌐 System language", viewModel.LanguageSetting);

        viewModel.LanguageSetting = "🇩🇪 Deutsch (de)";

        Assert.Equal("de", state.Settings().Language);
        Assert.Equal("🇩🇪 Deutsch (de)", viewModel.LanguageSetting);
        Assert.Equal("RESSOURCEN", viewModel.NavResourcesText);
        Assert.Equal("Keine Ressourcen geladen", viewModel.ResourceLogoTitle);

        viewModel.LanguageSetting = "not-listed";

        Assert.Equal("system", state.Settings().Language);
    }

    [Fact]
    public void Inactive_sync_setting_defaults_disabled_and_persists_selected_interval()
    {
        var directory = TempDirectory();
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        Assert.Equal("disabled", viewModel.InactiveSyncSetting);
        Assert.Contains("pauses", viewModel.InactiveSyncDescription, StringComparison.OrdinalIgnoreCase);

        viewModel.InactiveSyncSetting = "10m";

        Assert.Equal(10, state.Settings().InactiveSyncMinutes);
        Assert.Equal("10m", viewModel.InactiveSyncSetting);
        Assert.Contains("10m", viewModel.InactiveSyncDescription, StringComparison.Ordinal);

        viewModel.InactiveSyncSetting = "13m";

        Assert.Equal(0, state.Settings().InactiveSyncMinutes);
        Assert.Equal("disabled", viewModel.InactiveSyncSetting);
    }

    [Fact]
    public void Request_hard_limit_setting_defaults_none_and_accepts_per_minute_values()
    {
        var directory = TempDirectory();
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        Assert.Equal("none", viewModel.RequestHardLimitSetting);
        Assert.Contains("No extra request ceiling", viewModel.RequestHardLimitDescription, StringComparison.Ordinal);

        viewModel.RequestHardLimitSetting = "120/min";

        Assert.Equal(120, state.Settings().RequestHardLimitPerMinute);
        Assert.Equal("120/min", viewModel.RequestHardLimitSetting);
        Assert.Contains("120/min", viewModel.RequestHardLimitDescription, StringComparison.Ordinal);

        viewModel.RequestHardLimitSetting = "off";

        Assert.Equal(0, state.Settings().RequestHardLimitPerMinute);
        Assert.Equal("none", viewModel.RequestHardLimitSetting);
    }

    [Fact]
    public void Background_refresh_interval_contracts_cover_focus_cache_and_failure_states()
    {
        Assert.Equal(TimeSpan.FromSeconds(45), MainWindowViewModel.BackgroundRefreshIntervalFor(false, true, false, false, TimeSpan.FromMinutes(9)));
        Assert.Equal(TimeSpan.FromMinutes(4), MainWindowViewModel.BackgroundRefreshIntervalFor(false, true, true, false, TimeSpan.Zero));
        Assert.Equal(TimeSpan.FromSeconds(90), MainWindowViewModel.BackgroundRefreshIntervalFor(false, false, false, false, TimeSpan.Zero));

        Assert.Equal(TimeSpan.FromSeconds(12), MainWindowViewModel.BackgroundRefreshIntervalFor(true, true, false, false, TimeSpan.FromSeconds(10)));
        Assert.Equal(TimeSpan.FromSeconds(25), MainWindowViewModel.BackgroundRefreshIntervalFor(true, true, false, true, TimeSpan.FromSeconds(10)));
        Assert.Equal(TimeSpan.FromSeconds(20), MainWindowViewModel.BackgroundRefreshIntervalFor(true, true, true, false, TimeSpan.FromSeconds(12)));
        Assert.Equal(TimeSpan.FromSeconds(45), MainWindowViewModel.BackgroundRefreshIntervalFor(true, true, true, false, TimeSpan.FromMinutes(1)));
        Assert.Equal(TimeSpan.FromSeconds(120), MainWindowViewModel.BackgroundRefreshIntervalFor(true, true, true, false, TimeSpan.FromMinutes(8)));

        var now = new DateTimeOffset(2026, 6, 15, 12, 0, 0, TimeSpan.Zero);
        Assert.Equal(TimeSpan.FromSeconds(1), MainWindowViewModel.InactiveBackgroundCheckInterval(5, null, now));
        Assert.Equal(TimeSpan.FromSeconds(60), MainWindowViewModel.InactiveBackgroundCheckInterval(10, now, now));
        Assert.Equal(TimeSpan.FromSeconds(60), MainWindowViewModel.InactiveBackgroundCheckInterval(10, now.AddMinutes(-2), now));
        Assert.Equal(TimeSpan.FromSeconds(60), MainWindowViewModel.InactiveBackgroundCheckInterval(0, now, now));
    }

    [Fact]
    public void SetAppFocus_after_dispose_is_exception_free()
    {
        var directory = TempDirectory();
        var state = AppState.InMemoryWithConfigDirectory(directory);
        var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        viewModel.Dispose();

        var error = Record.Exception(() =>
        {
            viewModel.SetAppFocus(false);
            viewModel.SetAppFocus(true);
            viewModel.SetAppFocus(false);
        });

        Assert.Null(error);
    }

    [Fact]
    public async Task SetAppFocus_near_dispose_does_not_emit_objectdisposed_from_inflight_refresh()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        var inFlight = new AsyncAppRecordingHandler(async (_, cancellationToken) =>
        {
            await Task.Delay(200, cancellationToken).ConfigureAwait(false);
            return new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent(
                    "{\"items\":[]}",
                    Encoding.UTF8,
                    "application/json")
            };
        });
        var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state, inFlight));

        try
        {
            viewModel.ReloadSessions();
            viewModel.KindPicker.SetExpression("\"Pod\"");
            var refreshTask = viewModel.RefreshResourcesAsync(force: true);
            await Task.Delay(50);

            viewModel.SetAppFocus(false);
            viewModel.SetAppFocus(true);
            viewModel.Dispose();

            var focusError = Record.Exception(() =>
            {
                viewModel.SetAppFocus(false);
                viewModel.SetAppFocus(true);
            });
            Assert.Null(focusError);

            var refreshError = await Record.ExceptionAsync(async () => await refreshTask);
            Assert.False(refreshError is ObjectDisposedException);
        }
        finally
        {
            viewModel.Dispose();
        }
    }

    [Fact]
    public async Task Refresh_failure_before_http_requests_is_visible_in_diagnostics_rows()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfigWithoutServer());
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        var handler = new AsyncAppRecordingHandler((_, _) => Task.FromResult(new HttpResponseMessage(HttpStatusCode.OK)));
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state, handler));
        viewModel.ReloadSessions();

        await viewModel.RefreshResourcesAsync(force: true);

        Assert.Empty(handler.Requests);
        Assert.Contains("cluster server is missing", viewModel.StatusLine, StringComparison.OrdinalIgnoreCase);
        Assert.Contains(viewModel.RequestAuditRows, row =>
            row.Method == "APP"
            && row.Path.Contains("client setup", StringComparison.OrdinalIgnoreCase)
            && row.Outcome.Contains("cluster server is missing", StringComparison.OrdinalIgnoreCase));
        Assert.Contains(viewModel.RequestAuditRows, row =>
            row.Method == "APP"
            && row.Path.Contains("resource refresh", StringComparison.OrdinalIgnoreCase)
            && row.Outcome.Contains("cluster server is missing", StringComparison.OrdinalIgnoreCase));
    }

    [Fact]
    public void Startup_without_any_kubeconfig_stays_empty_and_healthy()
    {
        var directory = TempDirectory();
        var fakeHome = Path.Combine(directory, "home");
        Directory.CreateDirectory(fakeHome);
        var previousHome = Environment.GetEnvironmentVariable("PODLORD_HOME");
        try
        {
            Environment.SetEnvironmentVariable("PODLORD_HOME", fakeHome);
            var state = AppState.InMemoryWithConfigDirectory(directory);
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

            viewModel.LoadStartupKubeconfigs([]);

            Assert.Null(viewModel.SelectedSession);
            Assert.Empty(viewModel.Sessions);
            Assert.Empty(viewModel.RequestAuditRows);
            Assert.DoesNotContain("Cache:", viewModel.FooterLine, StringComparison.Ordinal);
            viewModel.SelectedWorkspace = "settings";
            Assert.Contains(viewModel.DiagnosticsRows, row => row.Label == "Cache" && row.Value == "0B");
            Assert.Equal("No resources loaded", viewModel.ResourceLogoTitle);
            Assert.DoesNotContain("failed", viewModel.StatusLine, StringComparison.OrdinalIgnoreCase);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_HOME", previousHome);
        }
    }

    [Fact]
    public void Diagnostics_rows_follow_language_changes_without_restart()
    {
        var directory = TempDirectory();
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(
            state,
            new KubernetesResourceService(state));

        viewModel.SelectedWorkspace = "settings";

        Assert.Contains(viewModel.DiagnosticsRows, row => row.Label == "Managed heap");

        viewModel.LanguageSetting = PodlordLocalizer.LanguageOptionLabel("de");

        Assert.DoesNotContain(viewModel.DiagnosticsRows, row => row.Label == "Managed heap");
        Assert.Contains(viewModel.DiagnosticsRows, row => row.Label == "Verwalteter Heap");
        Assert.Contains(viewModel.DiagnosticsRows, row => row.Description.Contains(".NET", StringComparison.Ordinal));
    }

    [Fact]
    public async Task Fake_kubernetes_refresh_keeps_resource_behavior_footer_and_diagnostics_contract()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        using var viewModel = new MainWindowViewModel(
            state,
            new KubernetesResourceService(state, new AppRecordingHandler(request =>
            {
                var path = request.RequestUri?.AbsolutePath ?? string.Empty;
                var json = path switch
                {
                    "/api/v1/pods" => """
                      {"items":[
                        {"metadata":{"name":"api","namespace":"payments","uid":"pod-1","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-a","containers":[{"image":"repo/api:1","resources":{"limits":{"cpu":"500m","memory":"1Gi"}}}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}},
                        {"metadata":{"name":"worker","namespace":"jobs","uid":"pod-2","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-b","containers":[{"image":"repo/worker:1","resources":{"limits":{"cpu":"250m","memory":"512Mi"}}}]},"status":{"phase":"Pending","containerStatuses":[{"ready":false,"restartCount":2,"state":{"waiting":{"reason":"ImagePullBackOff"}}}]}}
                      ]}
                      """,
                    "/apis/metrics.k8s.io/v1beta1/pods" => """
                      {"items":[
                        {"metadata":{"name":"api","namespace":"payments"},"containers":[{"name":"api","usage":{"cpu":"100m","memory":"256Mi"}}]},
                        {"metadata":{"name":"worker","namespace":"jobs"},"containers":[{"name":"worker","usage":{"cpu":"50m","memory":"128Mi"}}]}
                      ]}
                      """,
                    "/apis/metrics.k8s.io/v1beta1/nodes" => """{"items":[]}""",
                    _ => """{"items":[]}"""
                };
                return new HttpResponseMessage(HttpStatusCode.OK)
                {
                    Content = new StringContent(json, Encoding.UTF8, "application/json")
                };
            })));

        viewModel.ReloadSessions();
        viewModel.ProblemsOnly = false;
        viewModel.KindPicker.SetExpression("\"Pod\"");
        await viewModel.RefreshResourcesAsync(force: true);

        Assert.Equal(["api", "worker"], viewModel.Resources.Select(row => row.Name).Order(StringComparer.Ordinal).ToArray());
        Assert.Contains(viewModel.Resources, row => row.Name == "api" && row.CpuSummaryDisplay == "100m / 500m" && row.MemorySummaryDisplay == "256Mi / 1Gi");
        Assert.Contains(viewModel.Resources, row => row.Name == "worker" && row.Restarts == 2 && row.Status == "ImagePullBackOff");
        Assert.Contains("visible: 2/", viewModel.FooterLine, StringComparison.Ordinal);
        Assert.Contains("API:", viewModel.FooterLine, StringComparison.Ordinal);
        Assert.Contains("Synced:", viewModel.FooterLine, StringComparison.Ordinal);
        Assert.DoesNotContain("Cache:", viewModel.FooterLine, StringComparison.Ordinal);
        viewModel.SelectedWorkspace = "settings";
        Assert.Contains(viewModel.DiagnosticsRows, row => row.Label == "Cache" && !row.Value.Equals("0B", StringComparison.Ordinal));
        Assert.Contains(viewModel.DiagnosticsRows, row => row.Label == "Managed heap");
        Assert.NotEmpty(viewModel.RadarBlocks);
    }

    [Fact]
    public async Task Repeated_fake_kubernetes_refreshes_replace_views_instead_of_growing_them()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        using var viewModel = new MainWindowViewModel(
            state,
            new KubernetesResourceService(state, new AppRecordingHandler(request =>
            {
                var path = request.RequestUri?.AbsolutePath ?? string.Empty;
                var json = path switch
                {
                    "/api/v1/pods" => """
                      {"items":[
                        {"metadata":{"name":"api","namespace":"payments","uid":"pod-1","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-a","containers":[{"image":"repo/api:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}},
                        {"metadata":{"name":"worker","namespace":"jobs","uid":"pod-2","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-b","containers":[{"image":"repo/worker:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}}
                      ]}
                      """,
                    "/apis/metrics.k8s.io/v1beta1/pods" => """{"items":[]}""",
                    "/apis/metrics.k8s.io/v1beta1/nodes" => """{"items":[]}""",
                    _ => """{"items":[]}"""
                };
                return new HttpResponseMessage(HttpStatusCode.OK)
                {
                    Content = new StringContent(json, Encoding.UTF8, "application/json")
                };
            })));

        viewModel.ReloadSessions();
        viewModel.ProblemsOnly = false;
        viewModel.KindPicker.SetExpression("\"Pod\"");
        viewModel.SelectedWorkspace = "settings";
        await viewModel.RefreshResourcesAsync(force: true);
        var firstCounts = (Resources: viewModel.Resources.Count, Radar: viewModel.RadarBlocks.Count, Diagnostics: viewModel.DiagnosticsRows.Count);

        for (var index = 0; index < 5; index++)
        {
            await viewModel.RefreshResourcesAsync(force: true);
            Assert.Equal(firstCounts.Resources, viewModel.Resources.Count);
            Assert.Equal(firstCounts.Radar, viewModel.RadarBlocks.Count);
            Assert.Equal(firstCounts.Diagnostics, viewModel.DiagnosticsRows.Count);
            Assert.True(viewModel.RequestAuditRows.Count <= 256);
        }
    }

    [Fact]
    public void Saved_filter_preserves_metric_filters()
    {
        var directory = TempDirectory();
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        viewModel.PresetName = "hot storage";
        viewModel.CpuPicker.SetExpression(">500m");
        viewModel.MemoryPicker.SetExpression(">=1Gi");
        viewModel.StoragePicker.SetExpression(">5Gi");
        viewModel.SaveCurrentFilter();

        var saved = Assert.Single(viewModel.SavedPresets, preset => preset.Name == "hot storage");
        Assert.Equal(">500m", saved.Cpu);
        Assert.Equal(">=1Gi", saved.Memory);
        Assert.Equal(">5Gi", saved.Storage);

        viewModel.CpuPicker.SetExpression(string.Empty);
        viewModel.MemoryPicker.SetExpression(string.Empty);
        viewModel.StoragePicker.SetExpression(string.Empty);
        viewModel.SelectedPreset = Preset("temporary", false);
        viewModel.SelectedPreset = saved;

        Assert.Equal(">500m", viewModel.CpuPicker.Expression);
        Assert.Equal(">=1Gi", viewModel.MemoryPicker.Expression);
        Assert.Equal(">5Gi", viewModel.StoragePicker.Expression);
    }

    [Fact]
    public void Filter_preset_store_roundtrips_sorted_presets_under_safe_config_override()
    {
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        var temp = TempDirectory();
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", temp);
        try
        {
            var alpha = Preset("alpha", true);
            var zulu = Preset("zulu", false);

            FilterPresetStore.Save([zulu, alpha]);
            var loaded = FilterPresetStore.Load();

            Assert.Equal(["alpha", "default", "zulu"], loaded.Select(preset => preset.Name).ToArray());
            Assert.True(loaded[0].ProblemsOnly);
            Assert.False(loaded[1].ProblemsOnly);
            Assert.Equal("256", loaded[1].Limit);
            Assert.False(loaded[2].ProblemsOnly);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(temp, recursive: true);
        }
    }

    [Fact]
    public void Filter_preset_store_returns_default_for_missing_and_malformed_files()
    {
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        var temp = TempDirectory();
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", temp);
        try
        {
            var missing = Assert.Single(FilterPresetStore.Load());
            Assert.Equal("default", missing.Name);
            Assert.False(missing.ProblemsOnly);
            Assert.False(missing.ActivityOnly);
            var path = Path.Combine(temp, "filter-presets.json");
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, "{not-json");

            var malformed = Assert.Single(FilterPresetStore.Load());
            Assert.Equal("default", malformed.Name);
            Assert.Equal("256", malformed.Limit);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(temp, recursive: true);
        }
    }

    [Fact]
    public void Default_filter_is_present_updateable_and_not_deletable()
    {
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        var directory = TempDirectory();
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var state = AppState.InMemoryWithConfigDirectory(directory);
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

            var defaultPreset = Assert.Single(viewModel.SavedPresets);
            Assert.Equal("default", defaultPreset.Name);
            Assert.Contains("default", viewModel.SourceFilterOptions);

            viewModel.SelectedPreset = defaultPreset;
            viewModel.Search = "api";
            viewModel.SaveCurrentFilter();

            var savedDefault = Assert.Single(viewModel.SavedPresets, preset => preset.Name == "default");
            Assert.Equal("api", savedDefault.Search);

            viewModel.SelectedPreset = savedDefault;
            viewModel.RemoveSelectedFilter();

            Assert.Contains(viewModel.SavedPresets, preset => preset.Name == "default");
            viewModel.SelectedPreset = null;

            Assert.Equal("default", viewModel.SelectedPreset?.Name);
            Assert.Contains("Default filter cannot be deleted", viewModel.StatusLine, StringComparison.Ordinal);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Saved_filter_rename_commits_immediately_and_updates_source_assignments()
    {
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        var directory = TempDirectory();
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var kubeconfig = Path.Combine(directory, "config.yaml");
            File.WriteAllText(kubeconfig, OneContextKubeconfig("http://127.0.0.1:1"));
            var state = AppState.InMemoryWithConfigDirectory(directory);
            state.ImportKubeconfig(kubeconfig);
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));
            viewModel.PresetName = "Pods";
            viewModel.KindPicker.SetExpression("\"Pod\"");
            viewModel.SaveCurrentFilter();
            var preset = Assert.Single(viewModel.SavedPresets, item => item.Name == "Pods");

            viewModel.ReloadSessions();
            var source = Assert.Single(viewModel.Sources);
            viewModel.SelectedPreset = preset;
            viewModel.RenameSavedFilter(preset, "Workloads");

            Assert.DoesNotContain(viewModel.SavedPresets, item => item.Name == "Pods");
            var renamed = Assert.Single(viewModel.SavedPresets, item => item.Name == "Workloads");
            Assert.Equal("\"Pod\"", renamed.Kind);
            Assert.Equal(renamed, viewModel.SelectedPreset);
            Assert.Equal("Workloads", viewModel.PresetName);
            Assert.Contains("Workloads", viewModel.SourceFilterOptions);
            Assert.Equal("Workloads", Assert.Single(viewModel.Sources).FilterName);
            Assert.Equal("Workloads", state.Snapshot().ImportedContexts[0].FilterName);
            Assert.Contains("Renamed filter 'Pods' to 'Workloads'.", viewModel.StatusLine, StringComparison.Ordinal);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Saved_filter_rename_ignores_stale_row_object_after_lost_focus_commit()
    {
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        var directory = TempDirectory();
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var state = AppState.InMemoryWithConfigDirectory(directory);
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));
            viewModel.PresetName = "Activity";
            viewModel.ActivityOnly = true;
            viewModel.SaveCurrentFilter();
            var stalePreset = Assert.Single(viewModel.SavedPresets, item => item.Name == "Activity");

            viewModel.RenameSavedFilter(stalePreset, "Recent Activity");
            viewModel.RenameSavedFilter(stalePreset, "Recent Activity");

            Assert.DoesNotContain(viewModel.SavedPresets, item => item.Name == "Activity");
            Assert.Single(viewModel.SavedPresets, item => item.Name == "Recent Activity");
            Assert.Equal("Recent Activity", viewModel.SelectedPreset?.Name);
            Assert.Contains("already renamed", viewModel.StatusLine, StringComparison.Ordinal);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Main_view_model_lists_kubeconfig_sources_latest_import_first_with_hashes()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        var clock = new MutableClock(new DateTimeOffset(2026, 6, 10, 8, 0, 0, TimeSpan.Zero));
        var state = AppState.InMemoryWithConfigDirectory(directory, clock);
        File.WriteAllText(kubeconfig, OneContextKubeconfig("http://127.0.0.1:1"));
        state.ImportKubeconfig(kubeconfig);
        var firstHash = state.Snapshot().ImportedContexts[0].SourceContentHash;

        clock.Now = clock.Now.AddMinutes(2);
        File.WriteAllText(kubeconfig, OneContextKubeconfig("http://127.0.0.1:2", "prod"));
        state.ImportKubeconfig(kubeconfig);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        viewModel.ReloadSessions();

        Assert.Equal(2, viewModel.Sources.Count);
        Assert.Equal("2026-06-10T08:02:00.0000000Z", viewModel.Sources[0].ImportedAt);
        Assert.Equal("config.yaml", viewModel.Sources[0].Name);
        Assert.Equal("prod", viewModel.Sources[0].Context);
        Assert.NotEqual(firstHash, viewModel.Sources[0].Hash);
        Assert.Equal(firstHash, viewModel.Sources[1].Hash);
        Assert.Contains("copy:", viewModel.Sources[0].Detail, StringComparison.Ordinal);
        Assert.Contains("dev", viewModel.ActiveSessionChipLabel, StringComparison.Ordinal);

        viewModel.ImportedContextRows[0].ActivateCommand.Execute(null);

        Assert.Equal("prod", viewModel.SelectedSession?.DisplayName);
        Assert.Contains("prod", viewModel.ActiveSessionChipLabel, StringComparison.Ordinal);
        Assert.Equal("ACTIVE", viewModel.ImportedContextRows[0].ActiveMark);
    }

    [Fact]
    public void Main_view_model_hides_identical_source_duplicates_in_display_lists()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("http://127.0.0.1:1"));
        var seed = AppState.InMemoryWithConfigDirectory(directory);
        seed.ImportKubeconfig(kubeconfig);
        var current = seed.Snapshot().ImportedContexts[0];
        var legacy = current with
        {
            ContextId = "legacy-context",
            SourceContentHash = "",
            ImportedAt = "2026-06-10T07:59:00.0000000Z"
        };
        var session = seed.ListSessions()[0];
        var store = AppStore.Empty with
        {
            ImportedContexts = [legacy, current],
            Sessions = [session],
            ActiveSessionId = session.Id
        };
        File.WriteAllText(
            Path.Combine(directory, "store.json"),
            JsonSerializer.Serialize(store, new JsonSerializerOptions(JsonSerializerDefaults.Web)));
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        var state = AppState.LoadDefault();
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        try
        {
            viewModel.ReloadSessions();

            Assert.Single(state.Snapshot().ImportedContexts);
            Assert.Single(viewModel.Sources);
            Assert.Single(viewModel.ImportedContextRows);
            Assert.Equal("SOURCES (1)", viewModel.ImportedSourcesLabel);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Main_view_model_removes_source_snapshot_and_owned_kubeconfig_copy()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("http://127.0.0.1:1"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        viewModel.ReloadSessions();
        var source = Assert.Single(viewModel.Sources);
        viewModel.SelectedSource = source;

        viewModel.RemoveSource(source);

        Assert.Empty(viewModel.Sources);
        Assert.Empty(state.Snapshot().ImportedContexts);
        Assert.Empty(state.Snapshot().Sessions);
        Assert.False(viewModel.IsInspectorVisible);
        Assert.Empty(Directory.EnumerateFiles(Path.Combine(directory, "kubeconfigs"), "*.yaml"));
        Assert.Contains("Removed source snapshot", viewModel.StatusLine, StringComparison.Ordinal);
    }

    [Fact]
    public void Source_removal_clears_selected_source_and_related_view_state()
    {
        var directory = TempDirectory();
        var configA = Path.Combine(directory, "a.yaml");
        File.WriteAllText(configA, OneContextKubeconfig("http://127.0.0.1:1", "dev-a"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(configA);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        viewModel.ReloadSessions();
        var removed = Assert.Single(viewModel.Sources);
        viewModel.SelectedSource = removed;

        viewModel.RemoveSource(removed);

        Assert.Null(viewModel.SelectedSource);
        Assert.True(string.IsNullOrWhiteSpace(viewModel.SelectedSource?.Context));
        Assert.Null(viewModel.SelectedSession);
        Assert.False(viewModel.IsSelectedSource);
        Assert.False(viewModel.IsInspectorVisible);
        Assert.Empty(viewModel.Sources);
        Assert.Empty(viewModel.ImportedContextRows);
        Assert.Contains("Removed source snapshot", viewModel.StatusLine, StringComparison.Ordinal);
    }

    [Fact]
    public void Smart_source_import_scans_directory_with_kube_extension_files()
    {
        var directory = TempDirectory();
        var scanRoot = Path.Combine(directory, "scan");
        Directory.CreateDirectory(scanRoot);
        File.WriteAllText(Path.Combine(scanRoot, "cluster.kube"), OneContextKubeconfig("http://127.0.0.1:1", "kube"));
        File.WriteAllText(Path.Combine(scanRoot, "cluster.kubeconfig"), OneContextKubeconfig("http://127.0.0.1:2", "kubeconfig"));
        File.WriteAllText(Path.Combine(scanRoot, "cluster.cfg"), OneContextKubeconfig("http://127.0.0.1:3", "cfg"));
        File.WriteAllText(Path.Combine(scanRoot, "notes.yaml"), "not: kubeconfig");
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        viewModel.ImportPath = scanRoot;
        viewModel.ImportPathNow();

        Assert.Contains("Imported 3 context(s) from 3 kubeconfig file(s); ignored 1", viewModel.StatusLine, StringComparison.Ordinal);
        Assert.Equal(3, viewModel.Sources.Count);
        Assert.Contains(viewModel.Sources, source => source.Context == "kube");
        Assert.Contains(viewModel.Sources, source => source.Context == "kubeconfig");
        Assert.Contains(viewModel.Sources, source => source.Context == "cfg");
    }

    [Fact]
    public async Task Source_selection_uses_inspector_and_applies_edited_yaml_without_touching_original_file()
    {
        var directory = TempDirectory();
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        var kubeconfig = Path.Combine(directory, "config.yaml");
        var original = OneContextKubeconfig("http://127.0.0.1:1");
        File.WriteAllText(kubeconfig, original);
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        try
        {
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

            viewModel.PresetName = "team-a";
            viewModel.SaveCurrentFilter();
            viewModel.ReloadSessions();
            viewModel.SelectedSource = viewModel.Sources[0];

            Assert.True(viewModel.IsInspectorVisible);
            Assert.True(viewModel.IsSelectedSource);
            Assert.False(viewModel.IsSelectedKubernetesResource);
            Assert.False(viewModel.IsSelectedResourceLoggable);
            Assert.Equal("Source", viewModel.SelectedResource?.Kind);
            Assert.Contains(viewModel.FocusMetrics, row => row.Label == "Path" && row.Value == kubeconfig);
            Assert.Contains(viewModel.FocusMetrics, row => row.Label == "Cluster" && row.Value == "dev");
            Assert.Contains("apiVersion: v1", viewModel.DetailYaml, StringComparison.Ordinal);

            var selectedSource = Assert.IsType<SourceStatusRow>(viewModel.SelectedSource);
            selectedSource.Context = "renamed-source";
            selectedSource.FilterName = "team-a";

            Assert.Equal("renamed-source", viewModel.SelectedSource?.Context);
            Assert.Equal("team-a", viewModel.SelectedSource?.FilterName);
            Assert.Contains(viewModel.Sessions, session => session.DisplayName == "renamed-source");
            Assert.Equal("team-a", state.Snapshot().ImportedContexts[0].FilterName);

            selectedSource.Context = "";

            Assert.Equal("dev", viewModel.SelectedSource?.Context);

            viewModel.EditableYaml = OneContextKubeconfig("http://127.0.0.1:2", "edited");
            await viewModel.ApplyEditedYamlAsync();

            Assert.Equal(original, File.ReadAllText(kubeconfig));
            Assert.Contains("original path was not modified", viewModel.StatusLine, StringComparison.Ordinal);
            Assert.Contains(viewModel.Sources, source => source.Context == "edited");
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Smart_source_import_scans_directory_and_ignores_non_kubeconfig_yaml()
    {
        var directory = TempDirectory();
        var scanRoot = Path.Combine(directory, "scan");
        var nested = Path.Combine(scanRoot, "a", "b");
        Directory.CreateDirectory(nested);
        File.WriteAllText(Path.Combine(nested, "cluster.yaml"), OneContextKubeconfig("http://127.0.0.1:1", "deep"));
        File.WriteAllText(Path.Combine(scanRoot, "notes.yaml"), "not: kubeconfig");
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        viewModel.ImportPath = scanRoot;
        viewModel.ImportPathNow();

        Assert.Contains("Imported 1 context(s) from 1 kubeconfig file(s); ignored 1", viewModel.StatusLine, StringComparison.Ordinal);
        Assert.Single(viewModel.Sources);
        Assert.Equal("deep", viewModel.Sources[0].Context);
    }

    [Fact]
    public void Import_path_empty_input_never_imports_anything()
    {
        var directory = TempDirectory();
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        viewModel.ImportPath = "  ";
        viewModel.ImportPathNow();

        Assert.Empty(viewModel.Sources);
        Assert.Empty(viewModel.Sessions);
        Assert.Empty(state.Snapshot().ImportedContexts);
        Assert.Equal("Choose a kubeconfig file or enter a path, directory, or YAML.", viewModel.StatusLine);
    }

    [Fact]
    public void Import_path_single_file_imports_only_that_file_and_updates_status()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "only.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("http://127.0.0.1:1", "single"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        viewModel.ImportPath = kubeconfig;
        viewModel.ImportPathNow();

        Assert.Contains("Imported 1 context(s).", viewModel.StatusLine, StringComparison.Ordinal);
        Assert.Single(viewModel.Sources);
        Assert.Equal("single", viewModel.Sources[0].Context);
    }

    [Fact]
    public void Import_path_invalid_kubeconfig_file_reports_error_and_adds_no_contexts()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "invalid.yaml");
        File.WriteAllText(kubeconfig, "not a valid source");
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        viewModel.ImportPath = kubeconfig;
        viewModel.ImportPathNow();

        Assert.Equal("Import a kubeconfig that contains contexts.", viewModel.StatusLine);
        Assert.Empty(viewModel.Sources);
        Assert.Empty(viewModel.Sessions);
        Assert.Empty(state.Snapshot().ImportedContexts);
    }

    [Fact]
    public void Smart_source_import_respects_max_depth_and_skips_deeper_directories()
    {
        var directory = TempDirectory();
        var scanRoot = Path.Combine(directory, "scan");
        Directory.CreateDirectory(scanRoot);
        var includeRoot = scanRoot;
        for (var depth = 1; depth <= 10; depth++)
        {
            includeRoot = Path.Combine(includeRoot, $"include-{depth}");
            Directory.CreateDirectory(includeRoot);
        }

        var deepRoot = scanRoot;
        for (var depth = 1; depth <= 34; depth++)
        {
            deepRoot = Path.Combine(deepRoot, $"deep-{depth}");
            Directory.CreateDirectory(deepRoot);
        }

        File.WriteAllText(Path.Combine(includeRoot, "deep.yaml"), OneContextKubeconfig("http://127.0.0.1:1", "included"));
        File.WriteAllText(Path.Combine(deepRoot, "deep.yaml"), OneContextKubeconfig("http://127.0.0.1:2", "excluded"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        viewModel.ImportPath = scanRoot;
        viewModel.ImportPathNow();

        Assert.Contains("Imported 1 context(s) from 1 kubeconfig file(s); ignored 0", viewModel.StatusLine, StringComparison.Ordinal);
        Assert.Single(viewModel.Sources);
        Assert.Equal("included", viewModel.Sources[0].Context);
        Assert.DoesNotContain(viewModel.Sources, source => source.Context == "excluded");
    }

    [Fact]
    public void App_state_imported_context_dedup_preserves_metadata_when_imported_from_different_paths()
    {
        var directory = TempDirectory();
        var first = Path.Combine(directory, "first.yaml");
        var second = Path.Combine(directory, "second.yaml");
        var raw = OneContextKubeconfig("http://127.0.0.1:1", "shared");
        File.WriteAllText(first, raw);
        File.WriteAllText(second, raw);

        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(first);

        var context = Assert.Single(state.Snapshot().ImportedContexts);
        state.RenameImportedContext(context.ContextId, "Shared Alias");
        state.SetImportedContextFilter(context.ContextId, "team-a");

        var secondSummary = state.ImportKubeconfig(second);
        var snapshot = state.Snapshot();

            Assert.Single(snapshot.ImportedContexts);
        Assert.Equal(0, secondSummary.CreatedSessionCount);
        Assert.Equal("Shared Alias", snapshot.ImportedContexts[0].DisplayName);
        Assert.Equal("team-a", snapshot.ImportedContexts[0].FilterName);
        Assert.Equal(context.ContextId, snapshot.ImportedContexts[0].ContextId);
        Assert.Single(snapshot.Sessions);

        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        viewModel.ReloadSessions();

        var source = Assert.Single(viewModel.Sources);
        Assert.Equal("Shared Alias", source.Context);
        Assert.Equal("team-a", source.FilterName);
        Assert.Single(viewModel.Sessions);
    }

    [Fact]
    public void Smart_source_import_scans_directory_with_non_yaml_kubeconfig_extensions()
    {
        var directory = TempDirectory();
        var scanRoot = Path.Combine(directory, "scan");
        Directory.CreateDirectory(scanRoot);
        File.WriteAllText(Path.Combine(scanRoot, "cluster.kubeconfig"), OneContextKubeconfig("http://127.0.0.1:1", "kubeconfig"));
        File.WriteAllText(Path.Combine(scanRoot, "cluster.cfg"), OneContextKubeconfig("http://127.0.0.1:2", "cfg"));
        File.WriteAllText(Path.Combine(scanRoot, "notes.yaml"), "not: kubeconfig");
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        viewModel.ImportPath = scanRoot;
        viewModel.ImportPathNow();

        Assert.Contains("Imported 2 context(s) from 2 kubeconfig file(s); ignored 1", viewModel.StatusLine, StringComparison.Ordinal);
        Assert.Equal(2, viewModel.Sources.Count);
        Assert.Contains(viewModel.Sources, source => source.Context == "kubeconfig");
        Assert.Contains(viewModel.Sources, source => source.Context == "cfg");
    }

    [Fact]
    public void Smart_source_import_scans_directory_with_additional_kubeconfig_extensions()
    {
        var directory = TempDirectory();
        var scanRoot = Path.Combine(directory, "scan");
        Directory.CreateDirectory(scanRoot);
        File.WriteAllText(Path.Combine(scanRoot, "cluster.kube"), OneContextKubeconfig("http://127.0.0.1:1", "kube"));
        File.WriteAllText(Path.Combine(scanRoot, "cluster.config"), OneContextKubeconfig("http://127.0.0.1:2", "config"));
        File.WriteAllText(Path.Combine(scanRoot, "notes.yaml"), "not: kubeconfig");
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        viewModel.ImportPath = scanRoot;
        viewModel.ImportPathNow();

        Assert.Contains("Imported 2 context(s) from 2 kubeconfig file(s); ignored 1", viewModel.StatusLine, StringComparison.Ordinal);
        Assert.Equal(2, viewModel.Sources.Count);
        Assert.Contains(viewModel.Sources, source => source.Context == "kube");
        Assert.Contains(viewModel.Sources, source => source.Context == "config");
    }

    [Fact]
    public async Task Saved_filter_applies_to_cached_resources_immediately()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        using var viewModel = new MainWindowViewModel(
            state,
            new KubernetesResourceService(state, new AppRecordingHandler(request => new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent((request.RequestUri?.AbsolutePath ?? string.Empty).EndsWith("/pods", StringComparison.Ordinal)
                    ? """
                      {"items":[
                        {"metadata":{"name":"api","namespace":"payments","uid":"1","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-a","containers":[{"image":"repo/api:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}},
                        {"metadata":{"name":"worker","namespace":"payments","uid":"2","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-a","containers":[{"image":"repo/worker:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}}
                      ]}
                      """
                    : """{"items":[]}""", Encoding.UTF8, "application/json")
            })));

        viewModel.ReloadSessions();
        viewModel.ProblemsOnly = false;
        viewModel.KindPicker.SetExpression("\"Pod\"");
        await viewModel.RefreshResourcesAsync(force: true);

        var cachedCount = viewModel.Resources.Count;
        Assert.True(cachedCount > 0);
        viewModel.KindPicker.SetExpression("\"DefinitelyMissingKind\"");
        Assert.Empty(viewModel.Resources);
        viewModel.KindPicker.SetExpression(string.Empty);
        Assert.Equal(cachedCount, viewModel.Resources.Count);

        var preset = new FilterPreset("api-only", false, "", "", "", "\"Pod\"", "api", "\"payments\"", "", "", "", "", "", "", "", "", "256");

        viewModel.SelectedPreset = preset;

        Assert.Single(viewModel.Resources);
        Assert.Equal("api", viewModel.Resources[0].Name);
        Assert.Equal("\"Pod\"", viewModel.KindPicker.Expression);
    }

    [Fact]
    public async Task Metric_filters_and_sorting_apply_to_cached_resource_rows_immediately()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        using var viewModel = new MainWindowViewModel(
            state,
            new KubernetesResourceService(state, new AppRecordingHandler(request =>
            {
                var path = request.RequestUri?.AbsolutePath ?? string.Empty;
                var json = path switch
                {
                    "/api/v1/pods" => """
                      {"items":[
                        {"metadata":{"name":"api","namespace":"payments","uid":"1","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-a","containers":[{"image":"repo/api:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}},
                        {"metadata":{"name":"worker","namespace":"payments","uid":"2","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-a","containers":[{"image":"repo/worker:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}}
                      ]}
                      """,
                    "/apis/metrics.k8s.io/v1beta1/pods" => """
                      {"items":[
                        {"metadata":{"name":"api","namespace":"payments"},"containers":[{"name":"api","usage":{"cpu":"250m","memory":"512Mi"}}]},
                        {"metadata":{"name":"worker","namespace":"payments"},"containers":[{"name":"worker","usage":{"cpu":"50m","memory":"64Mi"}}]}
                      ]}
                      """,
                    "/apis/metrics.k8s.io/v1beta1/nodes" => """{"items":[]}""",
                    _ => """{"items":[]}"""
                };
                return new HttpResponseMessage(HttpStatusCode.OK)
                {
                    Content = new StringContent(json, Encoding.UTF8, "application/json")
                };
            })));

        viewModel.ReloadSessions();
        viewModel.ProblemsOnly = false;
        viewModel.KindPicker.SetExpression("\"Pod\"");
        await viewModel.RefreshResourcesAsync(force: true);

        Assert.Equal(["api", "worker"], viewModel.Resources.Select(row => row.Name).ToArray());
        Assert.Contains(viewModel.Resources, row => row.Name == "api" && row.CpuSummaryDisplay == "250m" && row.MemorySummaryDisplay == "512Mi");
        Assert.Contains(viewModel.Resources, row => row.Name == "worker" && row.CpuSummaryDisplay == "50m" && row.MemorySummaryDisplay == "64Mi");

        viewModel.CpuPicker.SetExpression(">100m");

        Assert.Equal(["api"], viewModel.Resources.Select(row => row.Name).ToArray());

        viewModel.CpuPicker.SetExpression(string.Empty);
        viewModel.MemoryPicker.SetExpression(">100Mi");

        Assert.Equal(["api"], viewModel.Resources.Select(row => row.Name).ToArray());

        viewModel.MemoryPicker.SetExpression(string.Empty);
        viewModel.SortResourcesBy("CPU");

        Assert.Equal(["api", "worker"], viewModel.Resources.Select(row => row.Name).ToArray());
        Assert.Equal("SORT CPU DESCENDING", viewModel.ResourceSortLabel);
        Assert.Equal("▼", viewModel.ResourceSortGlyphFor("CPU"));

        viewModel.SortResourcesBy("CPU");

        Assert.Equal(["worker", "api"], viewModel.Resources.Select(row => row.Name).ToArray());
        Assert.Equal("SORT CPU ASCENDING", viewModel.ResourceSortLabel);
        Assert.Equal("▲", viewModel.ResourceSortGlyphFor("CPU"));

        viewModel.SortResourcesBy("CPU");

        Assert.Equal("SORT AGE NEWEST", viewModel.ResourceSortLabel);
        Assert.Equal(string.Empty, viewModel.ResourceSortGlyphFor("CPU"));
    }

    [Fact]
    public async Task Sort_missing_cpu_values_moves_missing_rows_to_expected_side()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        using var viewModel = new MainWindowViewModel(
            state,
            new KubernetesResourceService(state, new AppRecordingHandler(request =>
            {
                var path = request.RequestUri?.AbsolutePath ?? string.Empty;
                var json = path switch
                {
                    "/api/v1/pods" => """
                      {"items":[
                        {"metadata":{"name":"known","namespace":"payments","uid":"1","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-a","containers":[{"image":"repo/api:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}},
                        {"metadata":{"name":"missing","namespace":"payments","uid":"2","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-b","containers":[{"image":"repo/worker:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}}
                      ]}
                      """,
                    "/apis/metrics.k8s.io/v1beta1/pods" => """
                      {"items":[
                        {"metadata":{"name":"known","namespace":"payments"},"containers":[{"name":"known","usage":{"cpu":"100m","memory":"128Mi"}}]}
                      ]}
                      """,
                    "/apis/metrics.k8s.io/v1beta1/nodes" => """{"items":[]}""",
                    _ => """{"items":[]}"""
                };
                return new HttpResponseMessage(HttpStatusCode.OK)
                {
                    Content = new StringContent(json, Encoding.UTF8, "application/json")
                };
            })));

        viewModel.ReloadSessions();
        viewModel.KindPicker.SetExpression("\"Pod\"");
        await viewModel.RefreshResourcesAsync(force: true);

        viewModel.SortResourcesBy("CPU");
        Assert.Equal(["known", "missing"], viewModel.Resources.Select(row => row.Name).ToArray());

        viewModel.SortResourcesBy("CPU");
        Assert.Equal(["missing", "known"], viewModel.Resources.Select(row => row.Name).ToArray());
    }

    [Fact]
    public async Task Source_switch_shows_cached_rows_immediately_and_loading_feedback_for_cold_sources()
    {
        var directory = TempDirectory();
        var devConfig = Path.Combine(directory, "dev.yaml");
        var prodConfig = Path.Combine(directory, "prod.yaml");
        File.WriteAllText(devConfig, OneContextKubeconfig("https://127.0.0.1:6443", "dev"));
        File.WriteAllText(prodConfig, OneContextKubeconfig("https://127.0.0.1:6443", "prod"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(devConfig);
        state.ImportKubeconfig(prodConfig);
        var service = new KubernetesResourceService(state, new AppRecordingHandler(request =>
        {
            var path = request.RequestUri?.AbsolutePath ?? string.Empty;
            return new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent(path.EndsWith("/pods", StringComparison.Ordinal)
                    ? """
                      {"items":[
                        {"metadata":{"name":"cached-api","namespace":"payments","uid":"cached-1","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-a","containers":[{"image":"repo/api:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}}
                      ]}
                      """
                    : """{"items":[]}""", Encoding.UTF8, "application/json")
            };
        }));
        var devSession = state.ListSessions().Single(session => session.DisplayName == "dev");
        var prodSession = state.ListSessions().Single(session => session.DisplayName == "prod");
        await service.WarmResourceCacheAsync(new ResourceQuery(devSession.Id, Kind: "\"Pod\"", ForceRefresh: true), KubernetesRequestPriority.UserVisible);
        using var viewModel = new MainWindowViewModel(state, service);

        viewModel.ReloadSessions();
        viewModel.SelectedSession = viewModel.Sessions.Single(session => session.Id == devSession.Id);

        Assert.Contains(viewModel.Resources, row => row.Name == "cached-api");
        Assert.Contains("Showing cached resources for dev", viewModel.StatusLine, StringComparison.Ordinal);
        Assert.True(viewModel.IsRefreshing);

        viewModel.SelectedSession = viewModel.Sessions.Single(session => session.Id == prodSession.Id);

        Assert.Empty(viewModel.Resources);
        Assert.Contains("Loading resources for prod", viewModel.StatusLine, StringComparison.Ordinal);
        Assert.True(viewModel.IsRefreshing);
    }

    [Fact]
    public async Task Source_switch_between_cached_sessions_uses_cached_rows_without_refresh()
    {
        var directory = TempDirectory();
        var devConfig = Path.Combine(directory, "dev.yaml");
        var prodConfig = Path.Combine(directory, "prod.yaml");
        File.WriteAllText(devConfig, OneContextKubeconfig("https://127.0.0.1:6443", "dev"));
        File.WriteAllText(prodConfig, OneContextKubeconfig("https://127.0.0.1:6443", "prod"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(devConfig);
        state.ImportKubeconfig(prodConfig);
        var handler = new AppRecordingHandler(request =>
        {
            var path = request.RequestUri?.AbsolutePath ?? string.Empty;
            return new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent(path.EndsWith("/pods", StringComparison.Ordinal)
                    ? """
                      {"items":[{"metadata":{"name":"cached-api","namespace":"payments","uid":"cached-1","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-a","containers":[{"image":"repo/api:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}}]}
                      """
                    : """{"items":[]}""", Encoding.UTF8, "application/json")
            };
        });
        var service = new KubernetesResourceService(state, handler);
        var devSession = state.ListSessions().Single(session => session.DisplayName == "dev");
        var prodSession = state.ListSessions().Single(session => session.DisplayName == "prod");

        await service.WarmResourceCacheAsync(new ResourceQuery(devSession.Id, Kind: "\"Pod\" \"Event\"", ForceRefresh: true), KubernetesRequestPriority.UserVisible);
        await service.WarmResourceCacheAsync(new ResourceQuery(prodSession.Id, Kind: "\"Pod\" \"Event\"", ForceRefresh: true), KubernetesRequestPriority.UserVisible);
        var networkCallsAfterWarm = handler.Requests.Count;

        using var viewModel = new MainWindowViewModel(state, service);

        viewModel.ReloadSessions();
        viewModel.ProblemsOnly = false;
        viewModel.KindPicker.SetExpression("\"Pod\"");
        viewModel.SelectedSession = viewModel.Sessions.Single(session => session.Id == devSession.Id);
        await viewModel.RefreshResourcesAsync();
        Assert.DoesNotContain("Loading resources for", viewModel.StatusLine, StringComparison.Ordinal);
        Assert.Equal(networkCallsAfterWarm, handler.Requests.Count);

        viewModel.SelectedSession = viewModel.Sessions.Single(session => session.Id == prodSession.Id);
        await viewModel.RefreshResourcesAsync();

        Assert.DoesNotContain("Loading resources for", viewModel.StatusLine, StringComparison.Ordinal);
        Assert.True(viewModel.Resources.Count > 0);
        Assert.Equal(networkCallsAfterWarm, handler.Requests.Count);
    }

    [Fact]
    public async Task Refresh_resources_with_identical_query_state_does_not_double_request_under_concurrent_calls()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));

        var baselineState = AppState.InMemoryWithConfigDirectory(directory);
        baselineState.ImportKubeconfig(kubeconfig);
        var baselineHandler = new AsyncAppRecordingHandler(async (_, cancellationToken) =>
        {
            await Task.Delay(180, cancellationToken).ConfigureAwait(false);
            return new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent("""{"items":[]}""", Encoding.UTF8, "application/json")
            };
        });
        using (var baselineViewModel = new MainWindowViewModel(
            baselineState,
            new KubernetesResourceService(baselineState, baselineHandler)))
        {
            baselineViewModel.ReloadSessions();
            await baselineViewModel.RefreshResourcesAsync(force: true);
        }

        var comparisonState = AppState.InMemoryWithConfigDirectory(directory);
        comparisonState.ImportKubeconfig(kubeconfig);
        var replayHandler = new AsyncAppRecordingHandler(async (_, cancellationToken) =>
        {
            await Task.Delay(180, cancellationToken).ConfigureAwait(false);
            return new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent("""{"items":[]}""", Encoding.UTF8, "application/json")
            };
        });
        using var viewModel = new MainWindowViewModel(
            comparisonState,
            new KubernetesResourceService(comparisonState, replayHandler));

        viewModel.ReloadSessions();
        var firstRefresh = viewModel.RefreshResourcesAsync(force: true);
            await Task.Delay(20);
        var duplicateRefresh = viewModel.RefreshResourcesAsync();

        await Task.WhenAll(firstRefresh, duplicateRefresh);
        await Task.Delay(800);

        Assert.Equal(baselineHandler.Requests.Count, replayHandler.Requests.Count);
    }

    [Fact]
    public async Task Blank_local_filter_expressions_clear_immediately_without_remote_roundtrip()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        var handler = new AppRecordingHandler(request =>
        {
            var path = request.RequestUri?.AbsolutePath ?? string.Empty;
            if (path.EndsWith("/pods", StringComparison.Ordinal))
            {
                return new HttpResponseMessage(HttpStatusCode.OK)
                {
                    Content = new StringContent(
                        """
                        {"items":[
                          {"metadata":{"name":"api","namespace":"payments","uid":"api","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-a","containers":[{"image":"repo/api:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}},
                          {"metadata":{"name":"worker","namespace":"payments","uid":"worker","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-b","containers":[{"image":"repo/worker:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}}
                        ]}
                        """,
                        Encoding.UTF8,
                        "application/json")
                };
            }

            if (path.Contains("/metrics/", StringComparison.Ordinal))
            {
                return new HttpResponseMessage(HttpStatusCode.OK)
                {
                    Content = new StringContent("""{"items":[]}""", Encoding.UTF8, "application/json")
                };
            }

            return new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent("""{"items":[]}""", Encoding.UTF8, "application/json")
            };
        });
        using var viewModel = new MainWindowViewModel(
            state,
            new KubernetesResourceService(state, handler));

        viewModel.ReloadSessions();
        viewModel.KindPicker.SetExpression("\"Pod\"");
        await viewModel.RefreshResourcesAsync(force: true);

        Assert.True(viewModel.Resources.Count >= 2, "Pods should be loaded from the cached display query.");
        var priorRequests = handler.Requests.Count;

        viewModel.NamePicker.SetExpression("\"api\"");

        Assert.Single(viewModel.Resources);
        Assert.Equal("api", viewModel.Resources[0].Name);
        Assert.Equal(priorRequests, handler.Requests.Count);

        viewModel.NamePicker.SetExpression("  ");

        Assert.Equal(2, viewModel.Resources.Count);
        Assert.Equal(priorRequests, handler.Requests.Count);
    }

    [Fact]
    public async Task Selecting_from_table_radar_and_graph_keeps_exactly_one_active_surface_selected()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        using var viewModel = new MainWindowViewModel(
            state,
            new KubernetesResourceService(state, new AppRecordingHandler(request =>
            {
                var path = request.RequestUri?.AbsolutePath ?? string.Empty;
                return path switch
                {
                    "/api/v1/pods" =>
                        new HttpResponseMessage(HttpStatusCode.OK)
                        {
                            Content = new StringContent(
                                """
                                {"items":[
                                  {"metadata":{"name":"api","namespace":"payments","uid":"1","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-a","containers":[{"image":"repo/api:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}},
                                  {"metadata":{"name":"worker","namespace":"payments","uid":"2","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-a","containers":[{"image":"repo/worker:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}}
                                ]}
                                """,
                                Encoding.UTF8,
                                "application/json")
                        },
                    "/apis/metrics.k8s.io/v1beta1/pods" =>
                        new HttpResponseMessage(HttpStatusCode.OK)
                        {
                            Content = new StringContent("""{"items":[]}""", Encoding.UTF8, "application/json")
                        },
                    "/apis/metrics.k8s.io/v1beta1/nodes" =>
                        new HttpResponseMessage(HttpStatusCode.OK)
                        {
                            Content = new StringContent("""{"items":[]}""", Encoding.UTF8, "application/json")
                        },
                    _ =>
                        new HttpResponseMessage(HttpStatusCode.OK)
                        {
                            Content = new StringContent("""{"items":[]}""", Encoding.UTF8, "application/json")
                        }
                };
            })));

        viewModel.ReloadSessions();
        viewModel.KindPicker.SetExpression("\"Pod\"");
        await viewModel.RefreshResourcesAsync(force: true);

        var api = Assert.Single(viewModel.Resources, row => row.Name == "api");
        viewModel.SelectedResourceRow = api;

        Assert.Same(api, viewModel.SelectedResource);
        Assert.Same(api, viewModel.SelectedResourceRow);
        Assert.Null(viewModel.SelectedGraphNode);
        Assert.Contains(viewModel.FocusMetrics, row =>
            row.Label == "Created"
            && Regex.IsMatch(row.Value, @"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{2}:\d{2}$"));

        viewModel.ResourceQuickSearch = "worker";
        viewModel.NextResourceMatch();

        Assert.Null(viewModel.SelectedGraphNode);
        Assert.Equal("worker", viewModel.SelectedResource?.Name);

        var cluster = Assert.Single(viewModel.RadarBlocks, block => block.DisplayKind == "Cluster").Resource;
        await viewModel.FocusRadarResourceAsync(cluster);

        Assert.Equal("Cluster", viewModel.SelectedResource?.Kind);
        Assert.Null(viewModel.SelectedResourceRow);
        Assert.Null(viewModel.SelectedGraphNode);

        await viewModel.FocusGraphNodeAsync(new GraphNodeViewModel("Namespace", "payments", "cluster", "Ready", null));

        Assert.Null(viewModel.SelectedResource);
        Assert.Null(viewModel.SelectedResourceRow);
        Assert.NotNull(viewModel.SelectedGraphNode);
    }

    [Fact]
    public async Task Events_workspace_maps_event_fields_and_click_focuses_inspector()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        var clock = new MutableClock(new DateTimeOffset(2026, 6, 10, 8, 2, 0, TimeSpan.Zero));
        using var viewModel = new MainWindowViewModel(
            state,
            new KubernetesResourceService(state, new AppRecordingHandler(request =>
            {
                var path = request.RequestUri?.AbsolutePath ?? string.Empty;
                return new HttpResponseMessage(HttpStatusCode.OK)
                {
                    Content = new StringContent(path.EndsWith("/events", StringComparison.Ordinal)
                        ? """
                          {"items":[
                            {
                              "metadata":{"name":"api.17f4","namespace":"payments","uid":"event-1","creationTimestamp":"2026-06-10T08:00:00Z"},
                              "lastTimestamp":"2026-06-10T08:01:00Z",
                              "type":"Warning",
                              "reason":"FailedScheduling",
                              "message":"0/2 nodes are available: insufficient cpu",
                              "involvedObject":{"kind":"Pod","name":"api-7f9d"}
                            },
                            {
                              "metadata":{"name":"api.17f3","namespace":"payments","uid":"event-0","creationTimestamp":"2026-06-10T07:59:00Z"},
                              "lastTimestamp":"2026-06-10T08:00:00Z",
                              "type":"Normal",
                              "reason":"Pulled",
                              "message":"Successfully pulled image",
                              "involvedObject":{"kind":"Pod","name":"api-7f9d"}
                            }
                          ]}
                          """
                        : """{"items":[]}""", Encoding.UTF8, "application/json")
                };
            }), clock));

        viewModel.ReloadSessions();
        viewModel.ProblemsOnly = false;
        viewModel.KindPicker.SetExpression("\"Pod\"");
        await viewModel.RefreshResourcesAsync(force: true);

        Assert.Equal(["api.17f4", "api.17f3"], viewModel.Events.Select(row => row.Name).ToArray());
        var timeline = viewModel.Events[0];
        Assert.Equal("Warning", timeline.Status);
        Assert.Equal("Warning", timeline.Type);
        Assert.Equal("api.17f4", timeline.Name);
        Assert.Equal("FailedScheduling", timeline.Reason);
        Assert.Equal("Pod/api-7f9d", timeline.Object);
        Assert.Equal("payments", timeline.Namespace);
        Assert.Equal("0/2 nodes are available: insufficient cpu", timeline.Message);

        viewModel.SortEventsBy("Age");

        Assert.Equal(["api.17f3", "api.17f4"], viewModel.Events.Select(row => row.Name).ToArray());
        Assert.Equal("SORT AGE DESCENDING", viewModel.EventSortLabel);

        viewModel.SortEventsBy("Age");

        Assert.Equal(["api.17f4", "api.17f3"], viewModel.Events.Select(row => row.Name).ToArray());
        Assert.Equal("SORT AGE ASCENDING", viewModel.EventSortLabel);

        viewModel.SortEventsBy("Age");

        Assert.Equal(["api.17f4", "api.17f3"], viewModel.Events.Select(row => row.Name).ToArray());
        Assert.Equal("SORT AGE NEWEST", viewModel.EventSortLabel);

        viewModel.SelectedEvent = timeline;

        Assert.True(viewModel.IsInspectorVisible);
        Assert.Equal("Event", viewModel.SelectedResource?.Kind);
        Assert.Equal("api.17f4", viewModel.SelectedResource?.Name);
        Assert.Equal("Pod/api-7f9d", viewModel.SelectedResource?.Owner);
    }

    [Fact]
    public async Task Events_workspace_reuses_existing_states_for_resolved_and_historical_events()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        var clock = new MutableClock(new DateTimeOffset(2026, 6, 10, 9, 0, 0, TimeSpan.Zero));
        using var viewModel = new MainWindowViewModel(
            state,
            new KubernetesResourceService(state, new AppRecordingHandler(request =>
            {
                var path = request.RequestUri?.AbsolutePath ?? string.Empty;
                var json = path.EndsWith("/events", StringComparison.Ordinal)
                    ? """
                      {"items":[
                        {
                          "metadata":{"name":"api-old-warning","namespace":"payments","uid":"event-old","creationTimestamp":"2026-06-10T08:00:00Z"},
                          "lastTimestamp":"2026-06-10T08:10:00Z",
                          "type":"Warning",
                          "reason":"BackOff",
                          "message":"Back-off restarting failed container",
                          "involvedObject":{"kind":"Pod","name":"api"}
                        },
                        {
                          "metadata":{"name":"orphan-old-warning","namespace":"payments","uid":"event-orphan","creationTimestamp":"2026-06-10T08:00:00Z"},
                          "lastTimestamp":"2026-06-10T08:00:00Z",
                          "type":"Warning",
                          "reason":"FailedMount",
                          "message":"volume mount failed",
                          "involvedObject":{"kind":"Pod","name":"missing"}
                        }
                      ]}
                      """
                    : path.EndsWith("/pods", StringComparison.Ordinal)
                        ? """
                          {"items":[
                            {"metadata":{"name":"api","namespace":"payments","uid":"pod-1","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-a","containers":[{"image":"repo/api:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}}
                          ]}
                          """
                        : """{"items":[]}""";
                return new HttpResponseMessage(HttpStatusCode.OK)
                {
                    Content = new StringContent(json, Encoding.UTF8, "application/json")
                };
            }), clock));

        viewModel.ReloadSessions();
        viewModel.ProblemsOnly = false;
        await viewModel.RefreshResourcesAsync(force: true);

        var resolved = Assert.Single(viewModel.Events, row => row.Name == "api-old-warning");
        var historical = Assert.Single(viewModel.Events, row => row.Name == "orphan-old-warning");

        Assert.Equal("Succeeded", resolved.Type);
        Assert.Equal("Observed", historical.Type);
        Assert.DoesNotContain(viewModel.Resources, row => row.Kind == "Event" && row.Name == "api-old-warning" && ResourceFilterMatcher.IsProblem(row));
        Assert.DoesNotContain(viewModel.Resources, row => row.Kind == "Event" && row.Name == "orphan-old-warning" && ResourceFilterMatcher.IsProblem(row));

        viewModel.SelectedPreset = new FilterPreset("activity", false, "", "", "", "", "", "", "", "", "", "", "", "", "", "", "256", ActivityOnly: true);

        Assert.DoesNotContain(viewModel.Resources, row => row.Kind == "Event" && row.Name is "api-old-warning" or "orphan-old-warning");
    }

    [Fact]
    public async Task Copy_menu_reference_opening_resolves_known_cached_resources()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        using var viewModel = new MainWindowViewModel(
            state,
            new KubernetesResourceService(state, new AppRecordingHandler(_ => new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent("""
                {"items":[
                  {"metadata":{"name":"api","namespace":"payments","uid":"1","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-a","containers":[{"image":"repo/api:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}},
                  {"metadata":{"name":"worker","namespace":"payments","uid":"2","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-a","containers":[{"image":"repo/worker:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}}
                ]}
                """, Encoding.UTF8, "application/json")
            })));

        viewModel.ReloadSessions();
        viewModel.ProblemsOnly = false;
        viewModel.KindPicker.SetExpression("\"Pod\"");
        await viewModel.RefreshResourcesAsync(force: true);

        Assert.True(viewModel.HasKnownResourceReference("Pod/api"));
        Assert.True(viewModel.HasKnownResourceReference("pods/api"));
        Assert.True(viewModel.HasKnownResourceReference("payments/worker"));
        Assert.True(viewModel.HasKnownResourceReference(viewModel.Resources[0].Id));
        Assert.False(viewModel.HasKnownResourceReference("Running"));

        Assert.True(viewModel.OpenKnownResourceReference("payments/worker"));
        Assert.Equal("worker", viewModel.SelectedResource?.Name);
        Assert.False(viewModel.OpenKnownResourceReference("missing"));
    }

    [Fact]
    public async Task Radar_map_builds_dependency_island_from_cached_resource_rows()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        using var viewModel = new MainWindowViewModel(
            state,
            new KubernetesResourceService(state, new AppRecordingHandler(request => new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent(RadarFixtureFor(request.RequestUri?.AbsolutePath ?? string.Empty), Encoding.UTF8, "application/json")
            })));

        viewModel.ReloadSessions();
        viewModel.ProblemsOnly = false;
        viewModel.KindPicker.SetExpression("\"Pod\" \"ReplicaSet\"");
        await viewModel.RefreshResourcesAsync(force: true);

        Assert.Contains("dev", viewModel.RadarSourceLabel, StringComparison.Ordinal);
        var cluster = Assert.Single(viewModel.RadarBlocks, block => block.DisplayKind == "Cluster");
        var namespaces = viewModel.RadarBlocks.Where(block => block.DisplayKind == "Namespace").ToList();
        var pod = Assert.Single(viewModel.RadarBlocks, block => block.DisplayKind == "Pod" && block.Resource.Name == "api-1");
        var replicaSet = Assert.Single(viewModel.RadarBlocks, block => block.DisplayKind == "ReplicaSet");

        Assert.All(viewModel.RadarBlocks, block => Assert.True(block.IsClickable));
        Assert.Contains(viewModel.RadarBlocks, block => block.IsAnnouncing);
        Assert.Contains(viewModel.RadarBlocks, block => block.IsAnnouncing && !ReferenceEquals(block.Brush, block.AnnounceBrush));
        Assert.All(viewModel.RadarBlocks, block => Assert.Equal(5.5, block.Width, precision: 2));
        Assert.All(viewModel.RadarBlocks, block => Assert.Equal(5.5, block.Height, precision: 2));
        Assert.All(viewModel.RadarBlocks, block => Assert.True(IsRadarGridAligned(block.X, 240)));
        Assert.All(viewModel.RadarBlocks, block => Assert.True(IsRadarGridAligned(block.Y, 100)));
        Assert.Equal(
            viewModel.RadarBlocks.Count,
            viewModel.RadarBlocks.Select(block => $"{block.X:0.###}:{block.Y:0.###}").Distinct(StringComparer.Ordinal).Count());
        Assert.InRange(Distance(cluster, namespaces[0]), 10, 45);
        Assert.True(Distance(namespaces.First(block => block.Resource.Name == "payments"), pod) > Distance(cluster, namespaces[0]));
        Assert.NotSame(cluster.Brush, namespaces[0].Brush);
        Assert.NotSame(namespaces[0].Brush, replicaSet.Brush);
        Assert.NotSame(replicaSet.Brush, pod.Brush);

        var positionsBeforeFilter = viewModel.RadarBlocks.ToDictionary(
            block => block.Resource.Id,
            block => (block.X, block.Y, block.Width, block.Height),
            StringComparer.Ordinal);

        viewModel.SelectedPreset = new FilterPreset("api-only", false, "", "", "", "", "api-1", "", "", "", "", "", "", "", "", "", "256");

        Assert.Equal(positionsBeforeFilter.Count, viewModel.RadarBlocks.Count);
        foreach (var block in viewModel.RadarBlocks)
        {
            var position = positionsBeforeFilter[block.Resource.Id];
            Assert.Equal(position.X, block.X, precision: 2);
            Assert.Equal(position.Y, block.Y, precision: 2);
            Assert.Equal(position.Width, block.Width, precision: 2);
            Assert.Equal(position.Height, block.Height, precision: 2);
        }

        Assert.False(viewModel.RadarBlocks.Single(block => block.DisplayKind == "Cluster").IsDimmed);
        Assert.False(viewModel.RadarBlocks.Single(block => block.DisplayKind == "Namespace" && block.Resource.Name == "payments").IsDimmed);
        Assert.False(viewModel.RadarBlocks.Single(block => block.DisplayKind == "Pod" && block.Resource.Name == "api-1").IsDimmed);
        Assert.True(viewModel.RadarBlocks.Single(block => block.DisplayKind == "Pod" && block.Resource.Name == "worker-1").IsDimmed);
        Assert.True(viewModel.RadarBlocks.Single(block => block.DisplayKind == "ReplicaSet").IsDimmed);

        viewModel.SelectedResourceRow = viewModel.Resources.Single(row => row.Name == "api-1");

        Assert.True(viewModel.RadarBlocks.Single(block => block.Resource.Name == "api-1").IsSelected);
        Assert.False(viewModel.RadarBlocks.Single(block => block.Resource.Name == "worker-1").IsSelected);

        await viewModel.FocusRadarResourceAsync(cluster.Resource);

        Assert.True(viewModel.IsInspectorVisible);
        Assert.False(viewModel.IsDetailLoading);
        Assert.Equal("Cluster", viewModel.SelectedResource?.Kind);

        var clusterX = cluster.X;
        var clusterY = cluster.Y;
        viewModel.PanRadar(14, -7);

        Assert.Equal(clusterX + 14, cluster.X, precision: 2);
        Assert.Equal(clusterY - 7, cluster.Y, precision: 2);
        Assert.Equal(14, viewModel.RadarPanX, precision: 2);
        Assert.Equal(-7, viewModel.RadarPanY, precision: 2);

        var visibleBeforeZoom = viewModel.RadarBlocks.Count;
        viewModel.ZoomRadar(-1);

        Assert.True(viewModel.RadarZoom < 1);
        Assert.All(viewModel.RadarBlocks, block => Assert.True(block.Width < 5.5));
        Assert.True(viewModel.RadarBlocks.Count >= visibleBeforeZoom);

        viewModel.ZoomRadarIn();
        Assert.InRange(viewModel.RadarZoom, 0.99, 1.01);

        viewModel.ResetRadarView();

        Assert.Equal(1, viewModel.RadarZoom, precision: 2);
        Assert.Equal(0, viewModel.RadarPanX, precision: 2);
        Assert.Equal(0, viewModel.RadarPanY, precision: 2);
    }

    [Fact]
    public async Task Radar_alert_blocks_reenter_announcing_state_on_reappearing_problem_rows()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        var alertCycle = 0;
        using var viewModel = new MainWindowViewModel(
            state,
            new KubernetesResourceService(state, new AppRecordingHandler(request =>
            {
                var path = request.RequestUri?.AbsolutePath ?? string.Empty;
                if (!path.EndsWith("/pods", StringComparison.Ordinal))
                {
                    return new HttpResponseMessage(HttpStatusCode.OK)
                    {
                        Content = new StringContent("""{"items":[]}""", Encoding.UTF8, "application/json")
                    };
                }

                var status = alertCycle switch
                {
                    0 => "CrashLoopBackOff",
                    1 => "Running",
                    _ => "CrashLoopBackOff"
                };

                return new HttpResponseMessage(HttpStatusCode.OK)
                {
                    Content = new StringContent(
                        """
                        {"items":[
                          {"metadata":{"name":"api-1","namespace":"payments","uid":"pod-1","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-a","containers":[{"image":"repo/api:1"}]},"status":{"phase":"__STATUS__","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}}
                        ]}
                        """.Replace("__STATUS__", status, StringComparison.Ordinal),
                        Encoding.UTF8,
                        "application/json")
                };
            })));

        viewModel.ReloadSessions();
        viewModel.KindPicker.SetExpression("\"Pod\"");
        await viewModel.RefreshResourcesAsync(force: true);

        var initial = Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "api-1");
        Assert.True(initial.IsAnnouncing);

        alertCycle = 1;
        await viewModel.RefreshResourcesAsync(force: true);
        var healthy = Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "api-1");
        Assert.Equal("Running", healthy.Resource.Status);
        Assert.True(healthy.IsAnnouncing);

        alertCycle = 2;
        await viewModel.RefreshResourcesAsync(force: true);
        var returning = Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "api-1");
        Assert.True(returning.IsAnnouncing);
    }

    [Fact]
    public async Task Resource_and_radar_views_remove_old_resources_and_add_new_ones_after_refresh()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        var secondWave = false;
        using var viewModel = new MainWindowViewModel(
            state,
            new KubernetesResourceService(state, new AppRecordingHandler(request => new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent(DynamicPodFixture(request.RequestUri?.AbsolutePath ?? string.Empty, secondWave), Encoding.UTF8, "application/json")
            })));

        viewModel.ReloadSessions();
        viewModel.ProblemsOnly = false;
        viewModel.KindPicker.SetExpression("\"Pod\"");
        await viewModel.RefreshResourcesAsync(force: true);

        Assert.True(viewModel.RadarWaterActivityRate > 0);
        Assert.Contains(viewModel.Resources, row => row.Kind == "Pod" && row.Name == "api-old");
        Assert.Contains(viewModel.RadarBlocks, block => block.DisplayKind == "Pod" && block.Resource.Name == "api-old");

        secondWave = true;
        await viewModel.RefreshResourcesAsync(force: true);

        Assert.DoesNotContain(viewModel.Resources, row => row.Kind == "Pod" && row.Name == "api-old");
        Assert.DoesNotContain(viewModel.RadarBlocks, block => block.DisplayKind == "Pod" && block.Resource.Name == "api-old");
        Assert.Contains(viewModel.Resources, row => row.Kind == "Pod" && row.Name == "api-new");
        Assert.Contains(viewModel.RadarBlocks, block => block.DisplayKind == "Pod" && block.Resource.Name == "api-new");

        static string DynamicPodFixture(string path, bool secondWave)
        {
            if (!path.EndsWith("/pods", StringComparison.Ordinal))
            {
                return """{"items":[]}""";
            }

            var name = secondWave ? "api-new" : "api-old";
            var uid = secondWave ? "pod-new" : "pod-old";
            return """
            {"items":[
              {"metadata":{"name":"__NAME__","namespace":"payments","uid":"__UID__","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-a","containers":[{"image":"repo/api:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}}
            ]}
            """.Replace("__NAME__", name, StringComparison.Ordinal).Replace("__UID__", uid, StringComparison.Ordinal);
        }
    }

    [Fact]
    public void Brush_converters_map_status_problem_value_and_radar_colors()
    {
        var status = new StatusBrushConverter();
        var problem = new ProblemBrushConverter();
        var reason = new ProblemReasonConverter();
        var deterministic = new DeterministicBrushConverter();
        var hasValue = new HasValueConverter();
        var radar = new RadarBrushConverter();
        var healthy = Row("Running", "healthy", 0, "1/1");
        var broken = Row("CrashLoopBackOff", "broken", 4, "0/1");

        Assert.Equal(AppThemeCatalog.StatusBrush("HEALTHY").ToString(), status.Convert("Running", typeof(IBrush), null, CultureInfo.InvariantCulture).ToString());
        Assert.Equal(AppThemeCatalog.StatusBrush("WARNING").ToString(), status.Convert("Pending", typeof(IBrush), null, CultureInfo.InvariantCulture).ToString());
        Assert.Equal(AppThemeCatalog.StatusBrush("CRITICAL").ToString(), status.Convert("Failed", typeof(IBrush), null, CultureInfo.InvariantCulture).ToString());
        Assert.Equal(AppThemeCatalog.StatusBrush("UNKNOWN").ToString(), status.Convert("Custom", typeof(IBrush), null, CultureInfo.InvariantCulture).ToString());
        Assert.Equal(AppThemeCatalog.StatusBrush("HEALTHY").ToString(), problem.Convert(healthy, typeof(IBrush), null, CultureInfo.InvariantCulture).ToString());
        Assert.Equal(AppThemeCatalog.StatusBrush("CRITICAL").ToString(), problem.Convert(broken, typeof(IBrush), null, CultureInfo.InvariantCulture).ToString());
        Assert.Equal("Restarted 4", reason.Convert(broken, typeof(string), null, CultureInfo.InvariantCulture));
        Assert.True(hasValue.Convert("node-a", typeof(bool), null, CultureInfo.InvariantCulture) is true);
        Assert.False(hasValue.Convert(" ", typeof(bool), null, CultureInfo.InvariantCulture) is true);
        Assert.Equal(
            deterministic.Convert("node-a", typeof(IBrush), null, CultureInfo.InvariantCulture).ToString(),
            deterministic.Convert("node-a", typeof(IBrush), null, CultureInfo.InvariantCulture).ToString());
        Assert.Same(Brushes.Transparent, deterministic.Convert("-", typeof(IBrush), null, CultureInfo.InvariantCulture));
        Assert.Same(Brushes.Transparent, radar.Convert(null, typeof(IBrush), null, CultureInfo.InvariantCulture));
        Assert.Equal(AppThemeCatalog.StatusBrush("CRITICAL").ToString(), radar.Convert(broken, typeof(IBrush), null, CultureInfo.InvariantCulture).ToString());
        Assert.Equal(AppThemeCatalog.StatusBrush("WARNING").ToString(), radar.Convert(Row("Pending", "pending", 0, "-"), typeof(IBrush), null, CultureInfo.InvariantCulture).ToString());
        Assert.NotSame(Brushes.Transparent, radar.Convert(healthy, typeof(IBrush), null, CultureInfo.InvariantCulture));
    }

    [Theory]
    [InlineData("Available", "green")]
    [InlineData("Complete", "green")]
    [InlineData("Ready", "green")]
    [InlineData("Running", "green")]
    [InlineData("Succeeded", "green")]
    [InlineData("Observed", "green")]
    [InlineData("Pending", "yellow")]
    [InlineData("Terminating", "yellow")]
    [InlineData("Suspended", "yellow")]
    [InlineData("Warning", "yellow")]
    [InlineData("CrashLoopBackOff", "red")]
    [InlineData("CreateContainerConfigError", "red")]
    [InlineData("CreateContainerError", "red")]
    [InlineData("ErrImagePull", "red")]
    [InlineData("Error", "red")]
    [InlineData("Failed", "red")]
    [InlineData("ImagePullBackOff", "red")]
    [InlineData("NotReady", "red")]
    [InlineData("OOMKilled", "red")]
    [InlineData("Unavailable", "red")]
    [InlineData("Mystery", "muted")]
    public void Status_converter_covers_known_operational_states(string input, string bucket)
    {
        var converted = new StatusBrushConverter().Convert(input, typeof(IBrush), null, CultureInfo.InvariantCulture);

        Assert.Equal((bucket switch
        {
            "green" => AppThemeCatalog.StatusBrush("HEALTHY"),
            "yellow" => AppThemeCatalog.StatusBrush("WARNING"),
            "red" => AppThemeCatalog.StatusBrush("CRITICAL"),
            _ => AppThemeCatalog.StatusBrush("UNKNOWN")
        }).ToString(), converted.ToString());
    }

    [Fact]
    public void Brush_converters_are_one_way()
    {
        Assert.Throws<NotSupportedException>(() => new StatusBrushConverter().ConvertBack(null, typeof(string), null, CultureInfo.InvariantCulture));
        Assert.Throws<NotSupportedException>(() => new ProblemBrushConverter().ConvertBack(null, typeof(string), null, CultureInfo.InvariantCulture));
        Assert.Throws<NotSupportedException>(() => new ProblemReasonConverter().ConvertBack(null, typeof(string), null, CultureInfo.InvariantCulture));
        Assert.Throws<NotSupportedException>(() => new DeterministicBrushConverter().ConvertBack(null, typeof(string), null, CultureInfo.InvariantCulture));
        Assert.Throws<NotSupportedException>(() => new HasValueConverter().ConvertBack(null, typeof(string), null, CultureInfo.InvariantCulture));
        Assert.Throws<NotSupportedException>(() => new RadarBrushConverter().ConvertBack(null, typeof(string), null, CultureInfo.InvariantCulture));
    }

    [Fact]
    public void Workspace_models_raise_changes_and_update_radar_blocks()
    {
        var graph = new GraphNodeViewModel("Pod", "api", "payments", "Running");
        var graphChanged = new List<string?>();
        graph.PropertyChanged += (_, args) => graphChanged.Add(args.PropertyName);

        graph.IsSearchMatch = true;
        graph.IsCurrentSearchMatch = true;

        Assert.Contains(nameof(GraphNodeViewModel.BorderBrush), graphChanged);
        Assert.Contains(nameof(GraphNodeViewModel.BackgroundBrush), graphChanged);
        Assert.Equal(AppThemeCatalog.StatusBrush("WARNING").ToString(), graph.BorderBrush.ToString());

        var idle = new RadarIdleCellViewModel(1, 2, 3, 4, Brushes.Gold);
        var idleChanged = new List<string?>();
        idle.PropertyChanged += (_, args) => idleChanged.Add(args.PropertyName);
        idle.UpdateFrom(new RadarIdleCellViewModel(5, 6, 7, 8, Brushes.LimeGreen));
        Assert.Equal(5, idle.X);
        Assert.Contains(nameof(RadarIdleCellViewModel.Brush), idleChanged);

        var source = new RadarBlockViewModel(Row("Running", "api", 0, "1/1"), "pods", 1, 2, 3, 4, Brushes.Gold, "", "ok");
        var target = new RadarBlockViewModel(Row("Pending", "queue", 0, "0/1"), "jobs", 9, 8, 7, 6, Brushes.OrangeRed, "Pending", "bad", true);
        var blockChanged = false;
        target.PropertyChanged += (_, args) => blockChanged = args.PropertyName == string.Empty;
        target.UpdateFrom(source);

        Assert.True(blockChanged);
        Assert.Equal("Pod/api", target.ToolTipTitle);
        Assert.Equal("payments", target.ToolTipNamespace);
        Assert.False(target.IsPlaceholder);

        var placeholder = new RadarBlockViewModel(Row("Running", "idle", 0, "1/1"), "idle-sector", 0, 0, 1, 1, Brushes.LimeGreen, "", "", true);
        Assert.Equal("RADAR IDLE CELL", placeholder.ToolTipTitle);
        Assert.Equal("idle-sector", placeholder.ToolTipNamespace);

        var warningBlock = new RadarBlockViewModel(
            Row("Failed", "wreck", 2, "0/1"),
            "payments/event-coast",
            0,
            0,
            18,
            18,
            Brushes.OrangeRed,
            "Failed",
            "bad",
            borderBrush: Brushes.Gold,
            showProblemGlyph: true,
            isEventShallow: true,
            isAnnouncing: true);
        Assert.True(warningBlock.ShowProblemGlyph);
        Assert.True(warningBlock.IsEventShallow);
        Assert.True(warningBlock.IsAnnouncing);
        Assert.Same(Brushes.Gold, warningBlock.BorderBrush);
        Assert.Equal(1.5, warningBlock.BorderThickness);

        var dimmedBlock = new RadarBlockViewModel(
            Row("Running", "background", 0, "1/1"),
            "payments/background",
            0,
            0,
            10,
            10,
            Brushes.Gray,
            "",
            "background",
            isDimmed: true);
        Assert.True(dimmedBlock.IsDimmed);
        Assert.Equal(0.72, dimmedBlock.Opacity);
        Assert.Equal(0.5, dimmedBlock.BorderThickness);

        var task = new PortForwardTaskViewModel("pf-1", "dev", "Pod", "api", "payments", 8080, 18080, "native websocket port-forward", "Ready");
        var taskChanges = new List<string?>();
        task.PropertyChanged += (_, args) => taskChanges.Add(args.PropertyName);
        task.Status = "Stopped";
        task.Status = "Stopped";
        task.Process = null;

        Assert.False(task.IsRunning);
        Assert.Contains(nameof(PortForwardTaskViewModel.Status), taskChanges);
        Assert.Contains(nameof(PortForwardTaskViewModel.IsRunning), taskChanges);
    }

    [Fact]
    public async Task Inspector_blocks_placeholder_yaml_and_hides_logs_for_non_pods()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));
        var ns = Row("Terminating", "stuck-ns", 0, "-") with
        {
            Kind = "Namespace",
            Namespace = null,
            ImageSummary = "-",
            Owner = null,
            Node = null
        };

        viewModel.SelectedResource = ns;
        await viewModel.ApplyEditedYamlAsync();

        Assert.False(viewModel.IsSelectedResourceLoggable);
        Assert.Contains("Fresh YAML has not loaded", viewModel.YamlApplyStatus, StringComparison.Ordinal);

        viewModel.SelectedResource = Row("Running", "api", 0, "1/1");

        Assert.True(viewModel.IsSelectedResourceLoggable);
        Assert.False(viewModel.IsInspectorLogsActive);
        Assert.True(viewModel.CanPortForwardSelectedResource);
        Assert.Contains("Open the Logs tab", viewModel.LogText, StringComparison.Ordinal);

        viewModel.CloseInspector();

        Assert.Equal(0, viewModel.SelectedInspectorTabIndex);
        Assert.False(viewModel.IsInspectorLogsActive);
        Assert.False(viewModel.CanPortForwardSelectedResource);
    }

    [Fact]
    public async Task Inspector_preserves_yaml_editor_when_yaml_tab_is_active()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        var detailRequests = 0;
        var handler = new AppRecordingHandler(request =>
        {
            var path = request.RequestUri?.AbsolutePath ?? string.Empty;
            if (path.EndsWith("/pods/api", StringComparison.Ordinal))
            {
                detailRequests++;
                var marker = detailRequests == 1 ? "server-first" : "server-second";
                return new HttpResponseMessage(HttpStatusCode.OK)
                {
                    Content = new StringContent(PodDetailYamlFixture("api", marker), Encoding.UTF8, "application/json")
                };
            }

            return new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent("""{"items":[]}""", Encoding.UTF8, "application/json")
            };
        });
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state, handler));

        viewModel.SelectedResource = Row("Running", "api", 0, "1/1");
        await viewModel.OpenSelectedResourceAsync();
        viewModel.SelectedInspectorTabIndex = 1;
        await viewModel.LoadFreshYamlAsync();
        var serverFetchesBeforeEdit = detailRequests;
        viewModel.EditableYaml = viewModel.EditableYaml.Replace("server-second", "user-edit", StringComparison.Ordinal);

        await viewModel.OpenSelectedResourceAsync();

        Assert.Equal(serverFetchesBeforeEdit, detailRequests);
        Assert.Contains("user-edit", viewModel.EditableYaml, StringComparison.Ordinal);
        Assert.Contains("paused", viewModel.StatusLine, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Inspector_populates_pod_log_container_options_for_multi_container_pods()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        var handler = new AppRecordingHandler(request =>
        {
            var path = request.RequestUri?.PathAndQuery ?? string.Empty;
            if (path == "/api/v1/namespaces/payments/pods/api/log?tailLines=100&timestamps=true")
            {
                return new HttpResponseMessage(HttpStatusCode.BadRequest)
                {
                    Content = new StringContent("""
                    {"kind":"Status","apiVersion":"v1","status":"Failure","message":"a container name must be specified for pod api, choose one of: [api sidecar]","reason":"BadRequest","code":400}
                    """, Encoding.UTF8, "application/json")
                };
            }

            if (path == "/api/v1/namespaces/payments/pods/api")
            {
                return new HttpResponseMessage(HttpStatusCode.OK)
                {
                    Content = new StringContent("""
                    {
                      "kind": "Pod",
                      "apiVersion": "v1",
                      "metadata": {
                        "name": "api",
                        "namespace": "payments",
                        "uid": "pod-api",
                        "creationTimestamp": "2026-06-10T08:00:00Z"
                      },
                      "spec": {
                        "nodeName": "node-a",
                        "containers": [
                          { "name": "api", "image": "repo/api:1" },
                          { "name": "sidecar", "image": "repo/sidecar:1" }
                        ]
                      },
                      "status": {
                        "phase": "Running",
                        "containerStatuses": [
                          { "name": "api", "ready": true, "restartCount": 0, "state": { "running": {} } },
                          { "name": "sidecar", "ready": true, "restartCount": 0, "state": { "running": {} } }
                        ]
                      }
                    }
                    """, Encoding.UTF8, "application/json")
                };
            }

            if (path.Contains("/log?", StringComparison.Ordinal) && path.Contains("container=api", StringComparison.Ordinal))
            {
                return new HttpResponseMessage(HttpStatusCode.OK)
                {
                    Content = new StringContent("api-line\n")
                };
            }

            if (path.Contains("/log?", StringComparison.Ordinal) && path.Contains("container=sidecar", StringComparison.Ordinal))
            {
                return new HttpResponseMessage(HttpStatusCode.OK)
                {
                    Content = new StringContent("sidecar-line\n")
                };
            }

            return new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent("""{"items":[]}""", Encoding.UTF8, "application/json")
            };
        });
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state, handler));

        viewModel.SelectedResource = Row("Running", "api", 0, "2/2");
        await viewModel.OpenSelectedResourceAsync();

        Assert.Equal(["All containers", "api", "sidecar"], viewModel.PodLogContainerOptions);
        Assert.Equal("All containers", viewModel.SelectedPodLogContainer);

        viewModel.SelectedInspectorTabIndex = 4;
        await Task.Delay(250);

        Assert.Contains("===== container: api =====", viewModel.LogText, StringComparison.Ordinal);
        Assert.Contains("===== container: sidecar =====", viewModel.LogText, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Inspector_validates_yaml_and_requires_second_click_before_delete()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        var handler = new AppRecordingHandler(request =>
        {
            if (request.Method == HttpMethod.Delete)
            {
                return new HttpResponseMessage(HttpStatusCode.OK)
                {
                    Content = new StringContent("""{"kind":"Status","apiVersion":"v1","status":"Success"}""", Encoding.UTF8, "application/json")
                };
            }

            return new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent("""{"items":[]}""", Encoding.UTF8, "application/json")
            };
        });
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state, handler));
        var pod = Row("Running", "api", 0, "1/1");

        viewModel.SelectedResource = pod;
        viewModel.EditableYaml = "apiVersion: v1\nkind: Pod\nmetadata:\n  name: [broken\n";
        await viewModel.ApplyEditedYamlAsync();

        Assert.Contains("YAML syntax error", viewModel.YamlApplyStatus, StringComparison.Ordinal);
        Assert.Contains("YAML syntax:", viewModel.YamlAssistStatus, StringComparison.Ordinal);
        Assert.True(viewModel.CanDeleteSelectedResource);
        Assert.Equal("DELETE", viewModel.DeleteActionLabel);

        await viewModel.DeleteSelectedResourceAsync();

        Assert.Equal("CONFIRM DELETE", viewModel.DeleteActionLabel);
        Assert.Contains("Press delete again", viewModel.StatusLine, StringComparison.Ordinal);

        await viewModel.DeleteSelectedResourceAsync();

        Assert.False(viewModel.IsInspectorVisible);
        Assert.Contains("Deleted Pod/api", viewModel.StatusLine, StringComparison.Ordinal);
    }

    [Fact]
    public void Inspector_omits_unavailable_metric_rows_and_keeps_known_limit_bars()
    {
        var directory = TempDirectory();
        var state = AppState.InMemoryWithConfigDirectory(directory);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        viewModel.SelectedResource = Row("Observed", "cloud-config", 0, "-") with
        {
            Kind = "Secret",
            Node = null,
            ImageSummary = "metadata only",
            Owner = null,
            Pulse = ResourcePulse.Empty
        };

        Assert.DoesNotContain(viewModel.FocusMetrics, row => row.Label == "CPU");
        Assert.DoesNotContain(viewModel.FocusMetrics, row => row.Label == "CPU %");
        Assert.DoesNotContain(viewModel.FocusMetrics, row => row.Label == "Memory");
        Assert.DoesNotContain(viewModel.FocusMetrics, row => row.Label == "Memory %");
        Assert.DoesNotContain(viewModel.FocusMetrics, row => row.Label == "Network");
        Assert.DoesNotContain(viewModel.FocusMetrics, row => row.Label == "Storage");
        Assert.DoesNotContain(viewModel.FocusMetrics, row => row.Label == "Node");
        Assert.DoesNotContain(viewModel.FocusMetrics, row => row.Label == "Owner");
        Assert.DoesNotContain(viewModel.FocusMetrics, row => row.Label == "Ready");
        Assert.DoesNotContain(viewModel.FocusMetrics, row => row.Label == "Restarts");
        Assert.Contains(viewModel.FocusMetrics, row => row.Label == "Image" && row.Value == "metadata only");

        viewModel.SelectedResource = Row("Running", "api", 0, "1/1") with
        {
            Pulse = new ResourcePulse(null, 500, null, 268_435_456, null, null, null, null, "API", "limits only")
        };

        Assert.Contains(viewModel.FocusMetrics, row => row.Label == "CPU" && row.Value == "-/500m" && !row.HasBar);
        Assert.Contains(viewModel.FocusMetrics, row => row.Label == "Memory" && row.Value == "-/256Mi" && !row.HasBar);
        Assert.DoesNotContain(viewModel.FocusMetrics, row => row.Label == "CPU %");
        Assert.DoesNotContain(viewModel.FocusMetrics, row => row.Label == "Memory %");
        Assert.DoesNotContain(viewModel.FocusMetrics, row => row.Label == "CPU limit suggestion");

        viewModel.SelectedResource = Row("Running", "api", 0, "1/1") with
        {
            Pulse = new ResourcePulse(125, 500, 128L * 1024 * 1024, 512L * 1024 * 1024, null, null, null, null, "API LIVE", "live")
        };

        var cpu = Assert.Single(viewModel.FocusMetrics, row => row.Label == "CPU");
        var memory = Assert.Single(viewModel.FocusMetrics, row => row.Label == "Memory");
        Assert.Equal("125m / 500m", cpu.Value);
        Assert.True(cpu.HasSuggestion);
        Assert.Equal("160m request / 250m limit", cpu.Suggestion);
        Assert.Equal(50, cpu.SuggestionPercent);
        Assert.Equal("128Mi / 512Mi", memory.Value);
        Assert.True(memory.HasSuggestion);
        Assert.Equal("160Mi request / 256Mi limit", memory.Suggestion);
        Assert.Equal(50, memory.SuggestionPercent);
        Assert.DoesNotContain(viewModel.FocusMetrics, row => row.Label == "CPU limit suggestion");
        Assert.DoesNotContain(viewModel.FocusMetrics, row => row.Label == "Memory limit suggestion");
    }

    [Fact]
    public async Task Focused_resource_survives_filter_exclusion_and_cache_refresh()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        using var viewModel = new MainWindowViewModel(
            state,
            new KubernetesResourceService(state, new AppRecordingHandler(request =>
            {
                var path = request.RequestUri?.AbsolutePath ?? string.Empty;
                var json = path switch
                {
                    "/api/v1/pods" => """
                      {"items":[
                        {"metadata":{"name":"api","namespace":"payments","uid":"pod-api","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-a","containers":[{"image":"repo/api:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}},
                        {"metadata":{"name":"worker","namespace":"payments","uid":"pod-worker","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-b","containers":[{"image":"repo/worker:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}}
                      ]}
                      """,
                    "/apis/apps/v1/namespaces/payments/deployments" or "/apis/apps/v1/deployments" => """{"items":[{"metadata":{"name":"api","namespace":"payments","uid":"deploy-api","creationTimestamp":"2026-06-10T08:00:00Z"},"status":{"availableReplicas":1,"replicas":1,"readyReplicas":1}}]}""",
                    _ => """{"items":[]}"""
                };
                return new HttpResponseMessage(HttpStatusCode.OK)
                {
                    Content = new StringContent(json, Encoding.UTF8, "application/json")
                };
            })));

        viewModel.ReloadSessions();
        viewModel.ProblemsOnly = false;
        viewModel.KindPicker.SetExpression("\"Pod\"");
        await viewModel.RefreshResourcesAsync(force: true);
        var api = Assert.Single(viewModel.Resources, row => row.Kind == "Pod" && row.Name == "api");
        viewModel.SelectedResourceRow = api;

        viewModel.NamePicker.SetExpression("\"definitely-missing\"");

        Assert.Equal("api", viewModel.SelectedResource?.Name);
        Assert.Equal("Pod", viewModel.SelectedResource?.Kind);
        Assert.Null(viewModel.SelectedResourceRow);
        Assert.True(viewModel.IsInspectorVisible);

        viewModel.NamePicker.SetExpression(string.Empty);
        await viewModel.RefreshResourcesAsync(force: true);

        Assert.Equal("api", viewModel.SelectedResource?.Name);
        Assert.Equal("Pod", viewModel.SelectedResource?.Kind);
        Assert.Equal("api", viewModel.SelectedResourceRow?.Name);
    }

    [Fact]
    public void Port_forward_is_available_only_for_running_pods_and_namespaced_services()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        viewModel.SelectedResource = Row("Succeeded", "job-pod", 0, "0/1");
        viewModel.PrepareSelectedResourcePortForward();

        Assert.False(viewModel.CanPortForwardSelectedResource);
        Assert.False(viewModel.IsPortForwardToolOpen);
        Assert.Contains("Running pods", viewModel.StatusLine, StringComparison.Ordinal);

        viewModel.SelectedResource = Row("ClusterIP", "api-service", 0, "-") with
        {
            Kind = "Service",
            Namespace = "payments"
        };

        Assert.True(viewModel.CanPortForwardSelectedResource);

        viewModel.PortForwards.Add(new PortForwardTaskViewModel("pf-starting", "dev", "Pod", "api", "payments", 80, 18080, "native", "starting"));
        viewModel.PortForwards.Add(new PortForwardTaskViewModel("pf-running", "dev", "Pod", "api-2", "payments", 81, 18081, "native", "running"));
        viewModel.PortForwards.Add(new PortForwardTaskViewModel("pf-stopped", "dev", "Pod", "old", "payments", 82, 18082, "native", "stopped"));
        viewModel.PortForwards.Add(new PortForwardTaskViewModel("pf-failed", "dev", "Pod", "bad", "payments", 83, 18083, "native", "failed"));

        Assert.Equal(["api", "api-2"], viewModel.VisiblePortForwards.Select(port => port.Name).ToArray());
        viewModel.OpenPortForwardTask(viewModel.PortForwards[1]);

        Assert.True(viewModel.IsPortForwardStopMode);
        Assert.Equal("STOP", viewModel.PortForwardActionLabel);
        Assert.Equal("18081", viewModel.PortLocalPort);

        viewModel.StopSelectedPortForward();

        Assert.Equal("stopped", viewModel.PortForwards[1].Status);
        Assert.Equal("START", viewModel.PortForwardActionLabel);
        Assert.Equal(["api"], viewModel.VisiblePortForwards.Select(port => port.Name).ToArray());
    }

    [Fact]
    public void Port_forward_is_rejected_for_cluster_scoped_resources()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        viewModel.SelectedResource = Row("Running", "api", 0, "1/1") with { Namespace = null };
        viewModel.PrepareSelectedResourcePortForward();

        Assert.False(viewModel.CanPortForwardSelectedResource);
        Assert.False(viewModel.IsPortForwardToolOpen);
            Assert.Empty(viewModel.VisiblePortForwards);
    }

    [Fact]
    public async Task Port_forward_preflight_rejects_occupied_local_port_before_service_call()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("http://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));
        viewModel.ReloadSessions();

        var selected = Row("Running", "api", 0, "1/1");
        viewModel.SelectedResource = selected;
        viewModel.PrepareSelectedResourcePortForward();

        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var occupiedPort = ((IPEndPoint)listener.LocalEndpoint).Port;
        viewModel.PortLocalPort = occupiedPort.ToString(System.Globalization.CultureInfo.InvariantCulture);

        await viewModel.StartPreparedPortForwardAsync();

        Assert.Empty(viewModel.PortForwards);
        Assert.Equal($"Reachable port {occupiedPort} is already in use on this machine.", viewModel.StatusLine);
    }

    [Fact]
    public void Resource_search_close_clears_filter_and_table_selection_focuses_resource()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));
        var row = Row("Running", "api", 0, "1/1");

        viewModel.SelectWorkspace("resources");
        viewModel.ResourceQuickSearch = "api";
        viewModel.Search = "worker";
        viewModel.CloseSearchForCurrentWorkspace();
        viewModel.SelectedResourceRow = row;

        Assert.Equal(string.Empty, viewModel.ResourceQuickSearch);
        Assert.Equal(string.Empty, viewModel.Search);
        Assert.Same(row, viewModel.SelectedResource);
        Assert.Same(row, viewModel.SelectedResourceRow);

        viewModel.CloseInspector();

        Assert.Null(viewModel.SelectedResource);
        Assert.Null(viewModel.SelectedResourceRow);

        viewModel.ToggleResourceSearch();
        viewModel.ResourceQuickSearch = "api";
        viewModel.Search = "worker";
        viewModel.ToggleResourceSearch();

        Assert.False(viewModel.IsResourceSearchOpen);
        Assert.Equal(string.Empty, viewModel.ResourceQuickSearch);
        Assert.Equal(string.Empty, viewModel.Search);

        viewModel.SelectWorkspace("graph");
        viewModel.ToggleGraphSearch();
        viewModel.GraphSearch = "api";
        viewModel.ToggleGraphSearch();
        Assert.False(viewModel.IsGraphSearchOpen);
        Assert.Equal(string.Empty, viewModel.GraphSearch);

        viewModel.SelectWorkspace("events");
        viewModel.ToggleEventSearch();
        viewModel.EventQuickSearch = "BackOff";
        viewModel.ToggleEventSearch();
        Assert.False(viewModel.IsEventSearchOpen);
        Assert.Equal(string.Empty, viewModel.EventQuickSearch);

        viewModel.SelectWorkspace("ports");
        viewModel.TogglePortSearch();
        viewModel.PortQuickSearch = "8080";
        viewModel.TogglePortSearch();
        Assert.False(viewModel.IsPortSearchOpen);
        Assert.Equal(string.Empty, viewModel.PortQuickSearch);
    }

    [Fact]
    public void Problems_and_activity_filters_are_mutually_exclusive()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://127.0.0.1:6443"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        Assert.False(viewModel.ProblemsOnly);
        Assert.False(viewModel.ActivityOnly);

        viewModel.ProblemsOnly = true;

        Assert.True(viewModel.ProblemsOnly);
        Assert.False(viewModel.ActivityOnly);

        viewModel.ActivityOnly = true;

        Assert.False(viewModel.ProblemsOnly);
        Assert.True(viewModel.ActivityOnly);

        viewModel.ProblemsOnly = true;

        Assert.True(viewModel.ProblemsOnly);
        Assert.False(viewModel.ActivityOnly);
    }

    [Fact]
    public void Workspace_models_ignore_noop_setters_and_expose_default_values()
    {
        var context = new ImportedContextRowViewModel("ctx-1", "/tmp/config", "display", _ => { }, (_, _) => { });
        var contextChanges = 0;
        context.PropertyChanged += (_, _) => contextChanges++;

        context.DisplayName = "display";

        Assert.Equal("display", context.DisplayName);
        Assert.Equal(0, contextChanges);

        var graph = new GraphNodeViewModel("Service", "api", "payments", "Observed");
        graph.IsSearchMatch = false;
        graph.IsCurrentSearchMatch = false;

        Assert.False(graph.IsSearchMatch);
        Assert.False(graph.IsCurrentSearchMatch);
        Assert.Equal(SolidColorBrush.Parse("#18000000").ToString(), graph.BackgroundBrush.ToString());

        graph.IsSearchMatch = true;
        Assert.Equal(SolidColorBrush.Parse("#26141D12").ToString(), graph.BackgroundBrush.ToString());

        var portForward = new PortForwardTaskViewModel("pf-2", "dev", "Pod", "api", "payments", 80, 18080, "native", "Ready");
        Assert.Equal("Ready", portForward.Status);
        Assert.Equal("api", portForward.Name);
    }

    [Fact]
    public void Imported_context_commands_call_rename_and_remove_actions()
    {
        ImportedContextRowViewModel? removed = null;
        string? renamed = null;
        ImportedContextRowViewModel? activated = null;
        var row = new ImportedContextRowViewModel(
            "ctx-1",
            "/tmp/config",
            "old",
            item => removed = item,
            (item, name) => renamed = $"{item.ContextId}:{name}",
            item => activated = item,
            isActive: true,
            sourceName: "config.yaml",
            hash: "hash");

        row.DisplayName = "new";
        row.ActivateCommand.Execute(null);
        row.RenameCommand.Execute(null);
        row.RemoveCommand.Execute(null);

        Assert.Same(row, activated);
        Assert.Equal("ctx-1:new", renamed);
        Assert.Same(row, removed);
        Assert.Equal("ACTIVE", row.ActiveMark);
        Assert.Equal("config.yaml", row.SourceName);
        Assert.Equal("hash", row.Hash);
    }

    [Fact]
    public void Source_status_row_edits_name_and_filter_through_table_callbacks()
    {
        var renamed = string.Empty;
        var filtered = string.Empty;
        var row = new SourceStatusRow(
            "config.yaml",
            "hash",
            "now",
            "/tmp/config",
            "dev",
            "cluster",
            "user",
            "token",
            "ok",
            "-",
            "ctx-1",
            filterName: "",
            renameAction: (_, value) =>
            {
                renamed = value;
                return string.IsNullOrWhiteSpace(value) ? "dev" : value.Trim();
            },
            filterAction: (_, value) =>
            {
                filtered = value;
                return value == "missing" ? "default" : value;
            });

        Assert.Equal("default", row.FilterName);

        row.Context = " prod ";
        row.FilterName = "missing";

        Assert.Equal(" prod ", renamed);
        Assert.Equal("prod", row.Context);
        Assert.Equal("missing", filtered);
        Assert.Equal("default", row.FilterName);
    }

    [Fact]
    public void Resource_value_rows_hide_secret_values_and_temporarily_reveal_decoded_values()
    {
        var secret = new ResourceValueRow("password", "c3dvcmQ=", sensitive: true, base64Encoded: true);
        var config = new ResourceValueRow("mode", "debug", sensitive: false, base64Encoded: false);

        Assert.Equal("••••••••••••", secret.DisplayValue);
        Assert.Equal("sword", secret.DecodedValue);
        Assert.Equal("sword", secret.PreferredCopyValue);
        Assert.Equal("debug", config.DisplayValue);

        secret.RevealTemporarily();

        Assert.Equal("sword", secret.DisplayValue);

        secret.Hide();

        Assert.Equal("••••••••••••", secret.DisplayValue);
    }

    [Fact]
    public void Alert_rule_store_merges_defaults_preserves_default_enabled_state_and_keeps_custom_rules()
    {
        var directory = TempDirectory();
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var defaultRule = AlertRuleCatalog.DefaultRules[0] with { Enabled = false };
            var custom = new AlertRule(
                "custom-memory",
                "Memory pressure",
                "Custom memory alert",
                true,
                false,
                "warning",
                new AlertRuleMatchers(Memory: "p95"),
                new AlertRuleActions(PlaySound: true),
                new AlertRuleUntil(AlertUntilModes.Duration, "10s"),
                "warning-ping");
            var grouped = custom with
            {
                Id = "custom-grouped",
                Name = "Grouped",
                MatcherGroups =
                [
                    new AlertMatcherGroup("one", [new AlertMatcherCriterion("kind", "Kind", "\"Pod\"")]),
                    new AlertMatcherGroup("two", [new AlertMatcherCriterion("cpu", "CPU", "p95")])
                ]
            };

            AlertRuleStore.Save([defaultRule, custom, grouped]);
            var loaded = AlertRuleStore.Load();

            Assert.Contains(loaded, rule => rule.Id == defaultRule.Id && !rule.Enabled && rule.BuiltIn);
            Assert.Contains(loaded, rule => rule.Id == "custom-memory" && !rule.BuiltIn && rule.Matchers.Memory == "p95");
            var loadedGrouped = Assert.Single(loaded, rule => rule.Id == "custom-grouped");
            Assert.Equal(2, loadedGrouped.MatcherGroups?.Count);
            Assert.Equal(["one", "two"], loadedGrouped.MatcherGroups!.Select(group => group.Id).ToArray());
            Assert.Equal(AlertRuleCatalog.DefaultRules.Count + 2, loaded.Count);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Alert_rule_store_returns_defaults_for_missing_and_malformed_files()
    {
        var directory = TempDirectory();
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            Assert.Equal(AlertRuleCatalog.DefaultRules.Select(rule => rule.Id), AlertRuleStore.Load().Select(rule => rule.Id));

            Directory.CreateDirectory(directory);
            File.WriteAllText(Path.Combine(directory, "alert-rules.json"), "{not-json");

            Assert.Equal(AlertRuleCatalog.DefaultRules.Select(rule => rule.Id), AlertRuleStore.Load().Select(rule => rule.Id));
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Alert_editor_adds_custom_rule_saves_it_and_evaluates_cached_rows()
    {
        var directory = TempDirectory();
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var state = AppState.InMemoryWithConfigDirectory(directory);
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));
            foreach (var rule in viewModel.AlertRules.Where(rule => rule.BuiltIn))
            {
                rule.Enabled = false;
            }

            viewModel.AddAlertRule();
            Assert.NotNull(viewModel.SelectedAlertRule);
            var custom = viewModel.SelectedAlertRule!;
            custom.Name = "Restarted pods";
            custom.Kind = "\"Pod\"";
            custom.Restarts = ">1";
            custom.RadarBlink = true;
            custom.RadarZoom = false;
            custom.PlaySound = true;
            custom.SoundChoice = AlertSoundCatalog.Resolve("warning-ping").Label;
            InjectCachedRows(viewModel, [
                Row("Running", "quiet", 0, "1/1"),
                Row("Running", "noisy", 2, "1/1")
            ]);

            viewModel.SaveAlertRules();

            var active = Assert.Single(viewModel.ActiveAlerts);
            Assert.Equal("Restarted pods", active.Rule);
            Assert.Contains("Pod/noisy", active.Matches, StringComparison.Ordinal);
            Assert.Contains("blink", active.Actions, StringComparison.Ordinal);
            Assert.Contains("Warning ping", active.Sound, StringComparison.Ordinal);
            Assert.Contains(AlertRuleStore.Load(), rule => rule.Name == "Restarted pods" && rule.Matchers.Restarts == ">1");
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Alert_rule_row_blocks_builtin_edits_and_exposes_sound_attribution()
    {
        var builtInRule = AlertRuleCatalog.DefaultRules.First(rule => rule.Id == "default-active-view-pulse");
        var builtIn = new AlertRuleRowViewModel(builtInRule);
        var custom = new AlertRuleRowViewModel(builtInRule with
        {
            Id = "custom-copy",
            BuiltIn = false,
            Name = "Custom copy"
        });
        var changedProperties = new List<string>();
        custom.PropertyChanged += (_, args) =>
        {
            if (args.PropertyName is not null)
            {
                changedProperties.Add(args.PropertyName);
            }
        };

        builtIn.Name = "Changed";
        builtIn.Kind = "\"Deployment\"";
        builtIn.Enabled = false;
        custom.Kind = "\"Deployment\"";
        custom.Restarts = ">5";
        custom.PlaySound = true;
        custom.SoundChoice = AlertSoundCatalog.Resolve("critical-klaxon").Label;
        custom.SelectedColor = Color.Parse("#123456");
        custom.UseStatusColor();
        custom.UseNoColor();
        custom.SoundSearch = "metal";

        Assert.NotEqual("Changed", builtIn.Name);
        Assert.Equal("", builtIn.Kind);
        Assert.False(builtIn.Enabled);
        Assert.Contains("Kind=\"Deployment\"", custom.MatcherSummary, StringComparison.Ordinal);
        Assert.Contains("Restarts=>5", custom.MatcherSummary, StringComparison.Ordinal);
        Assert.Contains("sound:critical-klaxon", custom.ActionSummary, StringComparison.Ordinal);
        Assert.Equal("none", custom.ColorChoice);
        Assert.True(builtIn.IsNoColor);
        Assert.Equal("5s", builtIn.AnimationUntilDuration);
        Assert.Equal("5s", builtIn.AnimationDurationChoice);
        Assert.False(builtIn.HasSound);
        Assert.False(builtIn.HasZoom);
        Assert.Equal(string.Empty, builtIn.SoundAttribution);
        Assert.Equal(string.Empty, builtIn.SoundSourceUrl);
        Assert.NotNull(builtIn.AnimationPreviewBrush);
        Assert.Contains(custom.FilteredSoundChoices, choice => choice.Contains("Metal", StringComparison.OrdinalIgnoreCase));
        Assert.Contains(custom.FilteredSoundItems, choice => choice.Name.Contains("Metal", StringComparison.OrdinalIgnoreCase));
        Assert.DoesNotContain(custom.FilteredSoundChoices, choice => choice.Contains("Radar activated", StringComparison.OrdinalIgnoreCase));
        Assert.Equal("Kenney", custom.SoundAttribution);
        Assert.Equal("https://kenney.nl/assets/sci-fi-sounds", custom.SoundSourceUrl);
        Assert.Contains(nameof(AlertRuleRowViewModel.MatcherSummary), changedProperties);
        Assert.Contains(nameof(AlertRuleRowViewModel.ActionSummary), changedProperties);
    }

    [Fact]
    public void Alert_rule_row_guided_editor_builds_or_matchers_actions_and_sound_metadata()
    {
        var row = new AlertRuleRowViewModel(new AlertRule(
            "custom-guided",
            "Guided alert",
            "",
            true,
            false,
            "",
            new AlertRuleMatchers(),
            new AlertRuleActions(RadarFocus: false, RadarZoom: false, RadarBlink: false, RadarColor: false, PlaySound: false),
            new AlertRuleUntil("none"),
            "none"));

        row.MatcherGroups[0].Criteria[0].Field = "Kind";
        row.MatcherGroups[0].Criteria[0].Expression = "\"Pod\"";
        row.AddCriterion(row.MatcherGroups[0]);
        row.MatcherGroups[0].Criteria[1].Field = "Restarts";
        row.MatcherGroups[0].Criteria[1].Expression = ">1";
        row.AddMatcherGroup();
        row.MatcherGroups[1].Criteria[0].Field = "CPU";
        row.MatcherGroups[1].Criteria[0].Expression = "p95";
        row.ColorChoice = "red";
        row.ColorDurationChoice = "10s";
        row.AnimationChoice = "blink";
        row.AnimationDurationChoice = "until change";
        row.ZoomChoice = "150%";
        row.SoundChoice = AlertSoundCatalog.Resolve("critical-klaxon").Label;

        var rule = row.ToRule();

        Assert.Contains("once", row.DurationChoices);
        Assert.Contains("1s", row.DurationChoices);
        Assert.Contains("60s", row.DurationChoices);
        Assert.Equal(2, rule.MatcherGroups?.Count);
        Assert.Equal(["Kind", "Restarts"], rule.MatcherGroups![0].Criteria.Select(criterion => criterion.Field).ToArray());
        Assert.Equal(["CPU"], rule.MatcherGroups[1].Criteria.Select(criterion => criterion.Field).ToArray());
        Assert.True(rule.Actions.RadarColor);
        Assert.Equal("red", rule.Actions.RadarColorValue);
        Assert.Equal(AlertUntilModes.Duration, rule.Actions.RadarColorUntilMode);
        Assert.Equal("10s", rule.Actions.RadarColorUntilDuration);
        Assert.True(rule.Actions.RadarBlink);
        Assert.Equal("blink", rule.Actions.RadarAnimation);
        Assert.True(rule.Actions.RadarZoom);
        Assert.Equal(150, rule.Actions.RadarZoomPercent);
        Assert.True(rule.Actions.PlaySound);
        Assert.Equal("critical-klaxon", rule.SoundId);
        Assert.Contains("color:red", row.ActionSummary, StringComparison.Ordinal);
        Assert.Contains("animation:blink", row.ActionSummary, StringComparison.Ordinal);
        Assert.Contains("zoom:150%", row.ActionSummary, StringComparison.Ordinal);
    }

    [Fact]
    public void Alert_sound_catalog_uses_traceable_bundled_cc0_assets()
    {
        var projectRoot = LocateProjectRoot();
        foreach (var sound in AlertSoundCatalog.BuiltIn.Where(sound => sound.Id != "none"))
        {
            Assert.Equal("Kenney", sound.Author);
            Assert.Equal("CC0-1.0", sound.License);
            Assert.StartsWith("https://kenney.nl/assets/", sound.SourceUrl, StringComparison.Ordinal);
            Assert.True(File.Exists(Path.Combine(projectRoot, "src", "Podlord.App", sound.Asset)), $"{sound.Asset} is missing.");
        }

        Assert.True(AlertSoundCatalog.BuiltIn.Count >= 100);
        Assert.Contains(AlertSoundCatalog.BuiltIn, sound => sound.Id == "radar-activated");
        Assert.Contains(AlertSoundCatalog.BuiltIn, sound => sound.Id == "panel-segment-load");
        Assert.Contains(AlertSoundCatalog.BuiltIn, sound => sound.Id == "kenney-interface-error-008" && sound.SourceUrl == "https://kenney.nl/assets/interface-sounds");
        Assert.Contains(AlertSoundCatalog.BuiltIn, sound => sound.Id == "command-ambient-loop" && sound.IsMusic);
    }

    [Fact]
    public void Default_alert_rules_recreate_old_radar_colors_and_view_pulse()
    {
        var directory = TempDirectory();
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var state = AppState.InMemoryWithConfigDirectory(directory);
            state.SaveSettings(state.Settings() with { RadarAutoFollowAlerts = false });
            var player = new RecordingAlertSoundPlayer();
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state), player);
            viewModel.SetRadarViewport(480, 220);
            InjectCachedRows(viewModel,
            [
                QuietAlertRow("quiet", 0),
                Row("Running", "recent", 0, "1/1") with { Age = "2h", LastChange = "now" },
                Row("Pending", "waiting", 0, "0/1") with { Age = "2h", LastChange = "2h" },
                Row("CrashLoopBackOff", "broken", 1, "0/1") with { Age = "2h", LastChange = "2h" }
            ]);

            ApplyLocalFilter(viewModel);

            var quiet = Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "quiet");
            var recent = Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "recent");
            var waiting = Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "waiting");
            var broken = Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "broken");

            Assert.Equal("none", quiet.AlertColor);
            Assert.False(quiet.IsPulseAnimation);
            Assert.Equal("fresh", recent.AlertColor);
            Assert.True(recent.IsPulseAnimation);
            Assert.Equal("status", waiting.AlertColor);
            Assert.True(waiting.IsPulseAnimation);
            Assert.Equal("status", broken.AlertColor);
            Assert.True(broken.IsPulseAnimation);
            var graphBroken = FlattenGraph(viewModel.GraphNodes).First(node => node.Resource?.Name == "broken");
            Assert.True(graphBroken.Resource?.IsPulseAnimation);
            var waitingRow = Assert.Single(viewModel.Resources, row => row.Name == "waiting");
            var problemBrush = new AlertResourceBrushConverter().Convert(waitingRow, typeof(IBrush), null, CultureInfo.InvariantCulture);
            Assert.Equal(BrushColor(AppThemeCatalog.StatusBrush("WARNING")), BrushColor(problemBrush));

            var recentBroken = Row("CrashLoopBackOff", "recent-broken", 1, "0/1") with { Age = "2h", LastChange = "now" };
            InjectCachedRows(viewModel, [recentBroken]);
            ApplyLocalFilter(viewModel);
            Assert.Equal("status", Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "recent-broken").AlertColor);
            Assert.Contains(player.PlayedPaths, path => path.EndsWith("warning-ping.ogg", StringComparison.Ordinal));
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Problem_color_default_zooms_to_oldest_matching_resource_and_plays_warning_once_per_update()
    {
        var directory = TempDirectory();
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var state = AppState.InMemoryWithConfigDirectory(directory);
            state.SaveSettings(state.Settings() with { RadarAutoFollowAlerts = false });
            var player = new RecordingAlertSoundPlayer();
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state), player);
            viewModel.SetRadarViewport(480, 220);

            InjectCachedRows(viewModel,
            [
                QuietAlertRow("quiet", 0),
                Row("Pending", "waiting", 0, "0/1") with { Age = "5h", LastChange = "5h" }
            ]);
            ApplyLocalFilter(viewModel);

            Assert.Equal(1, CountPlayed(player, "warning-ping.ogg"));
            Assert.InRange(ReadPrivate<double>(viewModel, "radarAutoFollowTargetZoom"), 0.99, 1.01);

            InjectCachedRows(viewModel,
            [
                QuietAlertRow("quiet", 0),
                Row("Pending", "waiting", 0, "0/1") with { Age = "5h", LastChange = "5h" },
                Row("CrashLoopBackOff", "broken", 1, "0/1") with { Age = "1h", LastChange = "1h" }
            ]);
            ApplyLocalFilter(viewModel);

            var oldestTarget = Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "waiting");
            var expectedPanX = -(oldestTarget.X + oldestTarget.Width / 2d - 480d / 2d);
            var expectedPanY = -(oldestTarget.Y + oldestTarget.Height / 2d - 220d / 2d);

            viewModel.PlayNextQueuedAlertSoundForTests();
            Assert.Equal(2, CountPlayed(player, "warning-ping.ogg"));
            Assert.Equal(1, viewModel.RadarAutoFollowQueueCountForTests);
            for (var i = 0; i < 18; i++)
            {
                viewModel.StepRadarAutoFollowForTests();
            }

            Assert.Equal(0, viewModel.RadarAutoFollowQueueCountForTests);
            Assert.Equal(expectedPanX, ReadPrivate<double>(viewModel, "radarAutoFollowTargetPanX"), precision: 2);
            Assert.Equal(expectedPanY, ReadPrivate<double>(viewModel, "radarAutoFollowTargetPanY"), precision: 2);

            ApplyLocalFilter(viewModel);
            Assert.Equal(2, CountPlayed(player, "warning-ping.ogg"));

            InjectCachedRows(viewModel,
            [
                QuietAlertRow("quiet", 0),
                Row("Pending", "waiting", 0, "0/1") with { Age = "5h", LastChange = "5h" },
                Row("CrashLoopBackOff", "broken", 2, "0/1") with { Age = "2h", LastChange = "1s" }
            ]);
            ApplyLocalFilter(viewModel);

            viewModel.PlayNextQueuedAlertSoundForTests();
            Assert.Equal(3, CountPlayed(player, "warning-ping.ogg"));
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Reactivating_problem_color_default_replays_warning_for_existing_problem_match()
    {
        var directory = TempDirectory();
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var state = AppState.InMemoryWithConfigDirectory(directory);
            var player = new RecordingAlertSoundPlayer();
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state), player);
            viewModel.SetRadarViewport(480, 220);
            InjectCachedRows(viewModel, [Row("Pending", "waiting", 0, "0/1") with { Age = "2h", LastChange = "2h" }]);

            ApplyLocalFilter(viewModel);

            Assert.Equal(1, CountPlayed(player, "warning-ping.ogg"));
            var problemRule = Assert.Single(viewModel.AlertRules, rule => rule.Id == "default-problem-color");

            viewModel.ToggleAlertRule(problemRule);
            Assert.Equal(1, CountPlayed(player, "warning-ping.ogg"));

            viewModel.ToggleAlertRule(problemRule);
            viewModel.PlayNextQueuedAlertSoundForTests();

            Assert.Equal(2, CountPlayed(player, "warning-ping.ogg"));
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Muting_app_audio_blocks_automatic_alert_sounds()
    {
        var directory = TempDirectory();
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var state = AppState.InMemoryWithConfigDirectory(directory);
            var player = new RecordingAlertSoundPlayer();
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state), player);
            viewModel.ToggleAudioMute();
            viewModel.SetRadarViewport(480, 220);
            InjectCachedRows(viewModel,
            [
                Row("Pending", "waiting", 0, "0/1") with { Age = "2h", LastChange = "2h" },
                Row("CrashLoopBackOff", "broken", 1, "0/1") with { Age = "2h", LastChange = "2h" }
            ]);

            ApplyLocalFilter(viewModel);

            Assert.True(viewModel.IsAudioMuted);
            Assert.Empty(player.PlayedPaths);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Multiple_alert_sounds_are_played_in_sequence_instead_of_collapsing()
    {
        var directory = TempDirectory();
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var state = AppState.InMemoryWithConfigDirectory(directory);
            var player = new RecordingAlertSoundPlayer();
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state), player);
            foreach (var rule in viewModel.AlertRules.Where(rule => rule.BuiltIn))
            {
                rule.Enabled = false;
            }

            viewModel.AddAlertRule();
            var warning = Assert.IsType<AlertRuleRowViewModel>(viewModel.SelectedAlertRule);
            warning.Name = "Restart warning";
            warning.Restarts = ">1";
            warning.SoundChoice = AlertSoundCatalog.Resolve("warning-ping").Label;
            warning.ColorChoice = "none";
            warning.AnimationChoice = "none";
            warning.ZoomChoice = "none";

            viewModel.AddAlertRule();
            var critical = Assert.IsType<AlertRuleRowViewModel>(viewModel.SelectedAlertRule);
            critical.Name = "Restart critical";
            critical.Restarts = ">1";
            critical.SoundChoice = AlertSoundCatalog.Resolve("critical-klaxon").Label;
            critical.ColorChoice = "none";
            critical.AnimationChoice = "none";
            critical.ZoomChoice = "none";

            InjectCachedRows(viewModel, [Row("Running", "noisy", 2, "1/1")]);

            viewModel.SaveAlertRules();

            Assert.Single(player.PlayedPaths);
            Assert.EndsWith("warning-ping.ogg", player.PlayedPaths[0], StringComparison.Ordinal);

            viewModel.PlayNextQueuedAlertSoundForTests();

            Assert.Equal(2, player.PlayedPaths.Count);
            Assert.EndsWith("critical-klaxon.ogg", player.PlayedPaths[1], StringComparison.Ordinal);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Multiple_alert_zoom_actions_are_queued_in_rule_order()
    {
        var directory = TempDirectory();
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var state = AppState.InMemoryWithConfigDirectory(directory);
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));
            foreach (var rule in viewModel.AlertRules.Where(rule => rule.BuiltIn))
            {
                rule.Enabled = false;
            }

            viewModel.SetRadarViewport(480, 220);
            viewModel.AddAlertRule();
            var first = Assert.IsType<AlertRuleRowViewModel>(viewModel.SelectedAlertRule);
            first.Name = "First zoom";
            first.NameFilter = "\"first\"";
            first.ColorChoice = "none";
            first.AnimationChoice = "none";
            first.ZoomChoice = "100%";

            viewModel.AddAlertRule();
            var second = Assert.IsType<AlertRuleRowViewModel>(viewModel.SelectedAlertRule);
            second.Name = "Second zoom";
            second.NameFilter = "\"second\"";
            second.ColorChoice = "none";
            second.AnimationChoice = "none";
            second.ZoomChoice = "100%";

            InjectCachedRows(viewModel,
            [
                Row("Running", "first", 0, "1/1") with { Age = "2h", LastChange = "2h" },
                Row("Running", "second", 0, "1/1") with { Age = "2h", LastChange = "2h" }
            ]);

            viewModel.SaveAlertRules();

            Assert.Equal(1, viewModel.RadarAutoFollowQueueCountForTests);
            var firstTargetPanX = ReadPrivate<double>(viewModel, "radarAutoFollowTargetPanX");
            for (var i = 0; i < 18; i++)
            {
                viewModel.StepRadarAutoFollowForTests();
            }

            Assert.Equal(0, viewModel.RadarAutoFollowQueueCountForTests);
            Assert.NotEqual(firstTargetPanX, ReadPrivate<double>(viewModel, "radarAutoFollowTargetPanX"));
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Audio_backend_failures_do_not_break_alert_rendering()
    {
        var directory = TempDirectory();
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var state = AppState.InMemoryWithConfigDirectory(directory);
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state), new ThrowingAlertSoundPlayer());
            viewModel.SetRadarViewport(480, 220);
            InjectCachedRows(viewModel, [Row("Pending", "waiting", 0, "0/1") with { Age = "2h", LastChange = "2h" }]);

            ApplyLocalFilter(viewModel);

            var block = Assert.Single(viewModel.RadarBlocks, item => item.Resource.Name == "waiting");
            Assert.Equal("status", block.AlertColor);
            Assert.NotEmpty(viewModel.ActiveAlerts);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Default_audio_player_can_be_disabled_for_headless_test_runs()
    {
        var previous = Environment.GetEnvironmentVariable("PODLORD_DISABLE_AUDIO");
        Environment.SetEnvironmentVariable("PODLORD_DISABLE_AUDIO", "1");
        try
        {
            using var player = AlertSoundPlayerFactory.CreateDefault();

            Assert.IsType<NoOpAlertSoundPlayer>(player);
            Assert.True(player.Play("missing.wav", out var error));
            Assert.Equal(string.Empty, error);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_DISABLE_AUDIO", previous);
        }
    }

    [Fact]
    public void Disabling_default_alert_rules_removes_old_hidden_radar_and_table_fallbacks()
    {
        var directory = TempDirectory();
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var state = AppState.InMemoryWithConfigDirectory(directory);
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));
            foreach (var rule in viewModel.AlertRules.Where(rule => rule.BuiltIn))
            {
                rule.Enabled = false;
            }

            viewModel.SetRadarViewport(480, 220);
            InjectCachedRows(viewModel,
            [
                Row("CrashLoopBackOff", "broken", 1, "0/1"),
                Row("Running", "recent", 0, "1/1") with { LastChange = "now" }
            ]);

            ApplyLocalFilter(viewModel);

            Assert.All(viewModel.RadarBlocks.Where(block => block.Resource.Kind == "Pod"), block =>
            {
                Assert.Equal("none", block.AlertColor);
                Assert.False(block.IsAnnouncing);
            });
            Assert.All(viewModel.Resources, row =>
            {
                Assert.False(row.IsAnnouncing);
                Assert.Equal("", row.AlertAnimation);
            });
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Default_active_view_pulse_expires_without_restarting_while_resource_stays_visible()
    {
        var directory = TempDirectory();
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var state = AppState.InMemoryWithConfigDirectory(directory);
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));
            viewModel.SetRadarViewport(480, 220);
            InjectCachedRows(viewModel, [Row("Running", "active", 0, "1/1") with { Age = "2h", LastChange = "2h" }]);

            ApplyLocalFilter(viewModel);

            var firstRadar = Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "active");
            Assert.True(firstRadar.IsPulseAnimation);
            Assert.True(Assert.Single(viewModel.Resources, row => row.Name == "active").IsPulseAnimation);

            ExpireAlertTimers(viewModel, firstRadar.Resource.Id);
            viewModel.ExpireAlertAnimationsForTests();

            var secondRadar = Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "active");
            Assert.False(secondRadar.IsAnnouncing);
            Assert.False(Assert.Single(viewModel.Resources, row => row.Name == "active").IsAnnouncing);

            viewModel.PanRadar(10_000, 10_000);
            Assert.DoesNotContain(viewModel.RadarBlocks, block => block.Resource.Name == "active");

            viewModel.PanRadar(-10_000, -10_000);
            var returningRadar = Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "active");
            Assert.True(returningRadar.IsPulseAnimation);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Alert_sound_preview_uses_embedded_audio_player_without_shelling_to_browser()
    {
        var directory = TempDirectory();
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var state = AppState.InMemoryWithConfigDirectory(directory);
            var player = new RecordingAlertSoundPlayer();
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state), player);
            viewModel.AddAlertRule();
            viewModel.SelectedAlertRule!.SoundChoice = AlertSoundCatalog.Resolve("warning-ping").Label;

            viewModel.PreviewSelectedAlertSound();

            var playedPath = Assert.Single(player.PlayedPaths);
            Assert.EndsWith("warning-ping.ogg", playedPath, StringComparison.Ordinal);
            Assert.True(File.Exists(playedPath));
            Assert.Contains("Previewing Warning ping", viewModel.StatusLine, StringComparison.Ordinal);
            Assert.False(player.DisposedBeforePlay);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Alert_actions_control_radar_blink_and_color_from_cached_rows()
    {
        var directory = TempDirectory();
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var state = AppState.InMemoryWithConfigDirectory(directory);
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));
            foreach (var rule in viewModel.AlertRules.Where(rule => rule.BuiltIn))
            {
                rule.Enabled = false;
            }

            viewModel.SetRadarViewport(480, 220);
            InjectCachedRows(viewModel, [QuietAlertRow("noisy", 2)]);
            viewModel.AddAlertRule();
            Assert.NotNull(viewModel.SelectedAlertRule);
            var custom = viewModel.SelectedAlertRule!;
            custom.Name = "Silent visual rule";
            custom.Kind = "\"Pod\"";
            custom.RadarBlink = false;
            custom.RadarColor = false;
            custom.RadarFocus = false;
            custom.RadarZoom = false;
            viewModel.SaveAlertRules();

            var quietBlock = Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "noisy");
            var quietBrush = quietBlock.Brush;
            Assert.False(quietBlock.IsAnnouncing);

            custom.RadarBlink = true;
            custom.RadarColor = true;
            custom.ColorChoice = "#123456";
            viewModel.SaveAlertRules();

            var activeBlock = Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "noisy");
            Assert.True(activeBlock.IsAnnouncing);
            Assert.False(ReferenceEquals(quietBrush, activeBlock.Brush));
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Alert_duration_keeps_action_active_after_match_clears()
    {
        var directory = TempDirectory();
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var state = AppState.InMemoryWithConfigDirectory(directory);
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));
            foreach (var rule in viewModel.AlertRules.Where(rule => rule.BuiltIn))
            {
                rule.Enabled = false;
            }

            viewModel.SetRadarViewport(480, 220);
            viewModel.AddAlertRule();
            Assert.NotNull(viewModel.SelectedAlertRule);
            var custom = viewModel.SelectedAlertRule!;
            custom.Name = "Restart hold";
            custom.Kind = "\"Pod\"";
            custom.Restarts = ">1";
            custom.RadarBlink = true;
            custom.RadarColor = false;
            custom.UntilMode = AlertUntilModes.Duration;
            custom.UntilDuration = "10s";

            InjectCachedRows(viewModel, [QuietAlertRow("noisy", 2)]);
            viewModel.SaveAlertRules();
            Assert.True(Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "noisy").IsAnnouncing);

            InjectCachedRows(viewModel, [QuietAlertRow("noisy", 0)]);
            viewModel.SaveAlertRules();

            Assert.True(Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "noisy").IsAnnouncing);
            Assert.Contains(viewModel.ActiveAlerts, alert => alert.Rule == "Restart hold");
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Alert_color_duration_holds_color_after_match_clears_without_forcing_animation()
    {
        var directory = TempDirectory();
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var state = AppState.InMemoryWithConfigDirectory(directory);
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));
            foreach (var rule in viewModel.AlertRules.Where(rule => rule.BuiltIn))
            {
                rule.Enabled = false;
            }

            viewModel.SetRadarViewport(480, 220);
            viewModel.AddAlertRule();
            Assert.NotNull(viewModel.SelectedAlertRule);
            var custom = viewModel.SelectedAlertRule!;
            custom.Name = "Color hold";
            custom.Kind = "\"Pod\"";
            custom.Restarts = ">1";
            custom.ColorChoice = "#123456";
            custom.ColorUntilMode = AlertUntilModes.Duration;
            custom.ColorUntilDuration = "10s";
            custom.AnimationChoice = "none";
            custom.ZoomChoice = "none";

            InjectCachedRows(viewModel, [QuietAlertRow("noisy", 2)]);
            viewModel.SaveAlertRules();
            ApplyLocalFilter(viewModel);
            var active = Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "noisy");
            var activeRow = Assert.Single(viewModel.Resources, row => row.Name == "noisy");

            Assert.Equal("#123456", active.AlertColor);
            Assert.False(active.IsAnnouncing);
            Assert.False(activeRow.IsAnnouncing);

            InjectCachedRows(viewModel, [QuietAlertRow("noisy", 0)]);
            viewModel.SaveAlertRules();
            ApplyLocalFilter(viewModel);
            var held = Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "noisy");
            var heldRow = Assert.Single(viewModel.Resources, row => row.Name == "noisy");

            Assert.Equal("#123456", held.AlertColor);
            Assert.False(held.IsAnnouncing);
            Assert.False(heldRow.IsAnnouncing);

            custom.ColorUntilMode = "none";
            viewModel.SaveAlertRules();
            ApplyLocalFilter(viewModel);

            Assert.Equal("none", Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "noisy").AlertColor);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Theory]
    [InlineData("blink")]
    [InlineData("pulse")]
    [InlineData("sweep")]
    [InlineData("outline")]
    public void Alert_animation_choice_reaches_radar_and_resource_rows_as_distinct_state(string animation)
    {
        var directory = TempDirectory();
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var state = AppState.InMemoryWithConfigDirectory(directory);
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));
            foreach (var rule in viewModel.AlertRules.Where(rule => rule.BuiltIn))
            {
                rule.Enabled = false;
            }

            viewModel.SetRadarViewport(480, 220);
            viewModel.AddAlertRule();
            Assert.NotNull(viewModel.SelectedAlertRule);
            var custom = viewModel.SelectedAlertRule!;
            custom.Name = "Animation state";
            custom.Kind = "\"Pod\"";
            custom.Restarts = ">1";
            custom.ColorChoice = "none";
            custom.AnimationChoice = animation;
            custom.AnimationUntilMode = AlertUntilModes.NoMatch;
            custom.ZoomChoice = "none";

            InjectCachedRows(viewModel, [Row("Running", "noisy", 2, "1/1")]);
            viewModel.SaveAlertRules();
            ApplyLocalFilter(viewModel);

            var radar = Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "noisy");
            var resource = Assert.Single(viewModel.Resources, row => row.Name == "noisy");
            Assert.True(radar.IsAnnouncing);
            Assert.True(resource.IsAnnouncing);
            Assert.Equal(animation, radar.AlertAnimation);
            Assert.Equal(animation, resource.AlertAnimation);
            Assert.Equal(animation == "blink", radar.IsBlinkAnimation);
            Assert.Equal(animation == "pulse", radar.IsPulseAnimation);
            Assert.Equal(animation == "sweep", radar.IsSweepAnimation);
            Assert.Equal(animation == "outline", radar.IsOutlineAnimation);
            Assert.Equal(animation == "blink", resource.IsBlinkAnimation);
            Assert.Equal(animation == "pulse", resource.IsPulseAnimation);
            Assert.Equal(animation == "sweep", resource.IsSweepAnimation);
            Assert.Equal(animation == "outline", resource.IsOutlineAnimation);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Alert_animation_duration_holds_animation_after_match_clears()
    {
        var directory = TempDirectory();
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var state = AppState.InMemoryWithConfigDirectory(directory);
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));
            foreach (var rule in viewModel.AlertRules.Where(rule => rule.BuiltIn))
            {
                rule.Enabled = false;
            }

            viewModel.SetRadarViewport(480, 220);
            viewModel.AddAlertRule();
            Assert.NotNull(viewModel.SelectedAlertRule);
            var custom = viewModel.SelectedAlertRule!;
            custom.Name = "Animation hold";
            custom.Kind = "\"Pod\"";
            custom.Restarts = ">1";
            custom.ColorChoice = "none";
            custom.AnimationChoice = "sweep";
            custom.AnimationDurationChoice = "10s";
            custom.ZoomChoice = "none";

            InjectCachedRows(viewModel, [Row("Running", "noisy", 2, "1/1")]);
            viewModel.SaveAlertRules();
            ApplyLocalFilter(viewModel);
            Assert.True(Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "noisy").IsSweepAnimation);

            InjectCachedRows(viewModel, [Row("Running", "noisy", 0, "1/1")]);
            viewModel.SaveAlertRules();
            ApplyLocalFilter(viewModel);
            var heldRadar = Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "noisy");
            var heldResource = Assert.Single(viewModel.Resources, row => row.Name == "noisy");

            Assert.True(heldRadar.IsSweepAnimation);
            Assert.True(heldResource.IsSweepAnimation);
            Assert.Contains(viewModel.ActiveAlerts, alert => alert.Rule == "Animation hold");
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Finite_alert_animation_expires_and_restarts_only_when_matched_resource_changes()
    {
        var directory = TempDirectory();
        var previous = Environment.GetEnvironmentVariable("PODLORD_CONFIG_HOME");
        Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", directory);
        try
        {
            var state = AppState.InMemoryWithConfigDirectory(directory);
            using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));
            foreach (var rule in viewModel.AlertRules.Where(rule => rule.BuiltIn))
            {
                rule.Enabled = false;
            }

            viewModel.SetRadarViewport(480, 220);
            viewModel.AddAlertRule();
            var custom = Assert.IsType<AlertRuleRowViewModel>(viewModel.SelectedAlertRule);
            custom.Name = "Finite pulse";
            custom.Kind = "\"Pod\"";
            custom.Restarts = ">1";
            custom.ColorChoice = "none";
            custom.AnimationChoice = "pulse";
            custom.AnimationDurationChoice = "2s";
            custom.ZoomChoice = "none";

            InjectCachedRows(viewModel, [Row("Running", "noisy", 2, "1/1")]);
            viewModel.SaveAlertRules();
            ApplyLocalFilter(viewModel);

            var rowId = Assert.Single(viewModel.Resources, row => row.Name == "noisy").Id;
            Assert.True(Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "noisy").IsPulseAnimation);

            ExpireAlertHold(viewModel, "alertAnimationUntilByRuleResource", custom.Id, rowId);
            viewModel.ExpireAlertAnimationsForTests();

            Assert.False(Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "noisy").IsAnnouncing);
            Assert.False(Assert.Single(viewModel.Resources, row => row.Name == "noisy").IsAnnouncing);

            InjectCachedRows(viewModel, [Row("Running", "noisy", 3, "1/1") with { LastChange = "1s" }]);
            viewModel.SaveAlertRules();
            ApplyLocalFilter(viewModel);

            Assert.True(Assert.Single(viewModel.RadarBlocks, block => block.Resource.Name == "noisy").IsPulseAnimation);
            Assert.True(Assert.Single(viewModel.Resources, row => row.Name == "noisy").IsPulseAnimation);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", previous);
            Directory.Delete(directory, recursive: true);
        }
    }

    private static FilterPreset Preset(string name, bool problems)
    {
        return new FilterPreset(name, problems, "", "", "", "", "", "", "", "", "", "", "", "", "", "", "256");
    }

    private sealed class RecordingAlertSoundPlayer : IAlertSoundPlayer
    {
        private bool disposed;

        public List<string> PlayedPaths { get; } = [];

        public bool DisposedBeforePlay { get; private set; }

        public bool Play(string path, out string error)
        {
            DisposedBeforePlay = disposed;
            PlayedPaths.Add(path);
            error = string.Empty;
            return true;
        }

        public void Dispose()
        {
            disposed = true;
        }
    }

    private sealed class ThrowingAlertSoundPlayer : IAlertSoundPlayer
    {
        public bool Play(string path, out string error)
        {
            throw new InvalidOperationException("no audio device");
        }

        public void Dispose()
        {
        }
    }

    private static string OneContextKubeconfig(string server, string name = "dev")
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

    private static string OneContextKubeconfigWithoutServer()
    {
        return """
apiVersion: v1
clusters:
- name: dev
  cluster: {}
contexts:
- name: dev
  context:
    cluster: dev
    user: dev
users:
- name: dev
  user:
    token: secret-token
""";
    }

    private static FlatResourceRow Row(string status, string name, int restarts, string ready)
    {
        return new FlatResourceRow(
            $"id-{name}",
            status,
            "Pod",
            name,
            "payments",
            "cluster-a",
            "1m",
            ready,
            restarts,
            "node-a",
            "api:1",
            "ReplicaSet/api",
            "now",
            FreshnessState.Fresh);
    }

    private static FlatResourceRow QuietAlertRow(string name, int restarts)
    {
        return Row("Succeeded", name, restarts, "1/1") with { Age = "2h", LastChange = "2h" };
    }

    private static void InjectCachedRows(MainWindowViewModel viewModel, IReadOnlyList<FlatResourceRow> rows)
    {
        var field = typeof(MainWindowViewModel).GetField("cachedRows", BindingFlags.NonPublic | BindingFlags.Instance)
                    ?? throw new InvalidOperationException("cachedRows field missing");
        var cached = (List<FlatResourceRow>)(field.GetValue(viewModel)
                    ?? throw new InvalidOperationException("cachedRows field was null"));
        cached.Clear();
        cached.AddRange(rows);
    }

    private static void ApplyLocalFilter(MainWindowViewModel viewModel)
    {
        var method = typeof(MainWindowViewModel).GetMethod("ApplyLocalFilter", BindingFlags.NonPublic | BindingFlags.Instance)
                     ?? throw new InvalidOperationException("ApplyLocalFilter method missing");
        method.Invoke(viewModel, null);
    }

    private static void ExpireAlertTimers(MainWindowViewModel viewModel, string rowId)
    {
        ExpireTimer(viewModel, "resourceAlertBlinkUntil", rowId);
        ExpireTimer(viewModel, "radarAlertBlinkUntil", rowId);
    }

    private static void ExpireTimer(MainWindowViewModel viewModel, string fieldName, string rowId)
    {
        var field = typeof(MainWindowViewModel).GetField(fieldName, BindingFlags.NonPublic | BindingFlags.Instance)
                    ?? throw new InvalidOperationException($"{fieldName} field missing");
        var timers = (Dictionary<string, DateTimeOffset>)(field.GetValue(viewModel)
                     ?? throw new InvalidOperationException($"{fieldName} field is null"));
        timers[rowId] = DateTimeOffset.Now.Subtract(TimeSpan.FromSeconds(1));
    }

    private static void ExpireAlertHold(MainWindowViewModel viewModel, string fieldName, string ruleId, string rowId)
    {
        var field = typeof(MainWindowViewModel).GetField(fieldName, BindingFlags.NonPublic | BindingFlags.Instance)
                    ?? throw new InvalidOperationException($"{fieldName} field missing");
        var timers = (Dictionary<(string RuleId, string RowId), DateTimeOffset>)(field.GetValue(viewModel)
                     ?? throw new InvalidOperationException($"{fieldName} field is null"));
        timers[(ruleId, rowId)] = DateTimeOffset.Now.Subtract(TimeSpan.FromSeconds(1));
    }

    private static T ReadPrivate<T>(MainWindowViewModel viewModel, string fieldName)
    {
        var field = typeof(MainWindowViewModel).GetField(fieldName, BindingFlags.NonPublic | BindingFlags.Instance)
                    ?? throw new InvalidOperationException($"{fieldName} field missing");
        return (T)(field.GetValue(viewModel)
                   ?? throw new InvalidOperationException($"{fieldName} field is null"));
    }

    private static int CountPlayed(RecordingAlertSoundPlayer player, string fileName)
    {
        return player.PlayedPaths.Count(path => path.EndsWith(fileName, StringComparison.Ordinal));
    }

    private static Color BrushColor(object brush)
    {
        return Assert.IsType<SolidColorBrush>(brush).Color;
    }

    private static IEnumerable<GraphNodeViewModel> FlattenGraph(IEnumerable<GraphNodeViewModel> nodes)
    {
        foreach (var node in nodes)
        {
            yield return node;
            foreach (var child in FlattenGraph(node.Children))
            {
                yield return child;
            }
        }
    }

    private static string RadarFixtureFor(string path)
    {
        if (path.EndsWith("/replicasets", StringComparison.Ordinal))
        {
            return """
            {"items":[
              {"metadata":{"name":"api-rs","namespace":"payments","uid":"rs-1","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"replicas":2},"status":{"replicas":2,"readyReplicas":2,"availableReplicas":2}}
            ]}
            """;
        }

        if (path.EndsWith("/pods", StringComparison.Ordinal))
        {
            return """
            {"items":[
              {"metadata":{"name":"api-1","namespace":"payments","uid":"pod-1","creationTimestamp":"2026-06-10T08:00:00Z","ownerReferences":[{"kind":"ReplicaSet","name":"api-rs"}]},"spec":{"nodeName":"node-a","containers":[{"image":"repo/api:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}}]}},
              {"metadata":{"name":"worker-1","namespace":"jobs","uid":"pod-2","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-b","containers":[{"image":"repo/worker:1"}]},"status":{"phase":"Pending","containerStatuses":[{"ready":false,"restartCount":0,"state":{"waiting":{"reason":"Pending"}}}]}}
            ]}
            """;
        }

        return """{"items":[]}""";
    }

    private static string PodDetailYamlFixture(string name, string marker)
    {
        return $$"""
        {
          "kind": "Pod",
          "apiVersion": "v1",
          "metadata": {
            "name": "{{name}}",
            "namespace": "payments",
            "uid": "pod-{{name}}",
            "creationTimestamp": "2026-06-10T08:00:00Z",
            "labels": { "podlord.test/marker": "{{marker}}" }
          },
          "spec": {
            "nodeName": "node-a",
            "containers": [
              { "name": "api", "image": "repo/api:1" }
            ]
          },
          "status": {
            "phase": "Running",
            "containerStatuses": [
              { "name": "api", "ready": true, "restartCount": 0, "state": { "running": {} } }
            ]
          }
        }
        """;
    }

    private static double Distance(RadarBlockViewModel left, RadarBlockViewModel right)
    {
        var leftX = left.X + left.Width / 2;
        var leftY = left.Y + left.Height / 2;
        var rightX = right.X + right.Width / 2;
        var rightY = right.Y + right.Height / 2;
        var dx = leftX - rightX;
        var dy = leftY - rightY;
        return Math.Sqrt(dx * dx + dy * dy);
    }

    private static bool IsRadarGridAligned(double value, double viewportCenter)
    {
        const double origin = -2.75;
        const double step = 7;
        var grid = (value - viewportCenter - origin) / step;
        return Math.Abs(grid - Math.Round(grid)) < 0.0001;
    }

    private static string LocatePodlordLocalizerSource()
    {
        for (var directory = AppContext.BaseDirectory; !string.IsNullOrWhiteSpace(directory); directory = Path.GetDirectoryName(directory) ?? string.Empty)
        {
            var directMatch = Path.Combine(directory, "PodlordLocalizer.cs");
            if (File.Exists(directMatch))
            {
                return directMatch;
            }

            var sourceMatch = Path.Combine(directory, "src", "Podlord.App", "PodlordLocalizer.cs");
            if (File.Exists(sourceMatch))
            {
                return sourceMatch;
            }

            if (directory == Path.GetPathRoot(directory))
            {
                break;
            }
        }

        throw new FileNotFoundException("PodlordLocalizer.cs source file not found.");
    }

    private static string LocateProjectRoot()
    {
        for (var directory = AppContext.BaseDirectory; !string.IsNullOrWhiteSpace(directory); directory = Path.GetDirectoryName(directory) ?? string.Empty)
        {
            if (File.Exists(Path.Combine(directory, "Podlord.slnx"))
                && Directory.Exists(Path.Combine(directory, "src", "Podlord.App")))
            {
                return directory;
            }

            if (directory == Path.GetPathRoot(directory))
            {
                break;
            }
        }

        throw new DirectoryNotFoundException("Podlord project root not found.");
    }

    private static IReadOnlySet<string> CatalogLocaleCodesFromSource(string source)
    {
        return Regex.Matches(source, "\\[\"(?<code>[^\"]+)\"\\]\\s*=\\s*(?:English|WithEnglish)", RegexOptions.Multiline)
            .Select(match => match.Groups["code"].Value)
            .Where(code => !string.IsNullOrWhiteSpace(code))
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
    }

    private static IReadOnlySet<string> EnglishLocaleKeysFromSource(string source)
    {
        var english = Regex.Match(
            source,
            "private static readonly IReadOnlyDictionary<string, string> English = new Dictionary<string, string>\\(StringComparer\\.Ordinal\\)\\s*\\{(?<body>[\\s\\S]*?)\\n\\s*\\};",
            RegexOptions.Multiline);
        Assert.True(english.Success, "Could not parse English locale block from PodlordLocalizer source.");

        return Regex.Matches(english.Groups["body"].Value, "\\[\"(?<key>[^\"]+)\"\\]\\s*=", RegexOptions.Multiline)
            .Select(match => match.Groups["key"].Value)
            .Distinct(StringComparer.Ordinal)
            .ToHashSet(StringComparer.Ordinal);
    }

    private static IReadOnlySet<string> LocalizerTranslationsFromSource(string source, string locale)
    {
        if (string.Equals(locale, "en", StringComparison.OrdinalIgnoreCase))
        {
            return EnglishLocaleKeysFromSource(source);
        }

        var match = Regex.Match(
            source,
            "\\[\"" + Regex.Escape(locale) + "\"\\]\\s*=\\s*WithEnglish\\(\"" + Regex.Escape(locale) + "\",\\s*new Dictionary<string, string>\\(StringComparer\\.Ordinal\\)\\s*\\{(?<body>[\\s\\S]*?)\\}\\s*\\)",
            RegexOptions.Multiline);

        Assert.True(match.Success, $"Could not parse locale block for '{locale}'.");

        return Regex.Matches(match.Groups["body"].Value, "\\[\"(?<key>[^\"]+)\"\\]\\s*=", RegexOptions.Multiline)
            .Select(match => match.Groups["key"].Value)
            .Distinct(StringComparer.Ordinal)
            .ToHashSet(StringComparer.Ordinal);
    }

    private static string TempDirectory()
    {
        var path = Path.Combine(Path.GetTempPath(), $"podlord-app-test-{Guid.NewGuid():N}");
        Directory.CreateDirectory(path);
        return path;
    }

    private static HttpResponseMessage JsonResponse(string json)
    {
        return new HttpResponseMessage(HttpStatusCode.OK)
        {
            Content = new StringContent(json, Encoding.UTF8, "application/json")
        };
    }

    private sealed class MutableClock(DateTimeOffset now) : IPodlordClock
    {
        public DateTimeOffset Now { get; set; } = now;
    }

    private sealed class AppRecordingHandler(Func<HttpRequestMessage, HttpResponseMessage> respond) : HttpMessageHandler
    {
        public List<string> Requests { get; } = [];

        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
        {
            Requests.Add(request.RequestUri?.PathAndQuery ?? string.Empty);
            return Task.FromResult(respond(request));
        }
    }

    private sealed class AsyncAppRecordingHandler(Func<HttpRequestMessage, CancellationToken, Task<HttpResponseMessage>> respond) : HttpMessageHandler
    {
        public List<string> Requests { get; } = [];

        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
        {
            Requests.Add(request.RequestUri?.PathAndQuery ?? string.Empty);
            return respond(request, cancellationToken);
        }
    }

    private sealed class FakeReleaseUpdateChecker(UpdateCheckState result) : IReleaseUpdateChecker
    {
        public int Calls { get; private set; }

        public Task<UpdateCheckState> CheckLatestAsync(string currentVersion, CancellationToken cancellationToken)
        {
            Calls++;
            return Task.FromResult(result with { CurrentVersion = currentVersion });
        }

        public void Dispose()
        {
        }
    }
}
