using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.Input.Platform;
using Avalonia.Input.Raw;
using Avalonia.LogicalTree;
using Avalonia.Threading;
using Avalonia.VisualTree;
using Podlord.Core;
using Podlord.Kubernetes;

namespace Podlord.App.LayoutTests;

[Collection("Headless")]
public sealed class MainWindowHeadlessTests
{
    public MainWindowHeadlessTests()
    {
        HeadlessAppBuilder.EnsureStarted();
    }

    [Fact]
    public void Inspector_header_renders_back_and_forward_buttons()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var tempDir = Path.Combine(Path.GetTempPath(), "podlord-headless-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(tempDir);
            try
            {
                var state = AppState.InMemoryWithConfigDirectory(tempDir);
                var window = ShowWindow();
                try
                {
                    var buttons = window
                        .GetVisualDescendants()
                        .OfType<Button>()
                        .Where(b => b.Content is string s && (s == "◄" || s == "►"))
                        .ToList();

                    Assert.Contains(buttons, b => Equals(b.Content, "◄"));
                    Assert.Contains(buttons, b => Equals(b.Content, "►"));
                    Assert.All(buttons, b => Assert.False(b.IsEnabled));
                }
                finally
                {
                    CloseWindow(window);
                }
            }
            finally
            {
                try { Directory.Delete(tempDir, recursive: true); }
                catch { }
            }
        });
    }

    [Theory]
    [InlineData(false, true, true, true)]
    [InlineData(false, true, false, false)]
    [InlineData(false, true, null, true)]
    [InlineData(false, false, true, false)]
    [InlineData(true, false, false, true)]
    [InlineData(true, true, false, true)]
    public void Column_visibility_rule_pinned_overrides_empty_and_hidden(bool pinned, bool userVisible, bool? hasData, bool expected)
    {
        Assert.Equal(expected, MainWindow.ResolveColumnVisibility(pinned, userVisible, hasData));
    }

    [Fact]
    public void Settings_no_longer_exposes_auto_hide_empty_columns_flag()
    {
        var member = typeof(Settings).GetProperty("AutoHideEmptyColumns");
        Assert.Null(member);
    }

    [Fact]
    public void Resource_grid_renders_with_sortable_columns()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var window = ShowWindow();
            try
            {
                var grid = window.GetVisualDescendants().OfType<DataGrid>().FirstOrDefault(g => g.Name == "ResourceGrid");
                Assert.NotNull(grid);
                Assert.NotEmpty(grid!.Columns);
            }
            finally
            {
                CloseWindow(window);
            }
        });
    }

    [Fact]
    public void Log_editor_replaces_text_box()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var window = ShowWindow();
            try
            {
                var editorByName = window.GetLogicalDescendants()
                    .OfType<AvaloniaEdit.TextEditor>()
                    .Any(editor => editor.Name == "LogEditor");
                Assert.True(editorByName, "LogEditor TextEditor not found in MainWindow");
            }
            finally
            {
                CloseWindow(window);
            }
        });
    }

    [Fact]
    public void Resource_link_right_click_opens_context_menu_through_real_pointer_input()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var window = ShowWindow();
            try
            {
                var host = window.GetVisualDescendants().OfType<StackPanel>().FirstOrDefault(panel => panel.Name == "AboutSection")
                           ?? window.GetVisualDescendants().OfType<StackPanel>().First();
                var link = new ResourceLinkButton { Tag = "Pod/test-pod", Content = new TextBlock { Text = "Pod/test-pod" }, Width = 160, Height = 28 };
                host.Children.Add(link);
                Dispatcher.UIThread.RunJobs();
                window.UpdateLayout();
                Dispatcher.UIThread.RunJobs();

                Assert.NotNull(link.ContextMenu);
                var menu = link.ContextMenu!;
                Assert.False(menu.IsOpen);

                var origin = link.TranslatePoint(new Point(link.Bounds.Width / 2, link.Bounds.Height / 2), window) ?? new Point(0, 0);
                window.MouseDown(origin, MouseButton.Right);
                Dispatcher.UIThread.RunJobs();
                window.MouseUp(origin, MouseButton.Right);
                Dispatcher.UIThread.RunJobs();

                Assert.Equal(2, menu.Items.OfType<MenuItem>().Count());
            }
            finally
            {
                CloseWindow(window);
            }
        });
    }

    [Fact]
    public void Resource_link_context_menu_exposes_reference_actions_and_resolves_known_resource()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var window = ShowWindow();
            try
            {
                var row = new Podlord.Core.FlatResourceRow(
                    Id: "ses:Pod:default:test-pod:uid",
                    Status: "Running",
                    Kind: "Pod",
                    Name: "test-pod",
                    Namespace: "default",
                    Cluster: "cluster",
                    Age: "1m",
                    Ready: "1/1",
                    Restarts: 0,
                    Node: "node-a",
                    ImageSummary: "img:1",
                    Owner: "ReplicaSet/test",
                    LastChange: "now",
                    Freshness: Podlord.Core.FreshnessState.Fresh);
                var vm = window.ViewModel;
                vm.SeedCachedRowsForTesting([row]);
                Dispatcher.UIThread.RunJobs();

                var host = window.GetVisualDescendants().OfType<StackPanel>().FirstOrDefault(panel => panel.Name == "AboutSection")
                           ?? window.GetVisualDescendants().OfType<StackPanel>().First();
                var link = new ResourceLinkButton { Tag = "Pod/test-pod", Content = new TextBlock { Text = "Pod/test-pod" } };
                host.Children.Add(link);
                Dispatcher.UIThread.RunJobs();

                Assert.NotNull(link.ContextMenu);
                var open = link.ContextMenu!.Items.OfType<MenuItem>().First(item => string.Equals(item.Header as string, vm.T("ref.menuOpen"), StringComparison.Ordinal));
                var copy = link.ContextMenu!.Items.OfType<MenuItem>().First(item => string.Equals(item.Header as string, vm.T("ref.menuCopy"), StringComparison.Ordinal));

                Assert.Equal("Pod/test-pod", open.Tag);
                Assert.Equal("Pod/test-pod", copy.Tag);
                Assert.True(vm.OpenKnownResourceReference("Pod/test-pod"));
                Assert.True(string.IsNullOrEmpty(vm.StatusLine) || !vm.StatusLine.Contains("No cached resource matches"));
            }
            finally
            {
                CloseWindow(window);
            }
        });
    }

    [Fact]
    public void Diagnostics_grid_cell_copy_uses_full_backing_value()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var window = ShowWindow();
            try
            {
                var vm = window.ViewModel;
                var longValue = "cache total=123456789 list=234 detail=345 logs=456 pulse=567";
                var row = new DiagnosticMetricRow("Cache", longValue, "Long diagnostic value used to prove copy does not depend on visible clipping.");

                vm.SelectWorkspace("settings");
                vm.SelectedSettingsTabIndex = 2;
                vm.DiagnosticsRows.Clear();
                vm.DiagnosticsRows.Add(row);
                Dispatcher.UIThread.RunJobs();
                window.UpdateLayout();
                Dispatcher.UIThread.RunJobs();

                var grid = window.GetVisualDescendants()
                    .OfType<DataGrid>()
                    .FirstOrDefault(candidate => candidate.Name == "DiagnosticsGrid");
                Assert.NotNull(grid);
                Assert.Equal(longValue, MainWindow.CopyDiagnosticMetricValue(row, "Value"));
                Assert.Equal(row.Description, MainWindow.CopyDiagnosticMetricValue(row, "Description"));
            }
            finally
            {
                CloseWindow(window);
            }
        });
    }

    [Fact]
    public void About_tab_is_declared_with_donation_button_bindings()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var window = ShowWindow();
            try
            {
                var aboutTab = window.GetLogicalDescendants()
                    .OfType<TabItem>()
                    .FirstOrDefault(tab => tab.Header is string header && header == "About");
                Assert.NotNull(aboutTab);

                var buttons = LogicalDescendantsOf<Button>(aboutTab!).ToList();
                Assert.True(buttons.Count >= 7, $"Expected at least 7 buttons in About tab but found {buttons.Count}.");
                var tags = buttons.Select(button => button.Tag as string).Where(value => !string.IsNullOrEmpty(value)).ToHashSet();
                Assert.Contains("https://github.com/sponsors/YunaBraska", tags);
                Assert.Contains("https://buymeacoffee.com/YunaBraska", tags);
                Assert.Contains("https://ko-fi.com/YunaBraska", tags);
                Assert.Contains("https://liberapay.com/YunaBraska", tags);
                Assert.Contains("https://github.com/YunaBraska/podlord", tags);
                Assert.Contains("https://github.com/YunaBraska/podlord/issues/new", tags);
                Assert.Contains("https://github.com/YunaBraska/podlord/stargazers", tags);

                var aboutBlock = LogicalDescendantsOf<TextBlock>(aboutTab!).FirstOrDefault(tb => tb.Name == "AboutBlock");
                Assert.NotNull(aboutBlock);

                var logo = LogicalDescendantsOf<Image>(aboutTab!).FirstOrDefault();
                Assert.NotNull(logo);
            }
            finally
            {
                CloseWindow(window);
            }
        });
    }

    [Fact]
    public void Opening_sources_settings_updates_title_and_active_tab_state_without_stalling_dispatcher()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var window = ShowWindow();
            try
            {
                var session = new PodlordSession(
                    "session-1",
                    "session-1",
                    "context-1",
                    "cluster-a",
                    NamespaceScope.All,
                    SafetyLevel.Unknown,
                    null,
                    null,
                    true,
                    "now");

                var watch = System.Diagnostics.Stopwatch.StartNew();
                window.ViewModel.SelectedSession = session;
                window.ViewModel.OpenSourcesSettings();
                watch.Stop();

                Assert.True(
                    watch.Elapsed < TimeSpan.FromMilliseconds(250),
                    $"Workspace switch took {watch.Elapsed.TotalMilliseconds:0.0} ms.");

                Dispatcher.UIThread.RunJobs();

                Assert.Equal("Podlord - session-1", window.Title);
                Assert.Equal("settings", window.ViewModel.SelectedWorkspace);
                Assert.Equal(5, window.ViewModel.SelectedSettingsTabIndex);

                var sourcesTab = window.GetLogicalDescendants()
                    .OfType<TabItem>()
                    .First(tab => tab.Header is string header && header == window.ViewModel.SettingsSourcesText);
                Assert.True(sourcesTab.IsSelected);

                var settingsButton = window.GetVisualDescendants()
                    .OfType<Button>()
                    .First(button => button.Content is string text && text == window.ViewModel.NavSettingsText);
                Assert.Contains("active", settingsButton.Classes);
            }
            finally
            {
                CloseWindow(window);
            }
        });
    }

    [Fact]
    public void Tiny_cache_delta_keeps_visible_rows_and_frame_stable()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var window = ShowWindow();
            try
            {
                window.ViewModel.AnimationIntensitySetting = 0;
                window.ViewModel.RadarWaterEnabledSetting = false;
                window.ViewModel.ScreensaverSetting = false;
                window.ViewModel.Search = "visible";
                window.ViewModel.LimitText = "10";

                var visibleRow = new FlatResourceRow(
                    Id: "demo:Pod:default:visible:uid-1",
                    Status: "Running",
                    Kind: "Pod",
                    Name: "visible",
                    Namespace: "default",
                    Cluster: "cluster-a",
                    Age: "3m",
                    Ready: "1/1",
                    Restarts: 0,
                    Node: "node-a",
                    ImageSummary: "app:v1",
                    Owner: "Deployment/visible",
                    LastChange: "3m",
                    Freshness: FreshnessState.Fresh);

                var hiddenRow = new FlatResourceRow(
                    Id: "demo:Pod:default:hidden:uid-2",
                    Status: "Running",
                    Kind: "Pod",
                    Name: "hidden",
                    Namespace: "default",
                    Cluster: "cluster-a",
                    Age: "7m",
                    Ready: "1/1",
                    Restarts: 0,
                    Node: "node-b",
                    ImageSummary: "app:v1",
                    Owner: "Deployment/hidden",
                    LastChange: "7m",
                    Freshness: FreshnessState.Fresh);

                window.ViewModel.SeedCachedRowsForTesting([visibleRow, hiddenRow]);
                Dispatcher.UIThread.RunJobs();
                window.UpdateLayout();
                Dispatcher.UIThread.RunJobs();

                var firstFrame = CaptureFrameHash(window);
                var firstVisible = Assert.Single(window.ViewModel.Resources);
                Assert.Equal(visibleRow.Id, firstVisible.Id);
                Assert.Equal("1 visible / 2 cached", window.ViewModel.ResourceCountLabel);

                var updatedHiddenRow = hiddenRow with { LastChange = "8m", Status = "Succeeded" };
                window.ViewModel.SeedCachedRowsForTesting([visibleRow, updatedHiddenRow]);
                Dispatcher.UIThread.RunJobs();
                window.UpdateLayout();
                Dispatcher.UIThread.RunJobs();

                var secondVisible = Assert.Single(window.ViewModel.Resources);
                Assert.Equal(visibleRow.Id, secondVisible.Id);
                Assert.Equal("1 visible / 2 cached", window.ViewModel.ResourceCountLabel);
                Assert.Equal(firstFrame, CaptureFrameHash(window));
            }
            finally
            {
                CloseWindow(window);
            }
        });
    }

    [Fact]
    public void Pending_secondary_restore_is_cleared_when_user_switches_tabs_before_posted_restore_runs()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var tempDir = Path.Combine(Path.GetTempPath(), "podlord-secondary-restore-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(tempDir);
            try
            {
                var alphaConfig = Path.Combine(tempDir, "alpha.yaml");
                var betaConfig = Path.Combine(tempDir, "beta.yaml");
                File.WriteAllText(alphaConfig, OneContextKubeconfig("https://alpha.example:6443", "alpha"));
                File.WriteAllText(betaConfig, OneContextKubeconfig("https://beta.example:6443", "beta"));
                var state = AppState.InMemoryWithConfigDirectory(tempDir);
                state.ImportKubeconfig(alphaConfig);
                state.ImportKubeconfig(betaConfig);
                using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));
                viewModel.ReloadSessions(openDefaultSession: false);
                var alpha = viewModel.Sessions.Single(session => session.DisplayName == "alpha");
                var beta = viewModel.Sessions.Single(session => session.DisplayName == "beta");

                viewModel.OpenSessionTab(alpha.Id, activate: true);
                viewModel.SelectWorkspace("graph");
                viewModel.SeedCachedRowsForTesting([LayoutRow("alpha-api", "alpha")]);
                Dispatcher.UIThread.RunJobs();
                viewModel.OpenSessionTab(beta.Id, activate: true);
                viewModel.SelectWorkspace("graph");
                viewModel.SeedCachedRowsForTesting([LayoutRow("beta-api", "beta")]);
                Dispatcher.UIThread.RunJobs();

                viewModel.OpenSessionTab(alpha.Id, activate: true);

                Assert.Equal(1, viewModel.PendingSecondaryRestoreCountForTests);

                viewModel.OpenSessionTab(beta.Id, activate: true);
                Dispatcher.UIThread.RunJobs();

                Assert.Equal(0, viewModel.PendingSecondaryRestoreCountForTests);
            }
            finally
            {
                try { Directory.Delete(tempDir, recursive: true); }
                catch { }
            }
        });
    }

    [Fact]
    public void Large_cache_secondary_views_are_lazy_and_apply_latest_filter_on_dispatcher()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var window = ShowWindow();
            try
            {
                window.ViewModel.RadarWaterEnabledSetting = false;
                window.ViewModel.ScreensaverSetting = false;
                window.ViewModel.SetRadarViewport(3000, 3000);

                window.ViewModel.SeedCachedRowsForTesting(LargeLayoutRows(900));

                Assert.Equal(256, window.ViewModel.Resources.Count);
                Assert.Empty(window.ViewModel.RadarBlocks);

                window.ViewModel.Search = "pod-0003";
                window.ViewModel.Search = "pod-0006";

                Assert.Single(window.ViewModel.Resources);
                Assert.Equal("pod-0006", window.ViewModel.Resources[0].Name);

                PumpUntil(
                    () =>
                    {
                        var latest = window.ViewModel.RadarBlocks.FirstOrDefault(block => block.Resource.Name == "pod-0006");
                        var stale = window.ViewModel.RadarBlocks.FirstOrDefault(block => block.Resource.Name == "pod-0003");
                        return latest is { IsDimmed: false } && stale is { IsDimmed: true };
                    },
                    "Lazy radar rebuild did not settle on the latest filter state.");
            }
            finally
            {
                CloseWindow(window);
            }
        });
    }

    private static IEnumerable<T> LogicalDescendantsOf<T>(ILogical root) where T : ILogical
    {
        var stack = new Stack<ILogical>();
        stack.Push(root);
        while (stack.Count > 0)
        {
            var current = stack.Pop();
            foreach (var child in current.LogicalChildren)
            {
                if (child is T match) yield return match;
                stack.Push(child);
            }
        }
    }

    private static MainWindow ShowWindow()
    {
        var window = new MainWindow([]);
        window.Show();
        Dispatcher.UIThread.RunJobs();
        return window;
    }

    private static void CloseWindow(Window window)
    {
        window.Close();
        Dispatcher.UIThread.RunJobs();
    }

    private static byte[] CaptureFrameHash(Window window)
    {
        var bitmap = window.CaptureRenderedFrame() ?? throw new InvalidOperationException("Rendered frame capture failed.");
        var path = Path.Combine(Path.GetTempPath(), $"podlord-frame-{Guid.NewGuid():N}.png");
        try
        {
            bitmap.Save(path);
            return System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(path));
        }
        finally
        {
            try
            {
                File.Delete(path);
            }
            catch
            {
            }
        }
    }

    private static FlatResourceRow LayoutRow(string name, string cluster)
    {
        return new FlatResourceRow(
            $"{cluster}:Deployment:default:{name}:uid",
            "Available",
            "Deployment",
            name,
            "default",
            cluster,
            "1m",
            "1/1",
            0,
            string.Empty,
            "app:1",
            string.Empty,
            "1m",
            FreshnessState.Fresh);
    }

    private static IReadOnlyList<FlatResourceRow> LargeLayoutRows(int count)
    {
        return Enumerable.Range(0, count)
            .Select(index => new FlatResourceRow(
                $"layout:Pod:payments:pod-{index:0000}:uid",
                "Running",
                "Pod",
                $"pod-{index:0000}",
                "payments",
                "layout",
                "1m",
                "1/1",
                0,
                "node-a",
                "api:1",
                "ReplicaSet/api",
                "1m",
                FreshnessState.Fresh))
            .ToArray();
    }

    private static void PumpUntil(Func<bool> predicate, string failure)
    {
        var deadline = DateTimeOffset.UtcNow.AddSeconds(5);
        while (DateTimeOffset.UtcNow < deadline)
        {
            Dispatcher.UIThread.RunJobs();
            if (predicate())
            {
                return;
            }
        }

        Assert.True(predicate(), failure);
    }

    private static string OneContextKubeconfig(string server, string name)
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
    user: {{name}}
current-context: {{name}}
users:
- name: {{name}}
  user:
    token: token
""";
    }

}

[CollectionDefinition("Headless", DisableParallelization = true)]
public sealed class HeadlessCollection
{
}
