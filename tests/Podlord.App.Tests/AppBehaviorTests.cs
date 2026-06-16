using System.Globalization;
using System.Net;
using System.Net.Sockets;
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
        Assert.True(Settings.Default.RadarWaterEnabled);
        Assert.Equal((byte)45, Settings.Default.RadarWaterSpeed);
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
            Assert.NotEqual(PodlordLocalizer.Text("action.applyServerSide", "en"), PodlordLocalizer.Text("action.applyServerSide", locale.Code));
            Assert.NotEqual(PodlordLocalizer.Text("inspector.overview", "en"), PodlordLocalizer.Text("inspector.overview", locale.Code));
            Assert.NotEqual(PodlordLocalizer.Text("port.containerPort", "en"), PodlordLocalizer.Text("port.containerPort", locale.Code));
        }

        Assert.Equal("RESOURCES", PodlordLocalizer.Text("nav.resources", "definitely-not-real"));
        Assert.Equal("missing.key", PodlordLocalizer.Text("missing.key", "de"));
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
        viewModel.RadarAutoFollowAlertsSetting = false;

        var settings = state.Settings();
        Assert.Equal((byte)86, settings.PixelEffectIntensity);
        Assert.Equal("light", settings.ThemeVariant);
        Assert.Equal((byte)35, settings.AnimationIntensity);
        Assert.False(settings.RadarWaterEnabled);
        Assert.Equal((byte)70, settings.RadarWaterSpeed);
        Assert.False(settings.RadarAutoFollowAlerts);
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
            source.FilterName = "Pods";
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
        var configB = Path.Combine(directory, "b.yaml");
        File.WriteAllText(configA, OneContextKubeconfig("http://127.0.0.1:1", "dev-a"));
        File.WriteAllText(configB, OneContextKubeconfig("http://127.0.0.1:2", "dev-b"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(configA);
        state.ImportKubeconfig(configB);
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
        File.WriteAllText(Path.Combine(scanRoot, "notes.txt"), "ignore-me");
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
                        {"metadata":{"name":"known","namespace":"payments","uid":"1","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-a","containers":[{"image":"repo/api:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}]}}},
                        {"metadata":{"name":"missing","namespace":"payments","uid":"2","creationTimestamp":"2026-06-10T08:00:00Z"},"spec":{"nodeName":"node-b","containers":[{"image":"repo/worker:1"}]},"status":{"phase":"Running","containerStatuses":[{"ready":true,"restartCount":0,"state":{"running":{}}]}}}
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

        await service.WarmResourceCacheAsync(new ResourceQuery(devSession.Id, Kind: "\"Pod\"", ForceRefresh: true), KubernetesRequestPriority.UserVisible);
        await service.WarmResourceCacheAsync(new ResourceQuery(prodSession.Id, Kind: "\"Pod\"", ForceRefresh: true), KubernetesRequestPriority.UserVisible);
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
        viewModel.EditableYaml = viewModel.EditableYaml.Replace("server-first", "user-edit", StringComparison.Ordinal);

        await viewModel.OpenSelectedResourceAsync();

        Assert.Equal(1, detailRequests);
        Assert.Contains("user-edit", viewModel.EditableYaml, StringComparison.Ordinal);
        Assert.DoesNotContain("server-second", viewModel.EditableYaml, StringComparison.Ordinal);
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

        Assert.Contains(viewModel.FocusMetrics, row => row.Label == "CPU" && row.Value == "-/500m" && row.HasBar);
        Assert.Contains(viewModel.FocusMetrics, row => row.Label == "Memory" && row.Value == "-/256Mi" && row.HasBar);
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

    private static FilterPreset Preset(string name, bool problems)
    {
        return new FilterPreset(name, problems, "", "", "", "", "", "", "", "", "", "", "", "", "", "", "256");
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
}
