using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;

namespace Podlord.App;

public partial class App : Application
{
    private AppRuntime? runtime;

    public override void Initialize()
    {
        AvaloniaXamlLoader.Load(this);
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            runtime = AppRuntime.LoadDefault();
            desktop.MainWindow = new MainWindow(runtime, desktop.Args ?? [], initialSessionId: null, loadStartupKubeconfigs: true, detached: false);
        }

        base.OnFrameworkInitializationCompleted();
    }
}
