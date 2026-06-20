using System.Text.RegularExpressions;
using Podlord.App;

namespace Podlord.App.Tests;

public sealed class FilterPickerBehaviorTests
{
    [Fact]
    public void Search_enter_adds_custom_values_that_are_checkable_and_removable()
    {
        var changed = 0;
        var picker = new FilterPickerViewModel("CustomResourceDefinition", "Kind", () => changed++);
        picker.ReplaceOptions(["Deployment", "Pod", "Service"]);

        picker.Search = "Pod";
        picker.AddSearchAsCustom();
        picker.Search = "/podlord-.*/";
        picker.AddSearchAsCustom();

        Assert.Equal("Pod /podlord-.*/", picker.Expression);
        Assert.Equal(2, picker.CustomValues.Count);
        picker.CustomValues[0].IsSelected = false;
        Assert.Equal("/podlord-.*/", picker.Expression);
        picker.CustomValues[0].IsSelected = true;
        picker.CustomValues[1].RemoveCommand.Execute(null);
        Assert.Equal("Pod", picker.Expression);
        Assert.True(changed >= 2);
    }

    [Fact]
    public void Saved_exact_multiselect_expression_restores_as_checked_options()
    {
        var picker = new FilterPickerViewModel("CustomResourceDefinition", "Kind", () => { });
        picker.ReplaceOptions(["Deployment", "Pod", "Service"]);

        picker.SetExpression("\"Pod\" \"Deployment\"");

        Assert.Empty(picker.CustomValues);
        Assert.Equal("\"Deployment\" \"Pod\"", picker.Expression);
        Assert.Contains("2 selected", picker.Summary, StringComparison.Ordinal);
    }

    [Fact]
    public void Unknown_exact_expression_restores_as_custom_filter_value()
    {
        var changed = 0;
        var picker = new FilterPickerViewModel("CustomResourceDefinition", "Kind", () => changed++);
        picker.ReplaceOptions(["Deployment", "Pod", "Service"]);

        picker.SetExpression("\"DefinitelyMissingKind\"");

        Assert.Single(picker.CustomValues);
        Assert.Equal("\"DefinitelyMissingKind\"", picker.Expression);
        Assert.Contains("1 custom", picker.Summary, StringComparison.Ordinal);
        Assert.Equal(1, changed);
    }

    [Fact]
    public void Option_refresh_preserves_custom_values_as_user_owned_options()
    {
        var picker = new FilterPickerViewModel("Namespace", "Namespace", () => { });
        picker.ReplaceOptions(["default", "payments"]);
        picker.Search = "payments";
        picker.AddSearchAsCustom();

        picker.ReplaceOptions(["default", "payments", "platform"]);

        Assert.Single(picker.CustomValues);
        Assert.Equal("payments", picker.Expression);
    }

    [Fact]
    public void Summary_clear_search_and_option_changes_cover_picker_states()
    {
        var changed = 0;
        var picker = new FilterPickerViewModel("Namespace", "Namespace", () => changed++);
        picker.ReplaceOptions(["default", "payments", "platform"]);

        Assert.Equal("Namespace", picker.Summary);
        Assert.Equal("Namespace", picker.GlyphKind);
        picker.Search = "zz";
        Assert.All(picker.Options, option => Assert.False(option.IsVisible));
        picker.Search = "pay";
        Assert.Contains(picker.Options, option => option.Value == "payments" && option.IsVisible);
        picker.Search = "payments";
        picker.AddSearchAsCustom();

        Assert.Equal("Namespace: 1 custom", picker.Summary);
        picker.Search = "/prod-.*/";
        picker.AddSearchAsCustom();
        Assert.Contains("2 custom", picker.Summary, StringComparison.Ordinal);
        picker.AddSearchAsCustom();
        Assert.Contains("2 custom", picker.Summary, StringComparison.Ordinal);
        picker.ClearCommand.Execute(null);

        Assert.Equal("Namespace", picker.Summary);
        Assert.Equal(string.Empty, picker.Expression);
        Assert.True(changed >= 3);
    }

    [Fact]
    public void Filter_option_and_relay_command_are_publicly_observable()
    {
        var changed = 0;
        var option = new FilterOptionViewModel("payments", false, () => changed++);
        var properties = new List<string?>();
        option.PropertyChanged += (_, args) => properties.Add(args.PropertyName);

        option.IsSelected = true;
        option.IsSelected = true;
        option.ApplySearch("zzz");
        option.ApplySearch("pay");
        option.SetSelectedSilently(false);
        option.SetSelectedSilently(false);

        Assert.Equal(1, changed);
        Assert.Contains(nameof(FilterOptionViewModel.IsSelected), properties);
        Assert.Contains(nameof(FilterOptionViewModel.IsVisible), properties);
        Assert.False(option.IsSelected);
        Assert.True(option.IsVisible);

        var commandRaised = false;
        var command = new RelayCommand(() => changed++);
        command.CanExecuteChanged += (_, _) => commandRaised = true;
        Assert.True(command.CanExecute(null));
        command.Execute(null);
        command.RaiseCanExecuteChanged();
        Assert.True(commandRaised);
        Assert.Equal(2, changed);
    }

    [Fact]
    public void Picker_handles_custom_only_duplicate_noops_and_same_option_refresh()
    {
        var changed = 0;
        var picker = new FilterPickerViewModel("ConfigMap", "Image", () => changed++);
        picker.ReplaceOptions(["api:1", "worker:2"]);

        picker.Search = "/api:.*/";
        picker.AddSearchAsCustom();
        picker.Search = "/api:.*/";
        picker.AddSearchAsCustom();

        Assert.Equal("Image: 1 custom", picker.Summary);
        Assert.Single(picker.CustomValues);

        picker.ReplaceOptions(["api:1", "worker:2"]);

        Assert.Single(picker.CustomValues);

        picker.SetExpression("~registry.local/");

        Assert.Equal("~registry.local/", picker.Expression);
        Assert.Equal("Image: 1 custom", picker.Summary);

        picker.CustomValues[0].RemoveCommand.Execute(null);

        Assert.Equal(string.Empty, picker.Expression);
        Assert.Equal("Image", picker.Summary);
        Assert.True(changed >= 3);
    }

    [Fact]
    public void Picker_keeps_exact_custom_values_until_user_deletes_them()
    {
        var picker = new FilterPickerViewModel("Namespace", "Namespace", () => { });
        picker.Search = "\"payments\"";
        picker.AddSearchAsCustom();

        Assert.Single(picker.CustomValues);

        picker.ReplaceOptions(["default", "payments"]);

        Assert.Single(picker.CustomValues);
        Assert.Equal("\"payments\"", picker.Expression);
        Assert.Equal("Namespace: 1 custom", picker.Summary);
        picker.CustomValues[0].IsSelected = false;
        Assert.Equal(string.Empty, picker.Expression);
        picker.CustomValues[0].RemoveCommand.Execute(null);
        Assert.Empty(picker.CustomValues);
    }

    [Fact]
    public void Main_window_converter_static_resources_are_registered()
    {
        var root = FindRepositoryRoot();
        var app = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "App.axaml"));
        var window = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "MainWindow.axaml"));
        var registered = Regex.Matches(app, "x:Key=\"([^\"]+Converter)\"")
            .Select(match => match.Groups[1].Value)
            .ToHashSet(StringComparer.Ordinal);
        var referenced = Regex.Matches(window, "StaticResource\\s+([A-Za-z0-9_]+Converter)")
            .Select(match => match.Groups[1].Value)
            .Distinct(StringComparer.Ordinal)
            .Order(StringComparer.Ordinal)
            .ToList();

        var missing = referenced.Where(reference => !registered.Contains(reference)).ToList();

        Assert.Empty(missing);
    }

    [Fact]
    public void Main_window_uses_drawn_kind_glyphs_instead_of_kind_abbreviations()
    {
        var root = FindRepositoryRoot();
        var app = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "App.axaml"));
        var window = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "MainWindow.axaml"));

        Assert.Contains("<local:KindGlyph", window, StringComparison.Ordinal);
        Assert.DoesNotContain("KindIconConverter", app, StringComparison.Ordinal);
        Assert.DoesNotContain("KindIconConverter", window, StringComparison.Ordinal);
    }

    [Fact]
    public void Main_window_exposes_storage_column_metric_filters_and_quantity_sorting_hooks()
    {
        var root = FindRepositoryRoot();
        var window = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "MainWindow.axaml"));
        var viewModel = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "MainWindowViewModel.cs"));
        var domain = File.ReadAllText(Path.Combine(root, "src", "Podlord.Core", "Domain.cs"));
        var matcher = File.ReadAllText(Path.Combine(root, "src", "Podlord.Core", "ResourceFilterMatcher.cs"));
        var presetStore = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "FilterPresetStore.cs"));

        Assert.Contains("Tag=\"Storage\"", window, StringComparison.Ordinal);
        Assert.Contains("Kind=\"PersistentVolume\"", window, StringComparison.Ordinal);
        Assert.Contains("Text=\"{Binding StorageCompactDisplay}\"", window, StringComparison.Ordinal);
        Assert.Contains("StorageMetricDetail", window + domain, StringComparison.Ordinal);
        Assert.Contains("IsVisible=\"{Binding HasStorageMetricBar}\"", window, StringComparison.Ordinal);
        Assert.Contains("StoragePicker = new FilterPickerViewModel", viewModel, StringComparison.Ordinal);
        Assert.Contains("CpuPicker = new FilterPickerViewModel", viewModel, StringComparison.Ordinal);
        Assert.Contains("MemoryPicker = new FilterPickerViewModel", viewModel, StringComparison.Ordinal);
        Assert.Contains("Cpu: EmptyToNull(CpuPicker.Expression)", viewModel, StringComparison.Ordinal);
        Assert.Contains("Memory: EmptyToNull(MemoryPicker.Expression)", viewModel, StringComparison.Ordinal);
        Assert.Contains("Storage: EmptyToNull(StoragePicker.Expression)", viewModel, StringComparison.Ordinal);
        Assert.Contains("\"Storage\" => MetricSortValue(row.Pulse.StorageUsedBytes, row.Pulse.StorageLimitBytes)", viewModel, StringComparison.Ordinal);
        Assert.Contains("string? Storage = null", domain, StringComparison.Ordinal);
        Assert.Contains("public static bool MatchesCpu", matcher, StringComparison.Ordinal);
        Assert.Contains("public static bool MatchesBytes", matcher, StringComparison.Ordinal);
        Assert.Contains("ParseCpuQuantity", matcher, StringComparison.Ordinal);
        Assert.Contains("ParseByteQuantity", matcher, StringComparison.Ordinal);
        Assert.Contains("string Storage = \"\"", presetStore, StringComparison.Ordinal);
    }

    [Fact]
    public void Main_window_settings_and_tables_expose_full_text_without_fixed_label_columns()
    {
        var root = FindRepositoryRoot();
        var app = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "App.axaml"));
        var window = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "MainWindow.axaml"));
        var codeBehind = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "MainWindow.axaml.cs"));

        Assert.Contains("ColumnDefinitions=\"240,*\"", window, StringComparison.Ordinal);
        Assert.Contains("ColumnDefinitions=\"240,*,54\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("ColumnDefinitions=\"Auto,*\" RowDefinitions=\"Auto,Auto\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("ColumnDefinitions=\"Auto,*,54\" RowDefinitions=\"Auto,Auto\"", window, StringComparison.Ordinal);
        Assert.Contains("<Setter Property=\"MaxWidth\" Value=\"220\" />", app, StringComparison.Ordinal);
        Assert.Contains("<Setter Property=\"TextTrimming\" Value=\"CharacterEllipsis\" />", app, StringComparison.Ordinal);
        Assert.Contains("<Setter Property=\"ToolTip.Tip\" Value=\"{Binding Text, RelativeSource={RelativeSource Self}}\" />", app, StringComparison.Ordinal);
        Assert.Contains("AddHandler(DataGridCell.PointerEnteredEvent, DataGridCellPointerEntered", codeBehind, StringComparison.Ordinal);
        Assert.Contains("private void DataGridCellPointerEntered", codeBehind, StringComparison.Ordinal);
        Assert.Contains("ToolTip.SetTip(cell", codeBehind, StringComparison.Ordinal);
        Assert.Contains("CopyValueForCell(cell, column)", codeBehind, StringComparison.Ordinal);
        Assert.Contains("private static DataGridColumn? ColumnForCell", codeBehind, StringComparison.Ordinal);
    }

    [Fact]
    public void Table_column_layout_uses_stable_header_ids_and_legacy_layout_fallback()
    {
        var root = FindRepositoryRoot();
        var codeBehind = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "MainWindow.axaml.cs"));

        Assert.Contains("return header;", codeBehind, StringComparison.Ordinal);
        Assert.Contains("return $\"column-{index}\";", codeBehind, StringComparison.Ordinal);
        Assert.Contains("private static TableColumnLayout? LayoutForColumn", codeBehind, StringComparison.Ordinal);
        Assert.Contains("byColumn.TryGetValue(columnId", codeBehind, StringComparison.Ordinal);
        Assert.Contains("var legacySuffix = $\":{columnId}\";", codeBehind, StringComparison.Ordinal);
        Assert.Contains("item.Key.EndsWith(legacySuffix, StringComparison.Ordinal)", codeBehind, StringComparison.Ordinal);
    }

    [Fact]
    public void Main_window_and_project_package_podlord_brand_assets()
    {
        var root = FindRepositoryRoot();
        var app = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "App.axaml"));
        var project = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "Podlord.App.csproj"));
        var window = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "MainWindow.axaml"));
        var iconPath = Path.Combine(root, "src", "Podlord.App", "Assets", "Brand", "Icons", "podlord.ico");
        var logoPath = Path.Combine(root, "src", "Podlord.App", "Assets", "Brand", "Logo", "podlord-logo-transparent.png");

        Assert.Contains("<ApplicationIcon>Assets\\Brand\\Icons\\podlord.ico</ApplicationIcon>", project, StringComparison.Ordinal);
        Assert.Contains("Name=\"Podlord\"", app, StringComparison.Ordinal);
        Assert.Contains("Icon=\"avares://Podlord.App/Assets/Brand/Icons/podlord-icon-256.png\"", window, StringComparison.Ordinal);
        Assert.Contains("IsResourceLogoVisible", window, StringComparison.Ordinal);
        Assert.Contains("podlord-logo-transparent.png", window, StringComparison.Ordinal);
        Assert.Contains("PlLogoSurfaceBrush", app, StringComparison.Ordinal);
        Assert.DoesNotContain("Classes=\"logoPlate\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("Background=\"#D0070A05\"", window, StringComparison.Ordinal);
        Assert.Contains("BorderThickness=\"0\"", window, StringComparison.Ordinal);
        Assert.Contains("Label=\"Imported\"", window, StringComparison.Ordinal);
        Assert.True(File.Exists(iconPath));
        Assert.True(File.Exists(logoPath));
        Assert.Equal([0, 0, 1, 0], File.ReadAllBytes(iconPath).Take(4).ToArray());
    }

    [Fact]
    public void Main_window_uses_sources_menu_without_duplicate_session_button()
    {
        var root = FindRepositoryRoot();
        var window = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "MainWindow.axaml"));
        var app = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "App.axaml"));
        var catalog = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "AppThemeCatalog.cs"));
        var viewModel = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "MainWindowViewModel.cs"));
        var workspaceModels = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "WorkspaceModels.cs"));
        var radarWaterLayer = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "RadarWaterLayer.cs"));
        var radarWaterModel = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "RadarWaterModel.cs"));
        var codeBehind = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "MainWindow.axaml.cs"));
        var kubernetesProject = File.ReadAllText(Path.Combine(root, "src", "Podlord.Kubernetes", "Podlord.Kubernetes.csproj"));
        var kubernetesService = File.ReadAllText(Path.Combine(root, "src", "Podlord.Kubernetes", "KubernetesResourceService.cs"));

        Assert.Contains("ActivateCommand", window, StringComparison.Ordinal);
        Assert.Contains("RadarSourceLabel", window, StringComparison.Ordinal);
        Assert.Contains("Classes=\"radarSourceButton\"", window, StringComparison.Ordinal);
        Assert.Contains("SourcesTitleText", window, StringComparison.Ordinal);
        Assert.Contains("ToolTip.Tip=\"{Binding RenameSourceTooltipText}\"", window, StringComparison.Ordinal);
        Assert.Contains("ToolTip.Tip=\"{Binding DeleteSourceTooltipText}\"", window, StringComparison.Ordinal);
        Assert.Contains("Click=\"SourcesWorkspaceClicked\"", window, StringComparison.Ordinal);
        Assert.Contains("Header=\"{Binding SettingsSourcesText}\"", window, StringComparison.Ordinal);
        Assert.Contains("SelectedIndex=\"{Binding SelectedSettingsTabIndex}\"", window, StringComparison.Ordinal);
        Assert.Contains("SelectedSettingsTabIndex = 5;", viewModel, StringComparison.Ordinal);
        Assert.Contains("SelectedItem=\"{Binding SelectedSource}\"", window, StringComparison.Ordinal);
        Assert.Contains("Binding=\"{Binding Context, Mode=TwoWay, UpdateSourceTrigger=LostFocus}\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("SelectedItem=\"{Binding FilterName, Mode=TwoWay}\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("IsVisible=\"{Binding IsSourcesWorkspace}\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("Text=\"WARNINGS\"", window, StringComparison.Ordinal);
        Assert.Contains("OpenSourcesSettings();", codeBehind, StringComparison.Ordinal);
        Assert.Contains("OpenSourcesSettings();", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("Click=\"SaveSourceClicked\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("SelectedSourceSafety", window + viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("Header=\"Safety\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("Content=\"{Binding ImportedSourcesLabel}\"", window, StringComparison.Ordinal);
        Assert.Contains("CanUserResizeColumns=\"True\"", window, StringComparison.Ordinal);
        Assert.Contains("Classes.active=\"{Binding IsResourcesNavActive}\"", window, StringComparison.Ordinal);
        Assert.Contains("Classes.active=\"{Binding IsSettingsNavActive}\"", window, StringComparison.Ordinal);
        Assert.Contains("Content=\"◀\"", window, StringComparison.Ordinal);
        Assert.Contains("Content=\"▶\"", window, StringComparison.Ordinal);
        Assert.Contains("Kind=\"{Binding GlyphKind}\"", window, StringComparison.Ordinal);
        Assert.Contains("local:ColumnPlaqueHeader", window, StringComparison.Ordinal);
        Assert.Contains("Classes=\"minimapFrame\"", window, StringComparison.Ordinal);
        Assert.Contains("Classes=\"minimapGlass\"", window, StringComparison.Ordinal);
        Assert.Contains("PlRadarWaterBrush", app, StringComparison.Ordinal);
        Assert.Contains("Kind=\"{Binding DisplayKind}\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("ItemsSource=\"{Binding RadarLinks}\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("RotateTransform Angle=\"{Binding Angle}\"", window, StringComparison.Ordinal);
        Assert.Contains("IsVisible=\"{Binding ShowProblemGlyph}\"", window, StringComparison.Ordinal);
        Assert.Contains("BorderBrush=\"{Binding BorderBrush}\"", window, StringComparison.Ordinal);
        Assert.Contains("Classes=\"commandConsole\"", window, StringComparison.Ordinal);
        Assert.Contains("Classes=\"selectedUnitCard\"", window, StringComparison.Ordinal);
        Assert.Contains("Content=\"{Binding ActivityText}\"", window, StringComparison.Ordinal);
        Assert.Contains("ApplyServerSideActionText", window, StringComparison.Ordinal);
        Assert.Contains("RequestAuditRows", window, StringComparison.Ordinal);
        Assert.Contains("GraphicsQualityOptions", window, StringComparison.Ordinal);
        Assert.Contains("GraphicsQualitySetting", window, StringComparison.Ordinal);
        Assert.Contains("AnimationIntensitySetting", window, StringComparison.Ordinal);
        Assert.Contains("AnimationIntensityLabel", window, StringComparison.Ordinal);
        Assert.Contains("RadarWaterEnabledSetting", window, StringComparison.Ordinal);
        Assert.Contains("RadarWaterSpeedSetting", window, StringComparison.Ordinal);
        Assert.Contains("RadarWaterSpeedLabel", window, StringComparison.Ordinal);
        Assert.Contains("Classes=\"settingRow\"", window, StringComparison.Ordinal);
        Assert.Contains("Text=\"{Binding RadarWaterSpeedHelpText}\"", window, StringComparison.Ordinal);
        Assert.Contains("Text=\"{Binding AnimationHelpText}\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("RadarAutoFollowAlertsSetting", window, StringComparison.Ordinal);
        Assert.DoesNotContain("RadarAutoFollowHelpText", window, StringComparison.Ordinal);
        Assert.Contains("Border.settingRow", app, StringComparison.Ordinal);
        Assert.Contains("TextBlock.settingLabel", app, StringComparison.Ordinal);
        Assert.Contains("TextBlock.settingHelp", app, StringComparison.Ordinal);
        Assert.Contains("Rectangle.radarAnnouncePulse", app, StringComparison.Ordinal);
        Assert.Contains("IterationCount=\"INFINITE\"", app, StringComparison.Ordinal);
        Assert.Contains("InactiveSyncOptions", viewModel, StringComparison.Ordinal);
        Assert.Contains("InactiveSyncSetting", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("InactiveSyncDescription", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("RequestHardLimitSetting", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("RequestHardLimitDescription", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("RequestHardLimitOptions", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("ItemsSource=\"{Binding RequestHardLimitOptions}\"", window, StringComparison.Ordinal);
        Assert.Contains("SelectedItem=\"{Binding RequestHardLimitSetting}\"", window, StringComparison.Ordinal);
        Assert.Contains("RequestHardLimitPerMinute", kubernetesService + viewModel, StringComparison.Ordinal);
        var localizer = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "PodlordLocalizer.cs"));
        Assert.Contains("[\"settings.requestHardLimit\"] = \"Request limit\"", localizer, StringComparison.Ordinal);
        Assert.Contains("[\"settings.alerts\"] = \"Alerts\"", localizer, StringComparison.Ordinal);
        Assert.Contains("Header=\"{Binding SettingsAlertsText}\"", window, StringComparison.Ordinal);
        Assert.Contains("ItemsSource=\"{Binding AlertRules}\"", window, StringComparison.Ordinal);
        Assert.Contains("AlertActivationText", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("DataContext.AlertActiveText", window, StringComparison.Ordinal);
        Assert.Contains("DataContext.AlertNameText", window, StringComparison.Ordinal);
        Assert.Contains("DataContext.AlertWhenText", window, StringComparison.Ordinal);
        Assert.Contains("DataContext.AlertActionsText", window, StringComparison.Ordinal);
        Assert.Contains("DataContext.AlertSoundText", window, StringComparison.Ordinal);
        Assert.DoesNotContain("ItemsSource=\"{Binding ActiveAlerts}\"", window, StringComparison.Ordinal);
        Assert.Contains("ActiveStateText", window, StringComparison.Ordinal);
        Assert.Contains("Click=\"AddAlertRuleClicked\"", window, StringComparison.Ordinal);
        Assert.Contains("Click=\"DuplicateAlertRuleClicked\"", window, StringComparison.Ordinal);
        Assert.Contains("Click=\"DeleteAlertRuleClicked\"", window, StringComparison.Ordinal);
        Assert.Contains("Click=\"SaveAlertRulesClicked\"", window, StringComparison.Ordinal);
        Assert.Contains("Click=\"ToggleAlertRuleClicked\"", window, StringComparison.Ordinal);
        Assert.Contains("ActivationGlyphKind", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("MatcherGroups", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("FieldChoices", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("ExampleOptions", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("colorPicker:ColorPicker", window, StringComparison.Ordinal);
        Assert.Contains("SelectedColor", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("Click=\"AlertColorStatusClicked\"", window, StringComparison.Ordinal);
        Assert.Contains("Click=\"AlertColorNoneClicked\"", window, StringComparison.Ordinal);
        Assert.Contains("AnimationChoices", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("ZoomChoices", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("Click=\"RemoveAlertMatcherGroupClicked\"", window, StringComparison.Ordinal);
        Assert.Contains("Click=\"RemoveAlertMatcherCriterionClicked\"", window, StringComparison.Ordinal);
        Assert.Contains("Click=\"AddAlertMatcherGroupClicked\"", window, StringComparison.Ordinal);
        Assert.Contains("Click=\"AddAlertMatcherCriterionClicked\"", window, StringComparison.Ordinal);
        Assert.Contains("Click=\"PreviewAlertSoundClicked\"", window, StringComparison.Ordinal);
        Assert.Contains("Click=\"SelectAlertSoundClicked\"", window, StringComparison.Ordinal);
        Assert.Contains("Click=\"OpenAlertSoundSourceClicked\"", window, StringComparison.Ordinal);
        Assert.Contains("Click=\"PreviewAlertZoomClicked\"", window, StringComparison.Ordinal);
        Assert.Contains("IsVisible=\"{Binding HasSound}\"", window, StringComparison.Ordinal);
        Assert.Contains("IsVisible=\"{Binding HasZoom}\"", window, StringComparison.Ordinal);
        Assert.Contains("Click=\"ToggleAudioMuteClicked\"", window, StringComparison.Ordinal);
        Assert.Contains("AudioMuteGlyph", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("AudioMuteText", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("SoundSearch", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("FilteredSoundItems", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("DurationChoices", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("ColorDurationChoice", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("AnimationDurationChoice", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("Kind=\"Sound\"", window, StringComparison.Ordinal);
        Assert.Contains("Classes.dropdown=\"True\"", window, StringComparison.Ordinal);
        Assert.Contains("Resource.IsPulseAnimation", window, StringComparison.Ordinal);
        Assert.Contains("AlertResourceBrushConverter", window + File.ReadAllText(Path.Combine(FindRepositoryRoot(), "src", "Podlord.App", "App.axaml")), StringComparison.Ordinal);
        Assert.DoesNotContain("Guided rules use", window, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("Process.Start", File.ReadAllText(Path.Combine(FindRepositoryRoot(), "src", "Podlord.App", "AlertSoundPlayer.cs")), StringComparison.Ordinal);
        Assert.Contains("AlertAuthorText", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("AlertSourceText", window + viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("AlertAssetText", window, StringComparison.Ordinal);
        Assert.DoesNotContain("Header=\"Severity\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("Health bar", window + viewModel, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("AlertRuleStore.Load", viewModel, StringComparison.Ordinal);
        Assert.Contains("AlertRuleEvaluator.Evaluate", viewModel, StringComparison.Ordinal);
        Assert.Contains("activeAlertActionsByResourceId", viewModel, StringComparison.Ordinal);
        Assert.Contains("AlertSoundCatalog.BuiltIn", viewModel, StringComparison.Ordinal);
        Assert.Contains("Header=\"{Binding SettingsSyncText}\"", window, StringComparison.Ordinal);
        Assert.Contains("ShouldRunBackgroundRefresh", viewModel, StringComparison.Ordinal);
        Assert.Contains("Header=\"{Binding SettingsGraphicsText}\"", window, StringComparison.Ordinal);
        Assert.Contains("Header=\"{Binding SettingsDiagnosticsText}\"", window, StringComparison.Ordinal);
        Assert.True(window.IndexOf("SettingsAlertsText", StringComparison.Ordinal) < window.IndexOf("SettingsAppearanceText", StringComparison.Ordinal));
        Assert.True(window.IndexOf("SettingsAppearanceText", StringComparison.Ordinal) < window.IndexOf("SettingsDiagnosticsText", StringComparison.Ordinal));
        Assert.True(window.IndexOf("SettingsDiagnosticsText", StringComparison.Ordinal) < window.IndexOf("SettingsGraphicsText", StringComparison.Ordinal));
        Assert.True(window.IndexOf("SettingsGraphicsText", StringComparison.Ordinal) < window.IndexOf("SettingsPrivacyText", StringComparison.Ordinal));
        Assert.True(window.IndexOf("SettingsPrivacyText", StringComparison.Ordinal) < window.IndexOf("SettingsSourcesText", StringComparison.Ordinal));
        Assert.True(window.IndexOf("SettingsSourcesText", StringComparison.Ordinal) < window.IndexOf("SettingsSyncText", StringComparison.Ordinal));
        Assert.True(window.IndexOf("SettingsSyncText", StringComparison.Ordinal) < window.IndexOf("SettingsWorkspaceText", StringComparison.Ordinal));
        var sourceSettingsBlock = Regex.Match(window, "Header=\"\\{Binding SettingsSourcesText\\}\"(?<block>.*?)Header=\"\\{Binding SettingsSyncText\\}\"", RegexOptions.Singleline).Groups["block"].Value;
        Assert.DoesNotContain("Header=\"Hash\"", sourceSettingsBlock, StringComparison.Ordinal);
        Assert.DoesNotContain("Header=\"Status\"", sourceSettingsBlock, StringComparison.Ordinal);
        Assert.DoesNotContain("Header=\"File\"", sourceSettingsBlock, StringComparison.Ordinal);
        Assert.Contains("HorizontalAlignment=\"Stretch\"", window, StringComparison.Ordinal);
        Assert.Contains("SelectedItem=\"{Binding SelectedResourceRow}\"", window, StringComparison.Ordinal);
        Assert.Contains("ClusterPulseItems", window + viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("Text=\"CLUSTER METRICS\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("ClusterMetricCards", window, StringComparison.Ordinal);
        Assert.DoesNotContain("ClusterTopRows", window + viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("PulseTopRow", workspaceModels, StringComparison.Ordinal);
        Assert.Contains("Tag=\"CPU\"", window, StringComparison.Ordinal);
        Assert.Contains("Tag=\"Memory\"", window, StringComparison.Ordinal);
        Assert.Contains("CpuCompactDisplay", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("MemoryCompactDisplay", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("CpuMetricDetail", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("MemoryMetricDetail", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("MetricHealthBrushConverter", app, StringComparison.Ordinal);
        Assert.Contains("Padding=\"8,3\"", window, StringComparison.Ordinal);
        Assert.Contains("SuggestionLeft", window + workspaceModels, StringComparison.Ordinal);
        Assert.Contains("MetricTooltip", window + workspaceModels, StringComparison.Ordinal);
        Assert.Contains("MergeCachedMetricItems", viewModel, StringComparison.Ordinal);
        Assert.Contains("CachedMetricItems", viewModel, StringComparison.Ordinal);
        Assert.Contains("MetricRowsFromDetails", viewModel, StringComparison.Ordinal);
        Assert.Contains("HasCpuMetricBar", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("HasMemoryMetricBar", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("HasCpuMetricTextOnly", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("HasNetworkMetricInfo", window + viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("HasNoCpuMetricBar", window + viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("HasNoMemoryMetricBar", window + viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("HasNoStorageMetricBar", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("TextWrapping=\"WrapWithOverflow\"", window, StringComparison.Ordinal);
        Assert.Contains("ClipToBounds=\"False\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("Header=\"Metrics\"", window, StringComparison.Ordinal);
        Assert.Contains("SelectedInspectorTabIndex == 4", viewModel, StringComparison.Ordinal);
        Assert.Contains("x:Name=\"PulseStripScroller\"", window, StringComparison.Ordinal);
        Assert.Contains("Background=\"Transparent\"", window, StringComparison.Ordinal);
        Assert.Contains("HorizontalScrollBarVisibility=\"Hidden\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("PointerWheelChanged=\"PulseStripPointerWheelChanged\"", window, StringComparison.Ordinal);
        Assert.Contains("PulseStripPointerWheelChanged", window + codeBehind, StringComparison.Ordinal);
        Assert.Contains("PulseStripPointerMoved", window + codeBehind, StringComparison.Ordinal);
        Assert.Contains("PulseStripScroller.AddHandler", codeBehind, StringComparison.Ordinal);
        Assert.Contains("SetPulseStripOffset", codeBehind, StringComparison.Ordinal);
        Assert.Contains("Extent.Width - PulseStripScroller.Viewport.Width", codeBehind, StringComparison.Ordinal);
        Assert.DoesNotContain("new PulseMetricCard(\"Network\", \"-\"", viewModel, StringComparison.Ordinal);
        Assert.Contains("RadarSourceButtonClicked", window + codeBehind, StringComparison.Ordinal);
        Assert.Contains("flyout.ShowAt(button)", codeBehind, StringComparison.Ordinal);
        Assert.Contains("DataGridColumnHeader.PointerPressedEvent", codeBehind, StringComparison.Ordinal);
        Assert.Contains("ColumnHeaderPointerMoved", codeBehind, StringComparison.Ordinal);
        Assert.Contains("ColumnResizeEdge", codeBehind, StringComparison.Ordinal);
        Assert.Contains("position.X >= width - edgeWidth", codeBehind, StringComparison.Ordinal);
        Assert.Contains("ColumnForHeader", codeBehind, StringComparison.Ordinal);
        Assert.Contains("suppressNextHeaderSortClick", codeBehind, StringComparison.Ordinal);
        Assert.Contains("OpenColumnVisibilityMenu", codeBehind, StringComparison.Ordinal);
        Assert.Contains("MenuItemToggleType.None", codeBehind, StringComparison.Ordinal);
        Assert.Contains("ColumnPinIcon", codeBehind, StringComparison.Ordinal);
        Assert.Contains("CanUserReorderColumns", app, StringComparison.Ordinal);
        Assert.Contains("Focusable\" Value=\"False\"", app, StringComparison.Ordinal);
        Assert.Contains("TableColumnLayout", viewModel + codeBehind, StringComparison.Ordinal);
        Assert.Contains("ColumnDisplayIndexChanged", codeBehind, StringComparison.Ordinal);
        Assert.Contains("SaveTableLayout", codeBehind, StringComparison.Ordinal);
        Assert.Contains("ApplyTableLayout", codeBehind, StringComparison.Ordinal);
        Assert.Contains("ResourceSortGlyphFor", viewModel + codeBehind, StringComparison.Ordinal);
        Assert.Contains("EventSortGlyphFor", viewModel + codeBehind, StringComparison.Ordinal);
        Assert.Contains("Classes.Add(\"sortGlyph\")", codeBehind, StringComparison.Ordinal);
        Assert.Contains("IsVisible=\"{Binding Resource.HasCpuMetricBar}\"", window, StringComparison.Ordinal);
        Assert.Contains("IsVisible=\"{Binding Resource.HasMemoryMetricBar}\"", window, StringComparison.Ordinal);
        Assert.Contains("IsVisible=\"{Binding Resource.HasStorageMetricBar}\"", window, StringComparison.Ordinal);
        Assert.Contains("<Style Selector=\"ProgressBar\">", app, StringComparison.Ordinal);
        Assert.Contains("<Setter Property=\"MinWidth\" Value=\"0\" />", app, StringComparison.Ordinal);
        Assert.Contains("ClipToBounds=\"True\" MinWidth=\"0\"", window, StringComparison.Ordinal);
        Assert.Contains("Height=\"5\"", window, StringComparison.Ordinal);
        Assert.Contains("MinWidth=\"0\"", window, StringComparison.Ordinal);
        Assert.Contains("RestartBrushConverter", app, StringComparison.Ordinal);
        Assert.Contains("Foreground=\"{Binding Resource.Restarts, Converter={StaticResource RestartBrushConverter}}\"", window, StringComparison.Ordinal);
        Assert.Contains("Foreground=\"{Binding Resource, Converter={StaticResource ProblemBrushConverter}}\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("Text=\"{Binding Metrics}\"", window, StringComparison.Ordinal);
        Assert.Contains("PortForwardBadgeConverter", window + app, StringComparison.Ordinal);
        Assert.Contains("PortForwardEligibilityConverter", window + app, StringComparison.Ordinal);
        Assert.Contains("PointerPressed=\"WindowPointerPressed\"", window, StringComparison.Ordinal);
        Assert.Contains("PointerWheelChanged=\"RadarPointerWheelChanged\"", window, StringComparison.Ordinal);
        Assert.Contains("x:Name=\"RightSidebarShell\"", window, StringComparison.Ordinal);
        Assert.Contains("RightSidebarResizePressed", window + codeBehind, StringComparison.Ordinal);
        Assert.Contains("RightSidebarResizeMoved", window + codeBehind, StringComparison.Ordinal);
        Assert.Contains("RightSidebarShell.Width = Math.Clamp", codeBehind, StringComparison.Ordinal);
        Assert.Contains("SetRadarPanelWidth", viewModel + codeBehind, StringComparison.Ordinal);
        Assert.Contains("SizeChanged=\"RadarViewportSizeChanged\"", window, StringComparison.Ordinal);
        Assert.Contains("SetRadarViewport", viewModel + codeBehind, StringComparison.Ordinal);
        Assert.Contains("Height=\"{Binding RadarPanelHeight}\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("<Viewbox Stretch=\"Fill\">", window, StringComparison.Ordinal);
        Assert.Contains("PointerEntered=\"RadarPointerEntered\"", window, StringComparison.Ordinal);
        Assert.Contains("PointerMoved=\"RadarPointerMoved\"", window, StringComparison.Ordinal);
        Assert.Contains("PointerReleased=\"RadarPointerReleased\"", window, StringComparison.Ordinal);
        Assert.Contains("<local:RadarWaterLayer", window, StringComparison.Ordinal);
        Assert.Contains("PanX=\"{Binding RadarPanX}\"", window, StringComparison.Ordinal);
        Assert.Contains("PanY=\"{Binding RadarPanY}\"", window, StringComparison.Ordinal);
        Assert.Contains("Zoom=\"{Binding RadarZoom}\"", window, StringComparison.Ordinal);
        Assert.Contains("ActivityRate=\"{Binding RadarWaterActivityRate}\"", window, StringComparison.Ordinal);
        Assert.Contains("SpeedPercent=\"{Binding RadarWaterSpeedPercent}\"", window, StringComparison.Ordinal);
        Assert.Contains("PauseAnimation=\"{Binding IsRadarWaterPaused}\"", window, StringComparison.Ordinal);
        Assert.Contains("IsVisible=\"{Binding IsRadarWaterVisible}\"", window, StringComparison.Ordinal);
        Assert.Contains("ZIndex=\"0\"", window, StringComparison.Ordinal);
        Assert.Contains("ZIndex=\"1\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("ItemsSource=\"{Binding RadarWaterCells}\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("ToolTip.Tip=\"{Binding RadarZoomLabel}\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("Classes=\"radarZoomButton\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("Click=\"RadarZoomInClicked\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("Click=\"RadarZoomOutClicked\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("Click=\"RadarZoomResetClicked\"", window, StringComparison.Ordinal);
        Assert.Contains("Classes=\"radarAnnounceBlink\"", window, StringComparison.Ordinal);
        Assert.Contains("Classes=\"radarAnnouncePulse\"", window, StringComparison.Ordinal);
        Assert.Contains("Classes=\"radarAnnounceSweep\"", window, StringComparison.Ordinal);
        Assert.Contains("Classes=\"radarAnnounceOutline\"", window, StringComparison.Ordinal);
        Assert.Contains("Fill=\"{Binding Brush}\"", window, StringComparison.Ordinal);
        Assert.Contains("Classes=\"resourceAnnounceBlink\"", window, StringComparison.Ordinal);
        Assert.Contains("Classes=\"resourceAnnouncePulse\"", window, StringComparison.Ordinal);
        Assert.Contains("Classes=\"resourceAnnounceSweep\"", window, StringComparison.Ordinal);
        Assert.Contains("Classes=\"resourceAnnounceOutline\"", window, StringComparison.Ordinal);
        Assert.Contains("IsVisible=\"{Binding IsBlinkAnimation}\"", window, StringComparison.Ordinal);
        Assert.Contains("IsVisible=\"{Binding IsPulseAnimation}\"", window, StringComparison.Ordinal);
        Assert.Contains("IsVisible=\"{Binding IsSweepAnimation}\"", window, StringComparison.Ordinal);
        Assert.Contains("IsVisible=\"{Binding IsOutlineAnimation}\"", window, StringComparison.Ordinal);
        Assert.Contains("Opacity=\"{Binding Opacity}\"", window, StringComparison.Ordinal);
        Assert.Contains("Label=\"Name\"", window, StringComparison.Ordinal);
        Assert.Contains("Label=\"Reason\"", window, StringComparison.Ordinal);
        Assert.Contains("Label=\"Message\"", window, StringComparison.Ordinal);
        Assert.Contains("CellPointerPressed=\"CopyableTableCellPointerPressed\"", window, StringComparison.Ordinal);
        Assert.Contains("Click=\"InspectorTabClicked\"", window + codeBehind, StringComparison.Ordinal);
        Assert.Contains("Classes=\"inspectorTab\"", window, StringComparison.Ordinal);
        Assert.Contains("Classes.active=\"{Binding IsInspectorYamlActive}\"", window, StringComparison.Ordinal);
        Assert.Contains("IsVisible=\"{Binding CanPortForwardSelectedResource}\"", window, StringComparison.Ordinal);
        Assert.Contains("IsVisible=\"{Binding CanDeleteSelectedResource}\"", window, StringComparison.Ordinal);
        Assert.Contains("Click=\"DeleteSelectedResourceClicked\"", window + codeBehind, StringComparison.Ordinal);
        Assert.Contains("DeleteSelectedResourceAsync", viewModel + codeBehind, StringComparison.Ordinal);
        Assert.Contains("IsVisible=\"{Binding Converter={StaticResource PortForwardEligibilityConverter}}\"", window, StringComparison.Ordinal);
        Assert.Contains("InspectorValuesText", window, StringComparison.Ordinal);
        Assert.Contains("ItemsSource=\"{Binding ResourceValues}\"", window, StringComparison.Ordinal);
        Assert.Contains("Text=\"{Binding DisplayValue}\"", window, StringComparison.Ordinal);
        Assert.Contains("TextWrapping=\"Wrap\"", window, StringComparison.Ordinal);
        Assert.Contains("MaxLines=\"8\"", window, StringComparison.Ordinal);
        Assert.Contains("CopyActionsForCell", codeBehind, StringComparison.Ordinal);
        Assert.Contains("DataGridCellPointerPressedEventArgs", codeBehind, StringComparison.Ordinal);
        Assert.Contains("ref.menuOpen", codeBehind, StringComparison.Ordinal);
        Assert.Contains("copy.decodedSecret", codeBehind, StringComparison.Ordinal);
        Assert.Contains("copy.rawBase64Secret", codeBehind, StringComparison.Ordinal);
        Assert.Contains("CopyResourceValueRawClicked", codeBehind, StringComparison.Ordinal);
        Assert.Contains("CopyResourceValueDecodedClicked", codeBehind, StringComparison.Ordinal);
        Assert.Contains("Content=\"RAW\"", window, StringComparison.Ordinal);
        Assert.Contains("Content=\"DEC\"", window, StringComparison.Ordinal);
        Assert.Contains("return Preview(PreferredCopyValue);", workspaceModels, StringComparison.Ordinal);
        Assert.Contains("HasKnownResourceReference", codeBehind, StringComparison.Ordinal);
        Assert.Contains("RadarPointerWheelChanged", codeBehind, StringComparison.Ordinal);
        Assert.Contains("RadarPointerPressed", codeBehind, StringComparison.Ordinal);
        Assert.Contains("TryHandleRadarPanKey", codeBehind, StringComparison.Ordinal);
        Assert.Contains("TryHandleRadarZoomKey", codeBehind, StringComparison.Ordinal);
        Assert.Contains("Key.OemPlus", codeBehind, StringComparison.Ordinal);
        Assert.Contains("Key.OemMinus", codeBehind, StringComparison.Ordinal);
        Assert.Contains("Key.NumPad0", codeBehind, StringComparison.Ordinal);
        Assert.Contains("e.Pointer.Capture(control)", codeBehind, StringComparison.Ordinal);
        Assert.Contains("e.Pointer.Capture(null)", codeBehind, StringComparison.Ordinal);
        Assert.Contains("FocusEvent", viewModel, StringComparison.Ordinal);
        Assert.Contains("EventReason", viewModel, StringComparison.Ordinal);
        Assert.Contains("IsAnnouncing", viewModel, StringComparison.Ordinal);
        Assert.Contains("RadarFilterScope.From", viewModel, StringComparison.Ordinal);
        Assert.Contains("BuildDisplayCacheQuery", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("RadarWaterIntervalFor", viewModel, StringComparison.Ordinal);
        Assert.Contains("RadarFilteredBrush", viewModel, StringComparison.Ordinal);
        var normalizedCodeBehind = codeBehind.ReplaceLineEndings("\n");
        Assert.Contains("UpdateInspectorLayout();", codeBehind, StringComparison.Ordinal);
        Assert.Contains("private void CollapseInspectorLayout()", codeBehind, StringComparison.Ordinal);
        Assert.Contains("private void ExpandInspectorLayout()", codeBehind, StringComparison.Ordinal);
        Assert.Contains("MainContentGrid.RowDefinitions[1].Height = new GridLength(0, GridUnitType.Pixel);", codeBehind, StringComparison.Ordinal);
        Assert.Contains("MainContentGrid.RowDefinitions[2].Height = new GridLength(0, GridUnitType.Pixel);", codeBehind, StringComparison.Ordinal);
        Assert.Contains("MainContentGrid.RowDefinitions[1].Height = new GridLength(14, GridUnitType.Pixel);", codeBehind, StringComparison.Ordinal);
        Assert.Contains("case Key.Up:\n                viewModel.PanRadar(0, step);", normalizedCodeBehind, StringComparison.Ordinal);
        Assert.Contains("case Key.Down:\n                viewModel.PanRadar(0, -step);", normalizedCodeBehind, StringComparison.Ordinal);
        Assert.Contains("case Key.Left:\n                viewModel.PanRadar(step, 0);", normalizedCodeBehind, StringComparison.Ordinal);
        Assert.Contains("case Key.Right:\n                viewModel.PanRadar(-step, 0);", normalizedCodeBehind, StringComparison.Ordinal);
        Assert.Contains("activeContextMenu", codeBehind, StringComparison.Ordinal);
        Assert.Contains("CloseActiveContextMenu(cancelPendingHold: false)", codeBehind, StringComparison.Ordinal);
        Assert.Contains("CloseActiveContextMenu(cancelPendingHold: true)", codeBehind, StringComparison.Ordinal);
        Assert.Contains("menu.Closed += (_, _)", codeBehind, StringComparison.Ordinal);
        Assert.DoesNotContain("CopyTarget(", codeBehind, StringComparison.Ordinal);
        Assert.Contains("PortContainerPortText", window, StringComparison.Ordinal);
        Assert.Contains("PortLocalPortText", window, StringComparison.Ordinal);
        Assert.Contains("Content=\"{Binding PortForwardActionLabel}\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("Content=\"START\" Click=\"StartPortForwardClicked\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("Content=\"STOP\" Click=\"StopPortForwardClicked\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("PortDeclaredPortsLabel", window, StringComparison.Ordinal);
        Assert.DoesNotContain("PortForwardStatusLine", window, StringComparison.Ordinal);
        Assert.Contains("Click=\"RemoveSourceClicked\"", window + codeBehind, StringComparison.Ordinal);
        Assert.Contains("ToolTip.Tip=\"{Binding RemoveSnapshotTooltipText}\"", window, StringComparison.Ordinal);
        Assert.Contains("Text=\"{Binding PresetName, UpdateSourceTrigger=PropertyChanged}\"", window, StringComparison.Ordinal);
        Assert.Contains("ItemsSource=\"{Binding VisibleSavedPresets}\"", window, StringComparison.Ordinal);
        Assert.Contains("Text=\"{Binding Name, Mode=OneWay}\"", window, StringComparison.Ordinal);
        Assert.Contains("SavedFilterNameLostFocus", window + codeBehind, StringComparison.Ordinal);
        Assert.Contains("SavedFilterNameGotFocus", window + codeBehind, StringComparison.Ordinal);
        Assert.Contains("SavedFilterNameKeyDown", window + codeBehind, StringComparison.Ordinal);
        Assert.Contains("RenameSavedFilter(preset", codeBehind + viewModel, StringComparison.Ordinal);
        Assert.Contains("Click=\"RenameSavedFilterClicked\"", window + codeBehind, StringComparison.Ordinal);
        Assert.Contains("Click=\"DeleteSavedFilterClicked\"", window, StringComparison.Ordinal);
        Assert.Contains("ToolTip.Tip=\"{Binding RenameFilterTooltipText}\"", window, StringComparison.Ordinal);
        Assert.Contains("ToolTip.Tip=\"{Binding DeleteFilterTooltipText}\"", window, StringComparison.Ordinal);
        Assert.Contains("Command=\"{Binding AddSearchCommand}\"", window, StringComparison.Ordinal);
        Assert.Contains("<local:KindGlyph Kind=\"Add\" Fill=\"{StaticResource PlSuccessBrush}\" Width=\"14\" Height=\"14\" />", window, StringComparison.Ordinal);
        Assert.Contains("Command=\"{Binding RemoveCommand}\"", window, StringComparison.Ordinal);
        Assert.Contains("<local:KindGlyph Kind=\"Trash\" Fill=\"{StaticResource PlWarningBrush}\" Width=\"13\" Height=\"13\" />", window, StringComparison.Ordinal);
        Assert.Contains("Click=\"CloseSearchClicked\"", window + codeBehind, StringComparison.Ordinal);
        Assert.Contains("viewModel.CloseSearchForCurrentWorkspace();", codeBehind, StringComparison.Ordinal);
        Assert.DoesNotContain("Content=\"{Binding DataContext.DeleteActionText", window, StringComparison.Ordinal);
        Assert.Contains("IsVisible=\"{Binding IsSelectedResourceLoggable}\"", window, StringComparison.Ordinal);
        Assert.Contains("ItemsSource=\"{Binding FocusMetrics}\"", window, StringComparison.Ordinal);
        Assert.Contains("Label=\"Metric\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("ItemsSource=\"{Binding YamlSnippets}\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("InsertYamlSnippetClicked", window + codeBehind, StringComparison.Ordinal);
        Assert.Contains("YamlAssistStatus", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("avaloniaEdit:TextEditor", window, StringComparison.Ordinal);
        Assert.DoesNotContain("Classes=\"inspectorTabs\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("TabControl.inspectorTabs", app, StringComparison.Ordinal);
        Assert.Contains("Border.inspectorTabBar", app, StringComparison.Ordinal);
        Assert.Contains("Button.inspectorTab.active", app, StringComparison.Ordinal);
        Assert.Contains("IsVisible=\"{Binding IsInspectorYamlActive}\"", window, StringComparison.Ordinal);
        Assert.Contains("IsInspectorValuesActive", window + viewModel, StringComparison.Ordinal);
        Assert.Contains("x:Name=\"InspectorTabContentHost\"", window, StringComparison.Ordinal);
        Assert.Contains("SizeChanged=\"InspectorTabContentHostSizeChanged\"", window, StringComparison.Ordinal);
        Assert.Contains("UpdateYamlEditorHeight", codeBehind, StringComparison.Ordinal);
        Assert.DoesNotContain("MaxHeight=\"520\"", window, StringComparison.Ordinal);
        Assert.Contains("VerticalAlignment=\"Stretch\"", window, StringComparison.Ordinal);
        Assert.Contains("ShowLineNumbers=\"True\"", window, StringComparison.Ordinal);
        Assert.Contains("VerticalScrollBarVisibility=\"Auto\"", window, StringComparison.Ordinal);
        Assert.Contains("HorizontalScrollBarVisibility=\"Auto\"", window, StringComparison.Ordinal);
        Assert.Contains("SelectedInspectorTabIndex == 1", codeBehind, StringComparison.Ordinal);
        Assert.Contains("YamlSyntaxColorizer", codeBehind, StringComparison.Ordinal);
        Assert.Contains("AvaloniaEdit/Themes/Fluent/AvaloniaEdit.xaml", app, StringComparison.Ordinal);
        Assert.Contains("Avalonia.AvaloniaEdit", File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "Podlord.App.csproj")), StringComparison.Ordinal);
        Assert.Contains("Foreground=\"{Binding Cluster, Converter={StaticResource DeterministicBrushConverter}}\"", window, StringComparison.Ordinal);
        Assert.Contains("NodeReferenceConverter", window, StringComparison.Ordinal);
        Assert.Contains("Foreground=\"{Binding ImageSummary, Converter={StaticResource DeterministicBrushConverter}}\"", window, StringComparison.Ordinal);
        Assert.Contains("local:ResourceLinkButton", window, StringComparison.Ordinal);
        Assert.DoesNotContain("Foreground=\"#050806\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("Background=\"{Binding Cluster, Converter={StaticResource DeterministicBrushConverter}}\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("StackPanel Spacing=\"10\" Width=\"760\"", window, StringComparison.Ordinal);
        Assert.Contains("PlBackdropBrush", app, StringComparison.Ordinal);
        Assert.Contains("PlPanelMaterialBrush", app, StringComparison.Ordinal);
        Assert.Contains("PlInsetMaterialBrush", app, StringComparison.Ordinal);
        Assert.Contains("PlInspectorTextureBrush", app, StringComparison.Ordinal);
        Assert.Contains("PlSidebarTextureBrush", app, StringComparison.Ordinal);
        Assert.Contains("PlTableHeaderBrush", app, StringComparison.Ordinal);
        Assert.Contains("PlProgressTrackBrush", app, StringComparison.Ordinal);
        Assert.Contains("PlRadarGlassTextureBrush", app, StringComparison.Ordinal);
        Assert.Contains("PlBgAppBrush", app, StringComparison.Ordinal);
        Assert.Contains("PlBorderStrongBrush", app, StringComparison.Ordinal);
        Assert.Contains("ThemeVariantSetting", window, StringComparison.Ordinal);
        Assert.Contains("ThemeVariantOptions", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("BorderThickness\" Value=\"4\"", app, StringComparison.Ordinal);
        Assert.Contains("Imperial Ledger", catalog, StringComparison.Ordinal);
        Assert.Contains("Sirocco Command", catalog, StringComparison.Ordinal);
        Assert.Contains("Ironwood Warroom", catalog, StringComparison.Ordinal);
        Assert.Contains("Gunmetal Sector", catalog, StringComparison.Ordinal);
        Assert.Contains("Daylight Basic", catalog, StringComparison.Ordinal);
        Assert.Contains("ThemeVariantNames", catalog, StringComparison.Ordinal);
        foreach (var privateName in PrivateThemeNames())
        {
            Assert.DoesNotContain(privateName, catalog, StringComparison.OrdinalIgnoreCase);
            Assert.DoesNotContain(privateName, window, StringComparison.OrdinalIgnoreCase);
            Assert.DoesNotContain(privateName, app, StringComparison.OrdinalIgnoreCase);
        }
        Assert.Contains("native websocket port-forward", viewModel, StringComparison.Ordinal);
        Assert.Contains("StartPortForwardAsync", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("ResolveKubectl", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("StartPortForwardProcess", viewModel, StringComparison.Ordinal);
        Assert.Contains("PackageReference Include=\"KubernetesClient\"", kubernetesProject, StringComparison.Ordinal);
        Assert.Contains("WebSocketNamespacedPodPortForwardAsync", kubernetesService, StringComparison.Ordinal);
        Assert.Contains("StreamDemuxer", kubernetesService, StringComparison.Ordinal);
        Assert.Contains("Running: local computer 127.0.0.1", viewModel, StringComparison.Ordinal);
        Assert.Contains("BuildBackgroundWarmQuery", viewModel, StringComparison.Ordinal);
        Assert.Contains("CanHaveImages", viewModel, StringComparison.Ordinal);
        Assert.Contains("ImageOptions(row.ImageSummary)", viewModel, StringComparison.Ordinal);
        Assert.Contains("Id: null", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("Id: EmptyToNull(IdPicker.Expression)", viewModel, StringComparison.Ordinal);
        Assert.Contains("refreshInFlight", viewModel, StringComparison.Ordinal);
        Assert.Contains("RenderedSnapshotMatches", viewModel, StringComparison.Ordinal);
        Assert.Contains("await RefreshResourcesAsync(true, true)", viewModel, StringComparison.Ordinal);
        Assert.Contains("BackgroundRefreshIntervalFor", viewModel, StringComparison.Ordinal);
        Assert.Contains("return hasCachedRows ? TimeSpan.FromMinutes(4) : TimeSpan.FromSeconds(45);", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("radarWaterTimer", viewModel, StringComparison.Ordinal);
        Assert.Contains("if (!IsRadarIdle || !state.Settings().ScreensaverEnabled || !isWindowVisible)", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("private const double RadarWaterGridStep = RadarGridStep * 2;", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("VisibleRadarWaterCells", viewModel, StringComparison.Ordinal);
        Assert.Contains("RadarLifeBrushes", viewModel, StringComparison.Ordinal);
        Assert.Contains("if (IsSettingsWorkspace)", viewModel, StringComparison.Ordinal);
        Assert.Contains("ReferenceEquals(Brush, source.Brush)", workspaceModels, StringComparison.Ordinal);
        Assert.Contains("return;", workspaceModels, StringComparison.Ordinal);
        Assert.Contains("FocusRadarResourceAsync", viewModel, StringComparison.Ordinal);
        Assert.Contains("ZoomRadar", viewModel, StringComparison.Ordinal);
        Assert.Contains("ZoomRadarIn", viewModel, StringComparison.Ordinal);
        Assert.Contains("ZoomRadarOut", viewModel, StringComparison.Ordinal);
        Assert.Contains("ResetRadarView", viewModel, StringComparison.Ordinal);
        Assert.Contains("PanRadar", viewModel, StringComparison.Ordinal);
        Assert.Contains("RadarWaterActivityRate = telemetry.RequestsLastMinute", viewModel, StringComparison.Ordinal);
        Assert.Contains("RadarWaterSpeedPercent", viewModel, StringComparison.Ordinal);
        Assert.Contains("RadarWaterSpeed = (byte)Math.Clamp", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("RadarAutoFollowAlertsSetting", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("state.Settings().RadarAutoFollowAlerts", viewModel, StringComparison.Ordinal);
        Assert.Contains("StartRadarAutoFollow", viewModel, StringComparison.Ordinal);
        Assert.Contains("previousVisibleRadarAlertIds", viewModel, StringComparison.Ordinal);
        Assert.Contains("IsRadarWaterPaused = true", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("RadarWaterCells", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("Color.FromArgb(42", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("Color.FromArgb(52", viewModel, StringComparison.Ordinal);
        Assert.Contains("public sealed class RadarWaterLayer : Control", radarWaterLayer, StringComparison.Ordinal);
        Assert.Contains("AvaloniaProperty.Register<RadarWaterLayer, double>(nameof(PanX))", radarWaterLayer, StringComparison.Ordinal);
        Assert.Contains("AvaloniaProperty.Register<RadarWaterLayer, int>(nameof(ActivityRate))", radarWaterLayer, StringComparison.Ordinal);
        Assert.Contains("AvaloniaProperty.Register<RadarWaterLayer, int>(nameof(SpeedPercent), 45)", radarWaterLayer, StringComparison.Ordinal);
        Assert.Contains("AvaloniaProperty.Register<RadarWaterLayer, bool>(nameof(PauseAnimation))", radarWaterLayer, StringComparison.Ordinal);
        Assert.Contains("WaterIntervalFor(ActivityRate, SpeedPercent)", radarWaterLayer, StringComparison.Ordinal);
        Assert.Contains("460d - speed * 380d - activity * 40d", radarWaterModel, StringComparison.Ordinal);
        Assert.Contains("Math.Clamp(milliseconds, 60, 520)", radarWaterModel, StringComparison.Ordinal);
        Assert.Contains("InvalidateVisual();", radarWaterLayer, StringComparison.Ordinal);
        Assert.Contains("SyncTimer();", radarWaterLayer, StringComparison.Ordinal);
        Assert.Contains("context.DrawRectangle", radarWaterLayer, StringComparison.Ordinal);
        Assert.Contains("RadarSourceLabel", viewModel, StringComparison.Ordinal);
        Assert.Contains("RenameSourceRow", viewModel, StringComparison.Ordinal);
        Assert.Contains("AssignSourceRowFilter", viewModel, StringComparison.Ordinal);
        Assert.Contains("ImportKubeconfigDirectory", viewModel, StringComparison.Ordinal);
        Assert.Contains("LooksLikeKubeconfigYaml", viewModel, StringComparison.Ordinal);
        Assert.Contains("FocusSource", viewModel, StringComparison.Ordinal);
        Assert.Contains("isAnnouncing: announce", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("AdvanceRadarWater", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("RenderRadarWaterCells", viewModel, StringComparison.Ordinal);
        Assert.Contains("VisibleRadarCells", viewModel, StringComparison.Ordinal);
        Assert.Contains("SetAppFocus", viewModel, StringComparison.Ordinal);
        Assert.Contains("UpdateInspectorTabWork", viewModel, StringComparison.Ordinal);
        Assert.Contains("BeginFocusLoad", viewModel, StringComparison.Ordinal);
        Assert.Contains("OpenKnownResourceReference", viewModel, StringComparison.Ordinal);
        Assert.Contains("ResolveKnownResourceReference", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("Content=\"PREV\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("Content=\"NEXT\"", window, StringComparison.Ordinal);
        Assert.Contains("ActiveSessionChipLabel", window, StringComparison.Ordinal);
        Assert.DoesNotContain("SelectedItem=\"{Binding SelectedSession}\"", window, StringComparison.Ordinal);
        Assert.DoesNotContain("Text=\"SESSION\"", window, StringComparison.Ordinal);
        Assert.Contains("Rectangle.radarAnnouncePulse", app, StringComparison.Ordinal);
        Assert.Contains("<Animation Duration=\"0:0:1.35\" IterationCount=\"INFINITE\">", app, StringComparison.Ordinal);
        Assert.DoesNotContain("IMPORT HOME", window, StringComparison.Ordinal);
        Assert.DoesNotContain("REFRESH SOURCES", window, StringComparison.Ordinal);
        Assert.DoesNotContain("SESSION EDIT", window, StringComparison.Ordinal);
        Assert.DoesNotContain("IMPORT PASTE", window, StringComparison.Ordinal);
        Assert.DoesNotContain("Import Home Kubeconfig", viewModel, StringComparison.Ordinal);
    }

    [Fact]
    public void Main_window_exposes_radar_idle_seed_and_rule_state()
    {
        var root = FindRepositoryRoot();
        var window = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "MainWindow.axaml"));
        var viewModel = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "MainWindowViewModel.cs"));

        Assert.Contains("RadarIdleRuleLabel", window, StringComparison.Ordinal);
        Assert.Contains("RadarIdleSeed", window, StringComparison.Ordinal);
        Assert.Contains("Classes.idle=\"{Binding IsRadarIdle}\"", window, StringComparison.Ordinal);
        Assert.Contains("Border.minimapGlass.idle", File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "App.axaml")), StringComparison.Ordinal);
        Assert.Contains("RadarLifeRules", viewModel, StringComparison.Ordinal);
        Assert.Contains("footerTimer", viewModel, StringComparison.Ordinal);
        Assert.Contains("RefreshTimeLabels", viewModel, StringComparison.Ordinal);
        Assert.Contains("radarLifeSeenSignatures", viewModel, StringComparison.Ordinal);
        Assert.Contains("radarLifeStagnantGenerations", viewModel, StringComparison.Ordinal);
        Assert.Contains("RenderRadarLife(reset: !IsRadarIdle)", viewModel, StringComparison.Ordinal);
        Assert.Contains("ClusterCenters", viewModel, StringComparison.Ordinal);
        Assert.Contains("NamespaceAngle", viewModel, StringComparison.Ordinal);
        Assert.Contains("AddNamespaceArms", viewModel, StringComparison.Ordinal);
        Assert.Contains("RadarGridCell", viewModel, StringComparison.Ordinal);
        Assert.Contains("RadarCellFromPoint", viewModel, StringComparison.Ordinal);
        Assert.Contains("RectFromCell", viewModel, StringComparison.Ordinal);
        Assert.Contains("ProjectRadarRect", viewModel, StringComparison.Ordinal);
        Assert.Contains("RadarTerrainRing", viewModel, StringComparison.Ordinal);
        Assert.Contains("RadarTerrainStone", viewModel, StringComparison.Ordinal);
        Assert.Contains("RadarTerrainForest", viewModel, StringComparison.Ordinal);
        Assert.Contains("RadarTerrainGrass", viewModel, StringComparison.Ordinal);
        Assert.Contains("RadarTerrainDirt", viewModel, StringComparison.Ordinal);
        Assert.Contains("RadarTerrainSand", viewModel, StringComparison.Ordinal);
        Assert.Contains("RadarTerrainShallowWater", viewModel, StringComparison.Ordinal);
        Assert.Contains("RadarTerrainDeepWater", viewModel, StringComparison.Ordinal);
        Assert.Contains("RadarVirtualRow", viewModel, StringComparison.Ordinal);
        Assert.Contains("RadarDependencyRank", viewModel, StringComparison.Ordinal);
        Assert.Contains("IsVirtualRadarResource", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("AddRadarLink", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("OwnerPositionKey", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("SyncRadarLinks", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("TerrainPalette", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("RadarGridColumns", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("RadarGridRows", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("IsRadarCellInBounds", viewModel, StringComparison.Ordinal);
        Assert.Contains("StableHash", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("cellWidth - 1", viewModel, StringComparison.Ordinal);
        Assert.DoesNotContain("cellHeight - 1", viewModel, StringComparison.Ordinal);
    }

    private static IReadOnlyList<string> PrivateThemeNames()
    {
        return
        [
            "Cae" + "sar",
            "Du" + "ne",
            "War" + "craft",
            "Star" + "Craft",
            "Ter" + "ran",
            "Ze" + "rg",
            "Pro" + "toss",
            "Command & " + "Conquer",
            "Tibe" + "rian",
            "Red " + "Alert",
            "Total " + "Annihilation",
            "Mech" + "Commander",
            "Syndicate " + "Wars",
            "X-" + "COM",
            "UFO " + "Defense",
            "Fall" + "out",
            "Pip-" + "Boy",
            "Sim" + "City",
            "Transport " + "Tycoon",
            "Master of " + "Orion",
            "Desert " + "Command Console",
            "Fantasy " + "War Room",
            "Alien " + "Sector Command",
            "Imperial " + "City Builder"
        ];
    }

    private static string FindRepositoryRoot()
    {
        var current = AppContext.BaseDirectory;
        while (!string.IsNullOrWhiteSpace(current))
        {
            if (File.Exists(Path.Combine(current, "Podlord.slnx")))
            {
                return current;
            }

            current = Directory.GetParent(current)?.FullName;
        }

        throw new InvalidOperationException("Could not locate Podlord repository root.");
    }
}
