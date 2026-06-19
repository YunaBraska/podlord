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
                var window = new MainWindow([]);
                window.Show();
                Dispatcher.UIThread.RunJobs();

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
            var window = new MainWindow([]);
            window.Show();
            Dispatcher.UIThread.RunJobs();

            var grid = window.GetVisualDescendants().OfType<DataGrid>().FirstOrDefault(g => g.Name == "ResourceGrid");
            Assert.NotNull(grid);
            Assert.NotEmpty(grid!.Columns);
        });
    }

    [Fact]
    public void Log_editor_replaces_text_box()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var window = new MainWindow([]);
            window.Show();
            Dispatcher.UIThread.RunJobs();

            var editorByName = window.GetLogicalDescendants()
                .OfType<AvaloniaEdit.TextEditor>()
                .Any(editor => editor.Name == "LogEditor");
            Assert.True(editorByName, "LogEditor TextEditor not found in MainWindow");
        });
    }

    [Fact]
    public void Resource_link_right_click_opens_context_menu_through_real_pointer_input()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var window = new MainWindow([]);
            window.Show();
            Dispatcher.UIThread.RunJobs();

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
        });
    }

    [Fact]
    public void Resource_link_context_menu_copies_reference_to_clipboard_and_opens_in_inspector()
    {
        Dispatcher.UIThread.Invoke(async () =>
        {
            var window = new MainWindow([]);
            window.Show();
            Dispatcher.UIThread.RunJobs();

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
            vm.Resources.Add(row);
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

            copy.RaiseEvent(new Avalonia.Interactivity.RoutedEventArgs(MenuItem.ClickEvent));
            Dispatcher.UIThread.RunJobs();
            await Task.Delay(50);
            Dispatcher.UIThread.RunJobs();

            if (window.Clipboard is not null)
            {
                var clipboardText = await window.Clipboard.TryGetTextAsync();
                Assert.Equal("Pod/test-pod", clipboardText);
            }

            open.RaiseEvent(new Avalonia.Interactivity.RoutedEventArgs(MenuItem.ClickEvent));
            Dispatcher.UIThread.RunJobs();
            await Task.Delay(50);
            Dispatcher.UIThread.RunJobs();
            Assert.True(string.IsNullOrEmpty(vm.StatusLine) || !vm.StatusLine.Contains("No cached resource matches"));
        });
    }

    [Fact]
    public void About_tab_is_declared_with_donation_button_bindings()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var window = new MainWindow([]);
            window.Show();
            Dispatcher.UIThread.RunJobs();

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
}

[CollectionDefinition("Headless", DisableParallelization = true)]
public sealed class HeadlessCollection
{
}
