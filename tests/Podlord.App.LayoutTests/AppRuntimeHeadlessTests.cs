using Avalonia.Controls;
using Podlord.Core;
using Podlord.Kubernetes;

namespace Podlord.App.LayoutTests;

[Collection("Headless")]
public sealed class AppRuntimeHeadlessTests
{
    public AppRuntimeHeadlessTests()
    {
        HeadlessAppBuilder.EnsureStarted();
    }

    [Fact]
    public void Open_or_activate_session_adds_session_to_source_host_once()
    {
        var state = AppState.InMemory();
        var service = new KubernetesResourceService(state);
        var detachedCreated = false;
        var runtime = new AppRuntime(
            state,
            service,
            _ => { },
            _ =>
            {
                detachedCreated = true;
                return new FakeSessionSurfaceHost("detached", isDetached: true);
            });
        var host = new FakeSessionSurfaceHost("host-a");
        runtime.RegisterHost(host);

        runtime.OpenOrActivateSession("session-1", host.HostId, SessionOpenTarget.Tab);
        runtime.OpenOrActivateSession("session-1", host.HostId, SessionOpenTarget.Tab);

        Assert.Equal(["session-1"], host.AddedSessions);
        Assert.Equal(["session-1", "session-1"], host.ActivatedSessions);
        Assert.True(host.ContainsSession("session-1"));
        Assert.False(detachedCreated);
    }

    [Fact]
    public void Open_or_activate_session_focuses_existing_host_instead_of_duplicating()
    {
        var state = AppState.InMemory();
        var service = new KubernetesResourceService(state);
        Window? focusedWindow = null;
        var runtime = new AppRuntime(state, service, window => focusedWindow = window);
        var hostA = new FakeSessionSurfaceHost("host-a");
        var hostB = new FakeSessionSurfaceHost("host-b");
        runtime.RegisterHost(hostA);
        runtime.RegisterHost(hostB);
        hostA.AddSession("session-1", activate: true);
        runtime.RegisterSessionPlacement("session-1", hostA.HostId);

        runtime.OpenOrActivateSession("session-1", hostB.HostId, SessionOpenTarget.Tab);

        Assert.Empty(hostB.AddedSessions);
        Assert.Equal(["session-1", "session-1"], hostA.ActivatedSessions);
        Assert.Same(hostA.Window, focusedWindow);
    }

    [Fact]
    public void Open_in_window_detaches_when_session_is_already_open_in_same_host()
    {
        var state = AppState.InMemory();
        var service = new KubernetesResourceService(state);
        Window? focusedWindow = null;
        var detachedHost = new FakeSessionSurfaceHost("detached", isDetached: true);
        var runtime = new AppRuntime(
            state,
            service,
            window => focusedWindow = window,
            _ => detachedHost);
        var sourceHost = new FakeSessionSurfaceHost("host-a");
        runtime.RegisterHost(sourceHost);
        runtime.RegisterHost(detachedHost);
        sourceHost.AddSession("session-1", activate: true);
        runtime.RegisterSessionPlacement("session-1", sourceHost.HostId);

        runtime.OpenOrActivateSession("session-1", sourceHost.HostId, SessionOpenTarget.Window);

        Assert.Equal(["session-1"], sourceHost.RemovedSessions);
        Assert.Equal(["session-1"], detachedHost.AddedSessions);
        Assert.Same(detachedHost.Window, focusedWindow);
    }

    [Fact]
    public void Open_in_tab_for_unopened_session_does_not_create_detached_window()
    {
        var state = AppState.InMemory();
        var service = new KubernetesResourceService(state);
        var detachedCreated = false;
        var runtime = new AppRuntime(
            state,
            service,
            _ => { },
            _ =>
            {
                detachedCreated = true;
                return new FakeSessionSurfaceHost("detached", isDetached: true);
            });
        var sourceHost = new FakeSessionSurfaceHost("host-a");
        runtime.RegisterHost(sourceHost);

        runtime.OpenOrActivateSession("session-1", sourceHost.HostId, SessionOpenTarget.Tab);

        Assert.False(detachedCreated);
        Assert.Equal(["session-1"], sourceHost.AddedSessions);
        Assert.Equal(["session-1"], sourceHost.ActivatedSessions);
    }

    [Fact]
    public void Detached_factory_with_preopened_session_is_not_added_twice()
    {
        var state = AppState.InMemory();
        var service = new KubernetesResourceService(state);
        var detachedHost = new FakeSessionSurfaceHost("detached", isDetached: true);
        detachedHost.AddSession("session-1", activate: true);
        var runtime = new AppRuntime(
            state,
            service,
            _ => { },
            _ => detachedHost);
        var sourceHost = new FakeSessionSurfaceHost("host-a");
        runtime.RegisterHost(sourceHost);

        runtime.OpenOrActivateSession("session-1", sourceHost.HostId, SessionOpenTarget.Window);

        Assert.Equal(["session-1"], detachedHost.AddedSessions);
        Assert.True(detachedHost.ContainsSession("session-1"));
    }

    [Fact]
    public void Register_session_placement_rejects_second_live_host()
    {
        var state = AppState.InMemory();
        var service = new KubernetesResourceService(state);
        var runtime = new AppRuntime(state, service, _ => { });
        var hostA = new FakeSessionSurfaceHost("host-a");
        var hostB = new FakeSessionSurfaceHost("host-b");
        runtime.RegisterHost(hostA);
        runtime.RegisterHost(hostB);

        var first = runtime.RegisterSessionPlacement("session-1", hostA.HostId);
        var second = runtime.RegisterSessionPlacement("session-1", hostB.HostId);

        Assert.True(first);
        Assert.False(second);
    }

    [Fact]
    public void Unregister_host_removes_its_session_placements_and_notifies_once()
    {
        var state = AppState.InMemory();
        var service = new KubernetesResourceService(state);
        var runtime = new AppRuntime(state, service, _ => { });
        var host = new FakeSessionSurfaceHost("host-a");
        var changeCount = 0;
        runtime.SessionPlacementsChanged += (_, _) => changeCount++;
        runtime.RegisterHost(host);
        host.AddSession("session-1", activate: true);
        runtime.RegisterSessionPlacement("session-1", host.HostId);
        Assert.True(runtime.IsSessionOpen("session-1"));

        runtime.UnregisterHost(host);

        Assert.False(runtime.IsSessionOpen("session-1"));
        Assert.Equal(2, changeCount);
    }

    [Fact]
    public void Unregister_host_without_sessions_does_not_raise_placement_change()
    {
        var state = AppState.InMemory();
        var service = new KubernetesResourceService(state);
        var runtime = new AppRuntime(state, service, _ => { });
        var host = new FakeSessionSurfaceHost("host-a");
        var changeCount = 0;
        runtime.SessionPlacementsChanged += (_, _) => changeCount++;
        runtime.RegisterHost(host);

        runtime.UnregisterHost(host);

        Assert.Equal(0, changeCount);
    }

    [Fact]
    public void Open_or_activate_session_ignores_missing_source_host()
    {
        var state = AppState.InMemory();
        var service = new KubernetesResourceService(state);
        Window? focusedWindow = null;
        var runtime = new AppRuntime(state, service, window => focusedWindow = window);
        var host = new FakeSessionSurfaceHost("host-a");
        runtime.RegisterHost(host);

        runtime.OpenOrActivateSession("session-1", "missing-host", SessionOpenTarget.Tab);

        Assert.False(runtime.IsSessionOpen("session-1"));
        Assert.Empty(host.AddedSessions);
        Assert.Null(focusedWindow);
    }

    [Fact]
    public void Activate_existing_session_returns_false_when_session_is_unknown()
    {
        var state = AppState.InMemory();
        var service = new KubernetesResourceService(state);
        var runtime = new AppRuntime(state, service, _ => { });

        var activated = runtime.ActivateExistingSession("missing-session");

        Assert.False(activated);
    }

    [Fact]
    public void Detach_session_ignores_missing_source_host()
    {
        var state = AppState.InMemory();
        var service = new KubernetesResourceService(state);
        var detachedCreated = false;
        var runtime = new AppRuntime(
            state,
            service,
            _ => { },
            _ =>
            {
                detachedCreated = true;
                return new FakeSessionSurfaceHost("detached", isDetached: true);
            });

        runtime.DetachSession("session-1", "missing-host");

        Assert.False(detachedCreated);
        Assert.False(runtime.IsSessionOpen("session-1"));
    }

    [Fact]
    public void Default_focus_restores_minimized_window_when_activating_existing_session()
    {
        var state = AppState.InMemory();
        var service = new KubernetesResourceService(state);
        var runtime = new AppRuntime(state, service);
        var host = new FakeSessionSurfaceHost("host-a");
        runtime.RegisterHost(host);
        host.AddSession("session-1", activate: true);
        runtime.RegisterSessionPlacement("session-1", host.HostId);
        host.Window.WindowState = WindowState.Minimized;

        var activated = runtime.ActivateExistingSession("session-1");

        Assert.True(activated);
        Assert.Equal(WindowState.Normal, host.Window.WindowState);
    }

    [Fact]
    public void Detach_session_focuses_existing_detached_window_and_removes_duplicate_source_tab()
    {
        var state = AppState.InMemory();
        var service = new KubernetesResourceService(state);
        Window? focusedWindow = null;
        var runtime = new AppRuntime(state, service, window => focusedWindow = window);
        var sourceHost = new FakeSessionSurfaceHost("host-a");
        var detachedHost = new FakeSessionSurfaceHost("host-b", isDetached: true);
        runtime.RegisterHost(sourceHost);
        runtime.RegisterHost(detachedHost);
        sourceHost.AddSession("session-1", activate: true);
        detachedHost.AddSession("session-1", activate: true);
        runtime.RegisterSessionPlacement("session-1", detachedHost.HostId);

        runtime.DetachSession("session-1", sourceHost.HostId);

        Assert.Equal(["session-1"], sourceHost.RemovedSessions);
        Assert.Equal(["session-1", "session-1"], detachedHost.ActivatedSessions);
        Assert.Same(detachedHost.Window, focusedWindow);
    }

    private sealed class FakeSessionSurfaceHost : ISessionSurfaceHost
    {
        private readonly HashSet<string> sessions = new(StringComparer.Ordinal);

        public FakeSessionSurfaceHost(string hostId, bool isDetached = false)
        {
            HostId = hostId;
            IsDetached = isDetached;
        }

        public string HostId { get; }

        public Window Window { get; } = new();

        public bool IsDetached { get; }

        public List<string> AddedSessions { get; } = [];

        public List<string> ActivatedSessions { get; } = [];

        public List<string> RemovedSessions { get; } = [];

        public bool IsEmpty => sessions.Count == 0;

        public bool ContainsSession(string sessionId)
        {
            return sessions.Contains(sessionId);
        }

        public void AddSession(string sessionId, bool activate)
        {
            sessions.Add(sessionId);
            AddedSessions.Add(sessionId);
            if (activate)
            {
                ActivateSession(sessionId);
            }
        }

        public void ActivateSession(string sessionId)
        {
            ActivatedSessions.Add(sessionId);
        }

        public void RemoveSession(string sessionId)
        {
            sessions.Remove(sessionId);
            RemovedSessions.Add(sessionId);
        }
    }
}
