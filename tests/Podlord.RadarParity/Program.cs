using System.Diagnostics;
using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Media;
using Avalonia.Threading;
using Podlord.App;
using Podlord.Core;
using Podlord.Kubernetes;

if (args.Length != 2 || !File.Exists(args[0]))
{
    Console.Error.WriteLine("Usage: Podlord.RadarParity <owned-local-kubeconfig> <evidence-directory>");
    return 2;
}

Directory.CreateDirectory(args[1]);
AppBuilder.Configure<App>().UseSkia()
    .UseHeadless(new AvaloniaHeadlessPlatformOptions { UseHeadlessDrawing = false })
    .SetupWithoutStarting();
var state = AppState.InMemory();
state.SaveSettings(state.Settings() with
{
    ScreensaverEnabled = false, RadarWaterEnabled = false,
    RadarAutoFollowAlerts = false, AnimationIntensity = 0
});
state.ImportKubeconfig(args[0]);
var session = state.ListSessions().Single();
var service = new KubernetesResourceService(state);
var query = new ResourceQuery(SessionId: session.Id, Limit: 5000);
Pump(service.WarmResourceCacheAsync(query));
using var model = new MainWindowViewModel(state, service);
model.ReloadSessions(openDefaultSession: false);
model.OpenSessionTab(session.Id, activate: true);
foreach (var rule in model.AlertRules) rule.Enabled = false;
model.SaveAlertRules();
Pump(model.RefreshResourcesAsync());
model.SetRadarViewport(1200, 1200);
model.ResetRadarView();
Dispatcher.UIThread.RunJobs();
var snapshot = service.GetCachedResourceSnapshot(query, applyFilters: false);
var blocks = model.RadarBlocks.ToArray();
var expected = snapshot.Rows.Count(row => row.Kind != "Namespace");
var resourceIds = snapshot.Rows.Select(row => row.Id).ToHashSet(StringComparer.Ordinal);
var blockIds = blocks.Where(block => !block.Resource.Id.StartsWith("radar:", StringComparison.Ordinal))
    .Select(block => block.Resource.Id).ToHashSet(StringComparer.Ordinal);
if (expected < 1000 || snapshot.Rows.Any(row => row.Kind != "Namespace" && !blockIds.Contains(row.Id))
    || !blockIds.IsSubsetOf(resourceIds))
    throw new InvalidOperationException("The reference must include the complete real Kubernetes cache, not a partial viewport.");
var document = new
{
    boundary = "Real Kubernetes cache through C# application public entrypoints; native consumes this exact immutable projection",
    sessionId = session.Id, cluster = snapshot.Cluster,
    width = 1200, height = 1200, zoom = model.RadarZoom, panX = model.RadarPanX, panY = model.RadarPanY,
    rows = snapshot.Rows.Select(row => new
    {
        path = row.Id, uid = row.Id.Split(':')[^1], kind = row.Kind, name = row.Name,
        @namespace = row.Namespace, cluster = row.Cluster, owner = row.Owner,
        status = row.Status, restarts = row.Restarts
    }),
    blocks = blocks.Select(block => new
    {
        path = block.Resource.Id, kind = block.Resource.Kind,
        x = block.WorldX + block.WorldWidth / 2, y = block.WorldY + block.WorldHeight / 2,
        color = (block.Brush as ISolidColorBrush)?.Color.ToString(),
        decoration = block.Resource.Id.StartsWith("radar:", StringComparison.Ordinal)
    })
};
File.WriteAllText(Path.Combine(args[1], "reference.json"), JsonSerializer.Serialize(document, new JsonSerializerOptions { WriteIndented = true }));
var layer = new RadarBlockLayer { Blocks = model.RadarBlocks, PanX = model.RadarPanX, PanY = model.RadarPanY, Zoom = model.RadarZoom };
var window = new Window { Width = 1200, Height = 1200, Content = layer, Background = Brush.Parse("#12212B") };
try
{
    window.Show(); window.UpdateLayout(); Dispatcher.UIThread.RunJobs();
    using var image = window.CaptureRenderedFrame() ?? throw new InvalidOperationException("C# renderer produced no frame.");
    image.Save(Path.Combine(args[1], "csharp-radar.png"));
}
finally { window.Close(); Dispatcher.UIThread.RunJobs(); }
Console.WriteLine($"Captured {snapshot.Rows.Count} real resources and {blocks.Length} reference blocks.");
return 0;

static void Pump(Task task)
{
    var deadline = Stopwatch.StartNew();
    while (!task.IsCompleted)
    {
        if (deadline.Elapsed > TimeSpan.FromMinutes(5)) throw new TimeoutException("Reference cache did not settle.");
        Dispatcher.UIThread.RunJobs(); Thread.Sleep(5);
    }
    task.GetAwaiter().GetResult(); Dispatcher.UIThread.RunJobs();
}
