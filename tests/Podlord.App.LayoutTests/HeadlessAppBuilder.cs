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
            .UseHeadless(new AvaloniaHeadlessPlatformOptions
            {
                UseHeadlessDrawing = true
            });

    public static void EnsureStarted()
    {
        lock (Gate)
        {
            if (started)
            {
                return;
            }
            BuildAvaloniaApp().SetupWithoutStarting();
            started = true;
        }
    }
}
