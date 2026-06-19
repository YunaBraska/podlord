using System.Net;
using System.Text;
using Podlord.Core;
using Podlord.Kubernetes;

namespace Podlord.App.LayoutTests;

public sealed class RefreshLoadingFlowTests : IDisposable
{
    private readonly string tempDir;

    public RefreshLoadingFlowTests()
    {
        tempDir = Path.Combine(Path.GetTempPath(), "podlord-refreshflow-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tempDir);
    }

    public void Dispose()
    {
        try { Directory.Delete(tempDir, recursive: true); } catch { }
    }

    [Fact]
    public async Task Health_segments_show_loading_progress_while_refresh_in_flight()
    {
        var directory = tempDir;
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig());
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);

        var requestsSeen = 0;
        var slow = new ManualResetEventSlim(false);
        var handler = new AppRecordingHandler(_ =>
        {
            Interlocked.Increment(ref requestsSeen);
            slow.Wait(TimeSpan.FromMilliseconds(50));
            return new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent("""{"items":[]}""", Encoding.UTF8, "application/json")
            };
        });
        using var viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state, handler));
        viewModel.ReloadSessions();

        var refresh = viewModel.RefreshResourcesAsync(force: true);

        var observedLoadingState = false;
        for (var i = 0; i < 40 && !refresh.IsCompleted; i++)
        {
            viewModel.SimulateTimerTickForTests();
            if (viewModel.IsInitialLoading && viewModel.HealthSegments.Count == 30
                && viewModel.HealthSegments.Any(s => s.State == "LOADING" || s.State == "PENDING"))
            {
                observedLoadingState = true;
            }
            slow.Set();
            await Task.Delay(20);
            slow.Reset();
        }

        slow.Set();
        await refresh;
        Assert.True(observedLoadingState, $"Expected loading segments mid-refresh. requestsSeen={requestsSeen}");
    }

    private static string OneContextKubeconfig()
    {
        return """
apiVersion: v1
kind: Config
current-context: c
contexts:
- name: c
  context:
    cluster: cl
    user: u
clusters:
- name: cl
  cluster:
    server: http://127.0.0.1:6443
users:
- name: u
  user: {}
""";
    }
}

internal sealed class AppRecordingHandler : HttpMessageHandler
{
    private readonly Func<HttpRequestMessage, HttpResponseMessage> respond;

    public AppRecordingHandler(Func<HttpRequestMessage, HttpResponseMessage> respond)
    {
        this.respond = respond;
    }

    protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
    {
        return Task.FromResult(respond(request));
    }
}
