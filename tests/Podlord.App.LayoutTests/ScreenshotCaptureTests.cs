using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using Podlord.Core;

namespace Podlord.App.LayoutTests;

[Collection("Headless")]
public sealed class ScreenshotCaptureTests
{
    public ScreenshotCaptureTests()
    {
        HeadlessAppBuilder.EnsureStarted();
    }

    [Fact]
    public void Capture_resource_explorer_and_inspector_screenshots_when_enabled()
    {
        var enabled = string.Equals(Environment.GetEnvironmentVariable("PODLORD_CAPTURE_SCREENSHOTS"), "1", StringComparison.Ordinal);
        if (!enabled)
        {
            return;
        }

        var outputDir = Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "..", "doc", "screenshots");
        Directory.CreateDirectory(outputDir);

        Dispatcher.UIThread.Invoke(() =>
        {
            var window = new MainWindow([])
            {
                Width = 1440,
                Height = 860
            };
            try
            {
                window.Show();
                Dispatcher.UIThread.RunJobs();
                var demoRows = BuildDemoRows();
                window.ViewModel.SelectedSession = new PodlordSession(
                    Id: "demo-session",
                    DisplayName: "podlord-demo",
                    ContextId: "demo-context",
                    ClusterName: "prod-eu-1",
                    NamespaceScope: NamespaceScope.All,
                    SafetyLevel: SafetyLevel.Dev,
                    Color: null,
                    Icon: null,
                    Active: true,
                    CreatedAt: "2026-06-01T00:00:00.0000000Z");
                window.ViewModel.SeedCachedRowsForTesting(demoRows);
                window.ViewModel.ForceRadarLiveForTesting();
                window.UpdateLayout();
                Dispatcher.UIThread.RunJobs();

                SaveFrame(window, Path.Combine(outputDir, "resource-explorer.png"));

                if (window.ViewModel.Resources.Count > 0)
                {
                    window.ViewModel.SelectedResourceRow = window.ViewModel.Resources[0];
                    Dispatcher.UIThread.RunJobs();
                    window.UpdateLayout();
                    Dispatcher.UIThread.RunJobs();
                }

                SaveFrame(window, Path.Combine(outputDir, "inspector-settings.png"));
            }
            finally
            {
                window.Close();
                Dispatcher.UIThread.RunJobs();
            }
        });
    }

    private static void SaveFrame(Window window, string path)
    {
        if (window.CaptureRenderedFrame() is { } bitmap)
        {
            bitmap.Save(path);
        }
    }

    private static IReadOnlyList<FlatResourceRow> BuildDemoRows()
    {
        var rows = new List<FlatResourceRow>();
        rows.Add(new FlatResourceRow(
            Id: "demo:Pod:payments:api-7f9b:uid-1",
            Status: "Running",
            Kind: "Pod",
            Name: "api-7f9b",
            Namespace: "payments",
            Cluster: "prod-eu-1",
            Age: "3d 12h",
            Ready: "2/2",
            Restarts: 0,
            Node: "node-3",
            ImageSummary: "api:v1.42",
            Owner: "ReplicaSet/api",
            LastChange: "now",
            Freshness: FreshnessState.Fresh)
        {
            Pulse = ResourcePulse.Empty with
            {
                CpuMillicores = 280,
                CpuLimitMillicores = 500,
                MemoryBytes = 320L * 1024 * 1024,
                MemoryLimitBytes = 512L * 1024 * 1024,
                SourceBadge = "LIVE"
            }
        });
        rows.Add(new FlatResourceRow(
            Id: "demo:Pod:payments:checkout-2b3:uid-2",
            Status: "CrashLoopBackOff",
            Kind: "Pod",
            Name: "checkout-2b3",
            Namespace: "payments",
            Cluster: "prod-eu-1",
            Age: "11m",
            Ready: "0/1",
            Restarts: 7,
            Node: "node-2",
            ImageSummary: "checkout:v0.9",
            Owner: "Deployment/checkout",
            LastChange: "1m",
            Freshness: FreshnessState.Fresh));
        rows.Add(new FlatResourceRow(
            Id: "demo:Pod:logistics:fulfillment-1a8:uid-3",
            Status: "Running",
            Kind: "Pod",
            Name: "fulfillment-1a8",
            Namespace: "logistics",
            Cluster: "prod-eu-1",
            Age: "1d 4h",
            Ready: "1/1",
            Restarts: 0,
            Node: "node-1",
            ImageSummary: "fulfillment:1.7",
            Owner: "StatefulSet/fulfillment",
            LastChange: "12h",
            Freshness: FreshnessState.Fresh));
        rows.Add(new FlatResourceRow(
            Id: "demo:Service:payments:api:uid-4",
            Status: "Healthy",
            Kind: "Service",
            Name: "api",
            Namespace: "payments",
            Cluster: "prod-eu-1",
            Age: "27d 18h",
            Ready: "-",
            Restarts: 0,
            Node: null,
            ImageSummary: "-",
            Owner: null,
            LastChange: "27d",
            Freshness: FreshnessState.Fresh));
        rows.Add(new FlatResourceRow(
            Id: "demo:Node:cluster:node-3:uid-5",
            Status: "Ready",
            Kind: "Node",
            Name: "node-3",
            Namespace: null,
            Cluster: "prod-eu-1",
            Age: "92d",
            Ready: "Ready",
            Restarts: 0,
            Node: null,
            ImageSummary: "linux 6.6",
            Owner: null,
            LastChange: "92d",
            Freshness: FreshnessState.Fresh)
        {
            Pulse = ResourcePulse.Empty with
            {
                CpuMillicores = 3200,
                CpuLimitMillicores = 8000,
                MemoryBytes = 18L * 1024 * 1024 * 1024,
                MemoryLimitBytes = 32L * 1024 * 1024 * 1024,
                SourceBadge = "LIVE"
            }
        });
        return rows;
    }
}
