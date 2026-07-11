using System.Net;
using System.Text;
using Podlord.Core;
using Podlord.Kubernetes;

namespace Podlord.App.Tests;

public sealed class PerformanceBudgetTests
{
    private static readonly TimeSpan InteractionBudget = TimeSpan.FromMilliseconds(1_000);
    private static readonly TimeSpan SecondaryViewBudget = TimeSpan.FromMilliseconds(1_500);
    private static readonly TimeSpan InspectorInitialBudget = TimeSpan.FromMilliseconds(150);

    [Fact]
    public void Large_cache_filter_changes_are_cache_only_and_budgeted()
    {
        var (viewModel, handler) = CreateViewModel();
        using (viewModel)
        {
            viewModel.SeedCachedRowsForTesting(LargeRows(3_000));
            var requestCount = handler.Requests.Count;

            var kindFilter = Measure(() => viewModel.KindPicker.SetExpression("\"Pod\""));
            var namespaceFilter = Measure(() => viewModel.NamespacePicker.SetExpression("\"payments\""));
            var nameFilter = Measure(() => viewModel.NamePicker.SetExpression("pod-0299"));
            var clearFilter = Measure(() =>
            {
                viewModel.KindPicker.SetExpression(string.Empty);
                viewModel.NamespacePicker.SetExpression(string.Empty);
                viewModel.NamePicker.SetExpression(string.Empty);
            });

            AssertUnder("kind filter", kindFilter, InteractionBudget);
            AssertUnder("namespace filter", namespaceFilter, InteractionBudget);
            AssertUnder("name filter", nameFilter, InteractionBudget);
            AssertUnder("clear filters", clearFilter, SecondaryViewBudget);
            Assert.Equal(requestCount, handler.Requests.Count);
            Assert.True(viewModel.Resources.Count <= 256);
        }
    }

    [Fact]
    public void Large_cache_radar_viewport_and_zoom_updates_are_budgeted()
    {
        var (viewModel, handler) = CreateViewModel();
        using (viewModel)
        {
            viewModel.SeedCachedRowsForTesting(LargeRows(3_000));
            var requestCount = handler.Requests.Count;

            var resize = Measure(() => viewModel.SetRadarViewport(900, 420));
            var zoomIn = Measure(viewModel.ZoomRadarIn);
            var zoomOut = Measure(viewModel.ZoomRadarOut);

            AssertUnder("radar resize", resize, SecondaryViewBudget);
            AssertUnder("radar zoom in", zoomIn, InteractionBudget);
            AssertUnder("radar zoom out", zoomOut, InteractionBudget);
            Assert.Equal(requestCount, handler.Requests.Count);
            Assert.NotEmpty(viewModel.RadarBlocks);
        }
    }

    [Fact]
    public async Task Lazy_secondary_view_rebuild_eventually_applies_latest_filter_only()
    {
        var (viewModel, handler) = CreateViewModel();
        using (viewModel)
        {
            viewModel.SetRadarViewport(3_000, 3_000);
            viewModel.SeedCachedRowsForTesting(LargeRows(900));
            await WaitUntilAsync(
                () => viewModel.RadarBlocks.Any(block => block.Resource.Name == "pod-0003")
                      && viewModel.RadarBlocks.Any(block => block.Resource.Name == "pod-0006"),
                TimeSpan.FromSeconds(3),
                "Initial lazy radar build did not render expected pod blocks.");
            var requestCount = handler.Requests.Count;

            viewModel.Search = "pod-0003";
            viewModel.Search = "pod-0006";

            Assert.Single(viewModel.Resources);
            Assert.Equal("pod-0006", viewModel.Resources[0].Name);
            await WaitUntilAsync(
                () =>
                {
                    var latest = viewModel.RadarBlocks.FirstOrDefault(block => block.Resource.Name == "pod-0006");
                    var stale = viewModel.RadarBlocks.FirstOrDefault(block => block.Resource.Name == "pod-0003");
                    return latest is { IsDimmed: false } && stale is { IsDimmed: true };
                },
                TimeSpan.FromSeconds(3),
                "Lazy radar rebuild did not apply the latest filter state.");
            Assert.Equal(requestCount, handler.Requests.Count);
        }
    }

    [Fact]
    public void Large_cache_graph_and_events_workspace_materialization_are_budgeted()
    {
        var (viewModel, handler) = CreateViewModel();
        using (viewModel)
        {
            viewModel.SeedCachedRowsForTesting(LargeRows(3_000));
            var requestCount = handler.Requests.Count;

            var graph = Measure(() => viewModel.SelectWorkspace("graph"));
            var events = Measure(() => viewModel.SelectWorkspace("events"));
            var resources = Measure(() => viewModel.SelectWorkspace("resources"));

            AssertUnder("graph workspace", graph, SecondaryViewBudget);
            AssertUnder("events workspace", events, SecondaryViewBudget);
            AssertUnder("resources workspace", resources, InteractionBudget);
            Assert.Equal(requestCount, handler.Requests.Count);
        }
    }

    [Fact]
    public void Cached_tab_switches_restore_session_state_under_budget()
    {
        var directory = TempDirectory();
        var devConfig = Path.Combine(directory, "dev.yaml");
        var prodConfig = Path.Combine(directory, "prod.yaml");
        File.WriteAllText(devConfig, OneContextKubeconfig("https://dev.example:6443", "dev"));
        File.WriteAllText(prodConfig, OneContextKubeconfig("https://prod.example:6443", "prod"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(devConfig);
        state.ImportKubeconfig(prodConfig);
        state.SaveSettings(state.Settings() with
        {
            RadarWaterEnabled = false,
            RadarWaterSpeed = 0,
            RequestHardLimitPerMinute = 0
        });
        var handler = new RecordingHandler(_ => JsonResponse("""{"items":[]}"""));
        using var viewModel = new MainWindowViewModel(
            state,
            new KubernetesResourceService(state, handler),
            new NoOpAlertSoundPlayer(),
            new NoOpReleaseUpdateChecker(),
            () => "test");
        viewModel.ReloadSessions(openDefaultSession: false);
        viewModel.SetRadarViewport(520, 240);
        viewModel.ProblemsOnly = false;
        var dev = viewModel.Sessions.Single(session => session.DisplayName == "dev");
        var prod = viewModel.Sessions.Single(session => session.DisplayName == "prod");

        viewModel.OpenSessionTab(dev.Id, activate: true);
        viewModel.SeedCachedRowsForTesting(LargeRows(2_500, "dev"));
        viewModel.OpenSessionTab(prod.Id, activate: true);
        viewModel.SeedCachedRowsForTesting(LargeRows(2_500, "prod"));
        var requestCount = handler.Requests.Count;

        var toDev = Measure(() => viewModel.ActivateSessionTab(dev.Id));
        var toProd = Measure(() => viewModel.ActivateSessionTab(prod.Id));

        AssertUnder("switch to dev", toDev, SecondaryViewBudget);
        AssertUnder("switch to prod", toProd, SecondaryViewBudget);
        Assert.Equal(requestCount, handler.Requests.Count);
        Assert.All(viewModel.Resources, row => Assert.Equal("prod", row.Cluster));
    }

    [Fact]
    public async Task Inspector_focus_renders_cached_summary_before_fresh_detail_returns()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "dev.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://dev.example:6443", "dev"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        state.SaveSettings(state.Settings() with
        {
            RadarWaterEnabled = false,
            RadarWaterSpeed = 0,
            RequestHardLimitPerMinute = 0
        });
        var detailRequested = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseDetail = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var handler = new AsyncRecordingHandler(async (request, cancellationToken) =>
        {
            var path = request.RequestUri?.AbsolutePath ?? string.Empty;
            if (path == "/api/v1/namespaces/payments/pods/pod-0003")
            {
                detailRequested.TrySetResult();
                await releaseDetail.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
                return JsonResponse(PodObjectJson("pod-0003"));
            }

            return JsonResponse("""{"items":[]}""");
        });
        using var viewModel = new MainWindowViewModel(
            state,
            new KubernetesResourceService(state, handler),
            new NoOpAlertSoundPlayer(),
            new NoOpReleaseUpdateChecker(),
            () => "test");
        viewModel.ReloadSessions(openDefaultSession: false);
        viewModel.OpenSessionTab(viewModel.Sessions.Single().Id, activate: true);
        var row = Row(3);
        viewModel.SeedCachedRowsForTesting([row]);
        viewModel.SelectedResourceRow = row;

        var initial = Measure(() => _ = viewModel.OpenSelectedResourceAsync());

        AssertUnder("inspector initial cached focus", initial, InspectorInitialBudget);
        await detailRequested.Task.WaitAsync(TimeSpan.FromSeconds(2));
        Assert.True(viewModel.IsInspectorVisible);
        Assert.True(viewModel.IsDetailLoading);
        Assert.Contains(viewModel.Summary, item => item.Label == "Kind" && item.Value == "Pod");

        releaseDetail.TrySetResult();
        await WaitUntilAsync(() => !viewModel.IsDetailLoading, TimeSpan.FromSeconds(3), "Inspector detail did not finish.");
        Assert.Contains("name: pod-0003", viewModel.EditableYaml, StringComparison.Ordinal);
    }

    private static (MainWindowViewModel ViewModel, RecordingHandler Handler) CreateViewModel()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "dev.yaml");
        File.WriteAllText(kubeconfig, OneContextKubeconfig("https://dev.example:6443", "dev"));
        var state = AppState.InMemoryWithConfigDirectory(directory);
        state.ImportKubeconfig(kubeconfig);
        state.SaveSettings(state.Settings() with
        {
            RadarWaterEnabled = false,
            RadarWaterSpeed = 0,
            RequestHardLimitPerMinute = 0
        });
        var handler = new RecordingHandler(_ => JsonResponse("""{"items":[]}"""));
        var viewModel = new MainWindowViewModel(
            state,
            new KubernetesResourceService(state, handler),
            new NoOpAlertSoundPlayer(),
            new NoOpReleaseUpdateChecker(),
            () => "test");
        viewModel.ReloadSessions(openDefaultSession: false);
        viewModel.SetRadarViewport(520, 240);
        viewModel.ProblemsOnly = false;
        viewModel.OpenSessionTab(viewModel.Sessions.Single().Id, activate: true);
        return (viewModel, handler);
    }

    private static IReadOnlyList<FlatResourceRow> LargeRows(int count, string cluster = "cluster-a")
    {
        return Enumerable.Range(0, count).Select(index => Row(index, cluster)).ToArray();
    }

    private static FlatResourceRow Row(int index, string cluster = "cluster-a")
    {
        var kind = index % 20 == 0 ? "Event" :
            index % 17 == 0 ? "Service" :
            index % 13 == 0 ? "Deployment" :
            index % 11 == 0 ? "ReplicaSet" :
            "Pod";
        var name = kind == "Pod" ? $"pod-{index:0000}" : $"{kind.ToLowerInvariant()}-{index:0000}";
        return new FlatResourceRow(
            $"id-{cluster}-{kind}-{index:0000}",
            index % 97 == 0 ? "CrashLoopBackOff" : index % 31 == 0 ? "Pending" : "Running",
            kind,
            name,
            index % 3 == 0 ? "payments" : index % 3 == 1 ? "platform" : "kube-system",
            cluster,
            $"{index % 90 + 1}m",
            index % 31 == 0 ? "0/1" : "1/1",
            index % 7,
            $"node-{index % 16:00}",
            $"service-{index % 80:00}:1",
            kind == "Pod" ? $"ReplicaSet/{name}" : string.Empty,
            $"{index % 90 + 1}m",
            FreshnessState.Fresh,
            kind == "Event" ? $"{name}.17f{index:000}" : string.Empty,
            kind == "Event" ? "Scheduled" : string.Empty,
            kind == "Event" ? $"Event message {index}" : string.Empty,
            kind == "Event" ? $"Pod/{name}" : string.Empty,
            Created: "2026-06-20T08:00:00Z")
        {
            Pulse = new ResourcePulse(
                index % 4 == 0 ? index % 900 + 10 : null,
                kind is "Pod" or "Node" ? 1_000 : null,
                index % 5 == 0 ? (index % 2_048 + 128) * 1_024L * 1_024L : null,
                kind is "Pod" or "Node" ? 2_048L * 1_024L * 1_024L : null,
                null,
                null,
                index % 29 == 0 ? (index % 500 + 1) * 1_024L * 1_024L : null,
                index % 29 == 0 ? 5L * 1_024L * 1_024L * 1_024L : null,
                "API",
                string.Empty)
        };
    }

    private static void AssertUnder(string label, TimeSpan actual, TimeSpan budget)
    {
        Assert.True(
            actual < budget,
            $"{label} took {actual.TotalMilliseconds:0.0} ms; budget is {budget.TotalMilliseconds:0.0} ms.");
    }

    private static TimeSpan Measure(Action action)
    {
        var watch = System.Diagnostics.Stopwatch.StartNew();
        action();
        watch.Stop();
        return watch.Elapsed;
    }

    private static async Task WaitUntilAsync(Func<bool> predicate, TimeSpan timeout, string failure)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (DateTimeOffset.UtcNow < deadline)
        {
            if (predicate())
            {
                return;
            }

            await Task.Delay(25).ConfigureAwait(false);
        }

        Assert.True(predicate(), failure);
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

    private static string PodObjectJson(string name)
    {
        return $$"""
{
  "metadata": {
    "name": "{{name}}",
    "namespace": "payments",
    "uid": "uid-{{name}}",
    "creationTimestamp": "2026-06-20T08:00:00Z"
  },
  "spec": {
    "nodeName": "node-a",
    "containers": [
      {
        "name": "api",
        "image": "repo/api:1"
      }
    ]
  },
  "status": {
    "phase": "Running",
    "containerStatuses": [
      {
        "name": "api",
        "ready": true,
        "restartCount": 0,
        "state": {
          "running": {}
        }
      }
    ]
  }
}
""";
    }

    private static string TempDirectory()
    {
        var path = Path.Combine(Path.GetTempPath(), $"podlord-perf-test-{Guid.NewGuid():N}");
        Directory.CreateDirectory(path);
        return path;
    }

    private static HttpResponseMessage JsonResponse(string json)
    {
        return new HttpResponseMessage(HttpStatusCode.OK)
        {
            Content = new StringContent(json, Encoding.UTF8, "application/json")
        };
    }

    private sealed class RecordingHandler(Func<HttpRequestMessage, HttpResponseMessage> respond) : HttpMessageHandler
    {
        public List<string> Requests { get; } = [];

        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
        {
            Requests.Add(request.RequestUri?.PathAndQuery ?? string.Empty);
            return Task.FromResult(respond(request));
        }
    }

    private sealed class AsyncRecordingHandler(Func<HttpRequestMessage, CancellationToken, Task<HttpResponseMessage>> respond) : HttpMessageHandler
    {
        protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
        {
            return await respond(request, cancellationToken).ConfigureAwait(false);
        }
    }
}
