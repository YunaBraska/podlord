using Podlord.Core;
using Podlord.Kubernetes;

namespace Podlord.App.LayoutTests;

public sealed class InitialLoadHealthSegmentsTests : IDisposable
{
    private readonly string tempDir;

    public InitialLoadHealthSegmentsTests()
    {
        tempDir = Path.Combine(Path.GetTempPath(), "podlord-loadhealth-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tempDir);
    }

    public void Dispose()
    {
        try { Directory.Delete(tempDir, recursive: true); } catch { }
    }

    [Fact]
    public void Segments_layout_30_ticks_with_correct_states()
    {
        var state = AppState.InMemoryWithConfigDirectory(tempDir);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        viewModel.SetInitialLoadProgressForTests(DateTimeOffset.UtcNow, 20);
        viewModel.UpdateLoadingHealthSegments();
        Assert.Equal(30, viewModel.HealthSegments.Count);
        Assert.All(viewModel.HealthSegments, segment => Assert.Contains(segment.State, new[] { "LOADING", "PENDING" }));
    }

    [Fact]
    public void Loading_status_summary_shows_percentage()
    {
        var state = AppState.InMemoryWithConfigDirectory(tempDir);
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));

        viewModel.SetInitialLoadProgressForTests(DateTimeOffset.UtcNow, 10);
        viewModel.UpdateLoadingHealthSegments();
        Assert.Contains("Loading resources", viewModel.HealthSummary, StringComparison.Ordinal);
        Assert.Contains("%", viewModel.HealthSummary, StringComparison.Ordinal);
    }

    private static int CountLit(MainWindowViewModel viewModel)
    {
        return viewModel.HealthSegments.Count(s => s.State == "LOADING");
    }
}
