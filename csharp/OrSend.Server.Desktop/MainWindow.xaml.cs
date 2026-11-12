using System.Windows;
using OrSend.Desktop.Common;
using OrSend.Shared;

namespace OrSend.ServerDesktop;

public partial class MainWindow : Window
{
    private readonly ServerViewModel _vm;

    public MainWindow(ServerRuntime runtime)
    {
        InitializeComponent();
        WindowFit.ClampToWorkArea(this);

        _vm = new ServerViewModel(runtime);
        DataContext = _vm;
        _vm.LogsAppended += (_, _) =>
        {
            if (_vm.AutoScroll) LogScroll.ScrollToEnd();
        };
    }

    private void OnMinimize(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;

    private void OnMaximize(object sender, RoutedEventArgs e) =>
        WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;

    private void OnClose(object sender, RoutedEventArgs e) => Close();
}
