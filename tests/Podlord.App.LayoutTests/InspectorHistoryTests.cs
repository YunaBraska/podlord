using Podlord.Core;
using Podlord.Kubernetes;
using System.Reflection;

namespace Podlord.App.LayoutTests;

public sealed class InspectorHistoryTests : IDisposable
{
    private readonly string tempDir;

    public InspectorHistoryTests()
    {
        HeadlessAppBuilder.EnsureStarted();
        tempDir = Path.Combine(Path.GetTempPath(), "podlord-tests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tempDir);
    }

    public void Dispose()
    {
        try
        {
            Directory.Delete(tempDir, recursive: true);
        }
        catch
        {
        }
    }

    [Fact]
    public void Empty_history_disables_back_and_forward()
    {
        using var viewModel = NewViewModel();
        Assert.False(viewModel.CanGoBackInspector);
        Assert.False(viewModel.CanGoForwardInspector);
    }

    [Fact]
    public void Pushing_one_resource_keeps_back_disabled()
    {
        using var viewModel = NewViewModel();
        InjectRow(viewModel, NewRow("pod-a", "Pod"));
        PushHistory(viewModel, "pod-a");
        Assert.False(viewModel.CanGoBackInspector);
        Assert.False(viewModel.CanGoForwardInspector);
    }

    [Fact]
    public void After_two_pushes_back_is_enabled_forward_disabled()
    {
        using var viewModel = NewViewModel();
        InjectRow(viewModel, NewRow("pod-a", "Pod"));
        InjectRow(viewModel, NewRow("pod-b", "Pod"));
        PushHistory(viewModel, "pod-a");
        PushHistory(viewModel, "pod-b");
        Assert.True(viewModel.CanGoBackInspector);
        Assert.False(viewModel.CanGoForwardInspector);
    }

    [Fact]
    public void Stepping_back_enables_forward()
    {
        using var viewModel = NewViewModel();
        InjectRow(viewModel, NewRow("pod-a", "Pod"));
        InjectRow(viewModel, NewRow("pod-b", "Pod"));
        PushHistory(viewModel, "pod-a");
        PushHistory(viewModel, "pod-b");
        SetCursor(viewModel, 0);
        Assert.True(viewModel.CanGoForwardInspector);
        Assert.False(viewModel.CanGoBackInspector);
    }

    [Fact]
    public void Skips_missing_resources_when_walking_back()
    {
        using var viewModel = NewViewModel();
        InjectRow(viewModel, NewRow("pod-a", "Pod"));
        InjectRow(viewModel, NewRow("pod-c", "Pod"));
        PushHistory(viewModel, "pod-a");
        PushHistory(viewModel, "pod-b");
        PushHistory(viewModel, "pod-c");
        Assert.True(viewModel.CanGoBackInspector);
    }

    [Fact]
    public void Ring_buffer_caps_at_thirty_two()
    {
        using var viewModel = NewViewModel();
        for (var i = 0; i < 50; i++)
        {
            var id = $"pod-{i}";
            InjectRow(viewModel, NewRow(id, "Pod"));
            PushHistory(viewModel, id);
        }
        var ids = HistoryIds(viewModel);
        Assert.Equal(32, ids.Count);
        Assert.Equal("pod-18", ids[0]);
        Assert.Equal("pod-49", ids[^1]);
    }

    [Fact]
    public void Insert_after_cursor_preserves_forward_branch()
    {
        using var viewModel = NewViewModel();
        InjectRow(viewModel, NewRow("pod-1", "Pod"));
        InjectRow(viewModel, NewRow("pod-2", "Pod"));
        InjectRow(viewModel, NewRow("pod-3", "Pod"));
        InjectRow(viewModel, NewRow("pod-4", "Pod"));
        PushHistory(viewModel, "pod-1");
        PushHistory(viewModel, "pod-2");
        PushHistory(viewModel, "pod-3");
        SetCursor(viewModel, 1);
        PushHistory(viewModel, "pod-4");

        Assert.Equal(new[] { "pod-1", "pod-2", "pod-4", "pod-3" }, HistoryIds(viewModel));
    }

    [Fact]
    public void Duplicate_consecutive_push_does_not_grow_history()
    {
        using var viewModel = NewViewModel();
        InjectRow(viewModel, NewRow("pod-a", "Pod"));
        PushHistory(viewModel, "pod-a");
        PushHistory(viewModel, "pod-a");
        PushHistory(viewModel, "pod-a");
        Assert.Single(HistoryIds(viewModel));
    }

    private MainWindowViewModel NewViewModel()
    {
        var state = AppState.InMemoryWithConfigDirectory(tempDir);
        return new MainWindowViewModel(state, new KubernetesResourceService(state));
    }

    private static FlatResourceRow NewRow(string id, string kind) =>
        new(
            Id: id,
            Status: "Running",
            Kind: kind,
            Name: id,
            Namespace: "default",
            Cluster: "test",
            Age: "5m",
            Ready: "1/1",
            Restarts: 0,
            Node: "node-1",
            ImageSummary: "image:1",
            Owner: null,
            LastChange: "5m",
            Freshness: FreshnessState.Fresh);

    private static void InjectRow(MainWindowViewModel viewModel, FlatResourceRow row)
    {
        var field = typeof(MainWindowViewModel).GetField("cachedRows", BindingFlags.NonPublic | BindingFlags.Instance)
                    ?? throw new InvalidOperationException("cachedRows field missing");
        var list = (List<FlatResourceRow>)(field.GetValue(viewModel)
                    ?? throw new InvalidOperationException("cachedRows null"));
        list.Add(row);
    }

    private static void PushHistory(MainWindowViewModel viewModel, string id)
    {
        var method = typeof(MainWindowViewModel).GetMethod("PushInspectorHistory", BindingFlags.NonPublic | BindingFlags.Instance)
                     ?? throw new InvalidOperationException("PushInspectorHistory missing");
        method.Invoke(viewModel, [id]);
    }

    private static IReadOnlyList<string> HistoryIds(MainWindowViewModel viewModel)
    {
        var field = typeof(MainWindowViewModel).GetField("inspectorHistoryIds", BindingFlags.NonPublic | BindingFlags.Instance)
                    ?? throw new InvalidOperationException("inspectorHistoryIds missing");
        return (List<string>)(field.GetValue(viewModel)
                    ?? throw new InvalidOperationException("inspectorHistoryIds null"));
    }

    private static void SetCursor(MainWindowViewModel viewModel, int cursor)
    {
        var field = typeof(MainWindowViewModel).GetField("inspectorHistoryCursor", BindingFlags.NonPublic | BindingFlags.Instance)
                    ?? throw new InvalidOperationException("inspectorHistoryCursor missing");
        field.SetValue(viewModel, cursor);
    }
}
