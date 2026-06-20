using Podlord.Core;
using Podlord.Kubernetes;

namespace Podlord.App.LayoutTests;

public sealed class InitialLoadHealthSegmentsTests : IDisposable
{
    private readonly string tempDir;

    public InitialLoadHealthSegmentsTests()
    {
        HeadlessAppBuilder.EnsureStarted();
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

    [Fact]
    public void Loading_segments_do_not_play_audio_per_lit_segment()
    {
        var state = AppState.InMemoryWithConfigDirectory(tempDir);
        var service = new KubernetesResourceService(state);
        var player = new RecordingAlertSoundPlayer();
        using var viewModel = new MainWindowViewModel(state, service, player);
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
        viewModel.UpdateLoadingHealthSegments();
        Assert.Empty(player.PlayedPaths);

        RecordCompletedRequests(service, startedAt, 5);
        viewModel.UpdateLoadingHealthSegments();
        Assert.Equal(15, CountLit(viewModel));
        Assert.Empty(player.PlayedPaths);

        viewModel.UpdateLoadingHealthSegments();
        Assert.Empty(player.PlayedPaths);

        RecordCompletedRequests(service, startedAt, 5);
        viewModel.UpdateLoadingHealthSegments();
        Assert.Equal(30, CountLit(viewModel));
        Assert.Empty(player.PlayedPaths);
    }

    private static int CountLit(MainWindowViewModel viewModel)
    {
        return viewModel.HealthSegments.Count(s => s.State == "LOADING");
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

    private sealed class RecordingAlertSoundPlayer : IAlertSoundPlayer
    {
        public List<string> PlayedPaths { get; } = [];

        public bool Play(string path, out string error)
        {
            PlayedPaths.Add(path);
            error = string.Empty;
            return true;
        }

        public void Dispose()
        {
        }
    }
}
