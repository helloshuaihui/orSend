using System.IO;
using System.Windows;
using OrSend.Shared;

namespace OrSend.ClientDesktop;

public partial class App : Application
{
    public ClientRuntime Runtime { get; private set; } = null!;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        var dataDir = Path.Combine(AppContext.BaseDirectory, "App_Data");
        Directory.CreateDirectory(dataDir);
        var store = new JsonFileStore<ClientSettings>(Path.Combine(dataDir, "client-settings.json"));

        Runtime = new ClientRuntime(store, new EngineOptions());

        var window = new MainWindow(Runtime);
        MainWindow = window;
        window.Show();

        _ = Runtime.InitializeAsync();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        try
        {
            Runtime?.DisposeAsync().AsTask().Wait(4000);
        }
        catch
        {
            // 退出时不再抛出
        }
        base.OnExit(e);
    }
}
