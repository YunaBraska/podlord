using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Podlord.Core;

namespace Podlord.App;

internal enum SessionOpenTarget
{
    Activate,
    Tab,
    Window
}

internal interface ISessionSurfaceHost
{
    string HostId { get; }
    Window Window { get; }
    bool IsDetached { get; }
    bool ContainsSession(string sessionId);
    void AddSession(string sessionId, bool activate);
    void ActivateSession(string sessionId);
    void RemoveSession(string sessionId);
    bool IsEmpty { get; }
}

internal sealed class AppRuntime
{
    private readonly object sync = new();
    private readonly Dictionary<string, ISessionSurfaceHost> hosts = new(StringComparer.Ordinal);
    private readonly Dictionary<string, string> sessionHosts = new(StringComparer.Ordinal);
    private readonly Action<Window> focusWindowAction;
    private readonly Func<string, ISessionSurfaceHost> detachedWindowFactory;
    private readonly bool hasCustomDetachedWindowFactory;

    internal AppRuntime(
        AppState state,
        IKubernetesApplicationPort service,
        Action<Window>? focusWindowAction = null,
        Func<string, ISessionSurfaceHost>? detachedWindowFactory = null)
    {
        State = state;
        Service = service;
        this.focusWindowAction = focusWindowAction ?? FocusWindow;
        hasCustomDetachedWindowFactory = detachedWindowFactory is not null;
        this.detachedWindowFactory = detachedWindowFactory ?? (sessionId => new MainWindow(this, [], initialSessionId: sessionId, loadStartupKubeconfigs: false, detached: true));
    }

    public AppState State { get; }

    public IKubernetesApplicationPort Service { get; }

    public event EventHandler? SessionPlacementsChanged;

    public static AppRuntime LoadDefault()
    {
        var state = AppState.LoadDefault();
        var service = KubernetesServiceBootstrap.Create(state);
        return new AppRuntime(state, service);
    }

    public void RegisterHost(ISessionSurfaceHost host)
    {
        lock (sync)
        {
            hosts[host.HostId] = host;
        }
    }

    public void UnregisterHost(ISessionSurfaceHost host)
    {
        var changed = false;
        lock (sync)
        {
            hosts.Remove(host.HostId);
            foreach (var sessionId in sessionHosts.Where(entry => entry.Value.Equals(host.HostId, StringComparison.Ordinal)).Select(entry => entry.Key).ToList())
            {
                sessionHosts.Remove(sessionId);
                changed = true;
            }
        }

        if (changed)
        {
            NotifySessionPlacementsChanged();
        }
    }

    public void OpenOrActivateSession(string sessionId, string sourceHostId, SessionOpenTarget target)
    {
        ISessionSurfaceHost? existingHost;
        lock (sync)
        {
            existingHost = sessionHosts.TryGetValue(sessionId, out var hostId) && hosts.TryGetValue(hostId, out var registered)
                ? registered
                : null;
        }

        if (existingHost is not null)
        {
            if (target == SessionOpenTarget.Window
                && existingHost.HostId.Equals(sourceHostId, StringComparison.Ordinal)
                && !existingHost.IsDetached)
            {
                DetachSession(sessionId, sourceHostId);
                return;
            }

            ActivateHostSession(existingHost, sessionId);
            return;
        }

        if (target == SessionOpenTarget.Window)
        {
            CreateDetachedWindow(sessionId);
            return;
        }

        if (!TryGetHost(sourceHostId, out var sourceHost) || sourceHost is null)
        {
            return;
        }

        sourceHost.AddSession(sessionId, activate: true);
        lock (sync)
        {
            sessionHosts[sessionId] = sourceHost.HostId;
        }
        NotifySessionPlacementsChanged();
        focusWindowAction(sourceHost.Window);
    }

    public bool RegisterSessionPlacement(string sessionId, string hostId)
    {
        var changed = false;
        lock (sync)
        {
            if (sessionHosts.TryGetValue(sessionId, out var existingHostId)
                && !existingHostId.Equals(hostId, StringComparison.Ordinal)
                && hosts.ContainsKey(existingHostId))
            {
                return false;
            }

            changed = !sessionHosts.TryGetValue(sessionId, out var currentHostId)
                      || !currentHostId.Equals(hostId, StringComparison.Ordinal);
            sessionHosts[sessionId] = hostId;
        }

        if (changed)
        {
            NotifySessionPlacementsChanged();
        }

        return true;
    }

    public bool IsSessionOpen(string sessionId)
    {
        lock (sync)
        {
            return sessionHosts.ContainsKey(sessionId);
        }
    }

    public bool ActivateExistingSession(string sessionId)
    {
        if (!TryGetExistingHost(sessionId, out var host) || host is null)
        {
            return false;
        }

        ActivateHostSession(host, sessionId);
        return true;
    }

    public void UnregisterSessionPlacement(string sessionId, string hostId)
    {
        var changed = false;
        lock (sync)
        {
            if (sessionHosts.TryGetValue(sessionId, out var existing) && existing.Equals(hostId, StringComparison.Ordinal))
            {
                sessionHosts.Remove(sessionId);
                changed = true;
            }
        }

        if (changed)
        {
            NotifySessionPlacementsChanged();
        }
    }

    public void DetachSession(string sessionId, string sourceHostId)
    {
        if (TryGetExistingHost(sessionId, out var existingHost) && existingHost is not null && existingHost.IsDetached)
        {
            ActivateHostSession(existingHost, sessionId);
            return;
        }

        if (!TryGetHost(sourceHostId, out var sourceHost) || sourceHost is null)
        {
            return;
        }

        sourceHost.RemoveSession(sessionId);
        UnregisterSessionPlacement(sessionId, sourceHostId);
        CreateDetachedWindow(sessionId);
    }

    private void CreateDetachedWindow(string sessionId)
    {
        if (!hasCustomDetachedWindowFactory && Application.Current?.ApplicationLifetime is not IClassicDesktopStyleApplicationLifetime)
        {
            return;
        }

        var host = detachedWindowFactory(sessionId);
        RegisterHost(host);
        if (!host.ContainsSession(sessionId))
        {
            host.AddSession(sessionId, activate: true);
        }
        RegisterSessionPlacement(sessionId, host.HostId);
        host.Window.Show();
        focusWindowAction(host.Window);
    }

    private void NotifySessionPlacementsChanged()
    {
        SessionPlacementsChanged?.Invoke(this, EventArgs.Empty);
    }

    private void RemoveDuplicateSessionCopies(string sessionId, string keepHostId)
    {
        List<ISessionSurfaceHost> duplicates;
        lock (sync)
        {
            duplicates = hosts.Values
                .Where(host => !host.HostId.Equals(keepHostId, StringComparison.Ordinal) && host.ContainsSession(sessionId))
                .ToList();
        }

        foreach (var host in duplicates)
        {
            host.RemoveSession(sessionId);
        }
    }

    private void ActivateHostSession(ISessionSurfaceHost host, string sessionId)
    {
        RemoveDuplicateSessionCopies(sessionId, host.HostId);
        host.ActivateSession(sessionId);
        focusWindowAction(host.Window);
    }

    private bool TryGetExistingHost(string sessionId, out ISessionSurfaceHost? host)
    {
        lock (sync)
        {
            if (sessionHosts.TryGetValue(sessionId, out var hostId) && hosts.TryGetValue(hostId, out var existing))
            {
                host = existing;
                return true;
            }
        }

        host = null;
        return false;
    }

    private bool TryGetHost(string hostId, out ISessionSurfaceHost? host)
    {
        lock (sync)
        {
            if (hosts.TryGetValue(hostId, out var existing))
            {
                host = existing;
                return true;
            }
        }

        host = null;
        return false;
    }

    private static void FocusWindow(Window window)
    {
        if (window.WindowState == WindowState.Minimized)
        {
            window.WindowState = WindowState.Normal;
        }

        window.Activate();
    }
}
