using Avalonia.Controls;
using Avalonia.Controls.Primitives;
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
