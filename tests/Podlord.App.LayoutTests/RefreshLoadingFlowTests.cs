using Podlord.Core;
using Podlord.Kubernetes;

namespace Podlord.App.LayoutTests;

public sealed class RefreshLoadingFlowTests : IDisposable
{
    private readonly string tempDir;

    public RefreshLoadingFlowTests()
    {
        HeadlessAppBuilder.EnsureStarted();
        tempDir = Path.Combine(Path.GetTempPath(), "podlord-refreshflow-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tempDir);
    }

    public void Dispose()
    {
        try { Directory.Delete(tempDir, recursive: true); } catch { }
    }

    [Fact]
    public void Health_segments_show_loading_progress_while_refresh_in_flight()
    {
        var state = AppState.InMemoryWithConfigDirectory(tempDir);
        var service = new KubernetesResourceService(state);
        using var viewModel = new MainWindowViewModel(state, service);
        var startedAt = DateTimeOffset.UtcNow.Subtract(TimeSpan.FromSeconds(10));

        SetPrivate(viewModel, "selectedSession", new PodlordSession(
            "session",
            "session",
            "context",
            "cluster",
            NamespaceScope.All,
            SafetyLevel.Unknown,
            null,
            null,
            true,
            "now"));
        SetPrivate(viewModel, "isRefreshing", true);
        viewModel.SetInitialLoadProgressForTests(startedAt, 10);
        RecordCompletedRequests(service, startedAt, 4);

        viewModel.SimulateTimerTickForTests();

        Assert.True(viewModel.IsInitialLoading);
        Assert.Equal(30, viewModel.HealthSegments.Count);
        Assert.Contains(viewModel.HealthSegments, segment => segment.State == "LOADING");
        Assert.Contains(viewModel.HealthSegments, segment => segment.State == "PENDING");
        Assert.Contains("40%", viewModel.HealthSummary, StringComparison.Ordinal);
    }

    private static void RecordCompletedRequests(KubernetesResourceService service, DateTimeOffset startedAt, int count)
    {
        var field = typeof(KubernetesResourceService).GetField("requestStarts", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance)
                    ?? throw new InvalidOperationException("requestStarts field missing");
        var queue = (Queue<DateTimeOffset>)(field.GetValue(service)
                    ?? throw new InvalidOperationException("requestStarts field was null"));
        for (var index = 0; index < count; index++)
        {
            queue.Enqueue(startedAt.AddSeconds(index + 1));
        }
    }

    private static void SetPrivate<T>(MainWindowViewModel viewModel, string fieldName, T value)
    {
        var field = typeof(MainWindowViewModel).GetField(fieldName, System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance)
                    ?? throw new InvalidOperationException($"{fieldName} field missing");
        field.SetValue(viewModel, value);
    }
}
