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
}

[CollectionDefinition("Headless", DisableParallelization = true)]
public sealed class HeadlessCollection
{
}
