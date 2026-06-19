using Avalonia;
using Avalonia.Headless;
using Podlord.App;

[assembly: Xunit.CollectionBehavior(DisableTestParallelization = true)]

namespace Podlord.App.LayoutTests;

public static class HeadlessAppBuilder
{
    private static readonly object Gate = new();
    private static bool started;

    public static AppBuilder BuildAvaloniaApp() =>
        AppBuilder.Configure<App>()
            .UseSkia()
            .UseHeadless(new AvaloniaHeadlessPlatformOptions
            {
                UseHeadlessDrawing = false
            });

    public static void EnsureStarted()
    {
        lock (Gate)
        {
            if (started)
            {
                return;
            }

            var sandbox = Path.Combine(Path.GetTempPath(), $"podlord-layout-tests-{Guid.NewGuid():N}");
            Directory.CreateDirectory(sandbox);
            Environment.SetEnvironmentVariable("PODLORD_CONFIG_HOME", sandbox);
            Environment.SetEnvironmentVariable("PODLORD_HOME", sandbox);

            try
            {
                BuildAvaloniaApp().SetupWithoutStarting();
            }
            catch (InvalidOperationException)
            {
                // Another test already configured an Avalonia application in this process.
            }
            started = true;
        }
    }
}
