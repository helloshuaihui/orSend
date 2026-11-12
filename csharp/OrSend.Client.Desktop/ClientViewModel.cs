using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Windows.Media;
using System.Windows.Threading;
using OrSend.Desktop.Common;
using OrSend.Shared;

namespace OrSend.ClientDesktop;

public sealed class ClientViewModel : INotifyPropertyChanged
{
    private readonly ClientRuntime _runtime;
    private readonly DispatcherTimer _timer;
    private long _lastLogId;
    private bool _busy;
    private bool _settingsLoaded;

    public ClientViewModel(ClientRuntime runtime)
    {
        _runtime = runtime;

        NavItems = new ObservableCollection<NavItem>
        {
            new() { Key = "overview", Icon = "\uE80F", Title = "概览" },
            new() { Key = "tunnels",  Icon = "\uE968", Title = "隧道" },
            new() { Key = "logs",     Icon = "\uE7C3", Title = "运行日志" },
            new() { Key = "settings", Icon = "\uE713", Title = "设置" }
        };

        ConnectCommand = new RelayCommand(ConnectAsync, () => !EngineOnline);
        DisconnectCommand = new RelayCommand(DisconnectAsync, () => EngineOnline);
        AddTunnelCommand = new RelayCommand(AddTunnelAsync, () => EngineOnline);
        RemoveTunnelCommand = new RelayCommand(p => RemoveTunnelAsync(p as ClientTunnelInfo), _ => EngineOnline);
        SaveSettingsCommand = new RelayCommand(SaveSettingsAsync);
        ClearLogsCommand = new RelayCommand(() =>
        {
            Logs.Clear();
            RecentEvents.Clear();
            _lastLogId = 0;
        });

        _timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(1500) };
        _timer.Tick += async (_, _) => await RefreshAsync();
        _timer.Start();

        _ = RefreshAsync();
    }

    public ObservableCollection<NavItem> NavItems { get; }
    public ObservableCollection<ClientTunnelInfo> Tunnels { get; } = new();
    public ObservableCollection<LogRow> Logs { get; } = new();
    public ObservableCollection<LogRow> RecentEvents { get; } = new();
    public ObservableCollection<string> LogLevels { get; } = new() { "info", "debug", "warn", "error" };

    public RelayCommand ConnectCommand { get; }
    public RelayCommand DisconnectCommand { get; }
    public RelayCommand AddTunnelCommand { get; }
    public RelayCommand RemoveTunnelCommand { get; }
    public RelayCommand SaveSettingsCommand { get; }
    public RelayCommand ClearLogsCommand { get; }

    public event EventHandler? LogsAppended;

    private string _selectedTab = "overview";
    public string SelectedTab { get => _selectedTab; set => Set(ref _selectedTab, value); }

    private bool _engineOnline;
    public bool EngineOnline
    {
        get => _engineOnline;
        set
        {
            if (!Set(ref _engineOnline, value)) return;
            ConnectCommand.RaiseCanExecuteChanged();
            DisconnectCommand.RaiseCanExecuteChanged();
            AddTunnelCommand.RaiseCanExecuteChanged();
            RemoveTunnelCommand.RaiseCanExecuteChanged();
        }
    }

    private string _stateText = "未连接";
    public string StateText { get => _stateText; set => Set(ref _stateText, value); }

    private Brush _stateBrush = new SolidColorBrush(Color.FromRgb(0x6B, 0x72, 0x80));
    public Brush StateBrush { get => _stateBrush; set => Set(ref _stateBrush, value); }

    private Brush _stateWeakBrush = new SolidColorBrush(Color.FromRgb(0xF3, 0xF4, 0xF6));
    public Brush StateWeakBrush { get => _stateWeakBrush; set => Set(ref _stateWeakBrush, value); }

    private string _versionText = "";
    public string VersionText { get => _versionText; set => Set(ref _versionText, value); }

    private string _tunnelSummary = "-";
    public string TunnelSummary { get => _tunnelSummary; set => Set(ref _tunnelSummary, value); }

    private string _statusBar = "就绪";
    public string StatusBar { get => _statusBar; set => Set(ref _statusBar, value); }

    private string _statusRight = "";
    public string StatusRight { get => _statusRight; set => Set(ref _statusRight, value); }

    private string _settingsHint = "";
    public string SettingsHint { get => _settingsHint; set => Set(ref _settingsHint, value); }

    private string _engineOutput = "";
    public string EngineOutput { get => _engineOutput; set => Set(ref _engineOutput, value); }

    private string _statState = "未连接";
    public string StatState { get => _statState; set => Set(ref _statState, value); }

    private string _statTunnels = "0";
    public string StatTunnels { get => _statTunnels; set => Set(ref _statTunnels, value); }

    private string _statStreams = "0";
    public string StatStreams { get => _statStreams; set => Set(ref _statStreams, value); }

    private string _statUptime = "-";
    public string StatUptime { get => _statUptime; set => Set(ref _statUptime, value); }

    private string _infoServer = "-";
    public string InfoServer { get => _infoServer; set => Set(ref _infoServer, value); }

    private string _infoClientId = "-";
    public string InfoClientId { get => _infoClientId; set => Set(ref _infoClientId, value); }

    private string _infoName = "-";
    public string InfoName { get => _infoName; set => Set(ref _infoName, value); }

    private string _infoControlPort = "-";
    public string InfoControlPort { get => _infoControlPort; set => Set(ref _infoControlPort, value); }

    private string _infoLastError = "-";
    public string InfoLastError { get => _infoLastError; set => Set(ref _infoLastError, value); }

    // 设置表单
    private string _settingServer = "";
    public string SettingServer { get => _settingServer; set => Set(ref _settingServer, value); }

    private string _settingToken = "";
    public string SettingToken { get => _settingToken; set => Set(ref _settingToken, value); }

    private string _settingClientId = "";
    public string SettingClientId { get => _settingClientId; set => Set(ref _settingClientId, value); }

    private string _settingName = "";
    public string SettingName { get => _settingName; set => Set(ref _settingName, value); }

    private string _settingControlPort = "";
    public string SettingControlPort { get => _settingControlPort; set => Set(ref _settingControlPort, value); }

    private string _settingLogLevel = "info";
    public string SettingLogLevel { get => _settingLogLevel; set => Set(ref _settingLogLevel, value); }

    private bool _settingAutoConnect = true;
    public bool SettingAutoConnect { get => _settingAutoConnect; set => Set(ref _settingAutoConnect, value); }

    // 新建隧道表单
    private string _newTunnelName = "";
    public string NewTunnelName { get => _newTunnelName; set => Set(ref _newTunnelName, value); }

    private string _newTunnelHost = "127.0.0.1";
    public string NewTunnelHost { get => _newTunnelHost; set => Set(ref _newTunnelHost, value); }

    private string _newTunnelLocalPort = "";
    public string NewTunnelLocalPort { get => _newTunnelLocalPort; set => Set(ref _newTunnelLocalPort, value); }

    private string _newTunnelRemotePort = "";
    public string NewTunnelRemotePort { get => _newTunnelRemotePort; set => Set(ref _newTunnelRemotePort, value); }

    private bool _autoScroll = true;
    public bool AutoScroll { get => _autoScroll; set => Set(ref _autoScroll, value); }

    // ---------------- 刷新 ----------------
    public async Task RefreshAsync()
    {
        if (_busy) return;
        _busy = true;
        try
        {
            var snap = await _runtime.GetSnapshotAsync();

            EngineOnline = snap.EngineOnline;
            EngineOutput = snap.RecentOutput;

            if (!_settingsLoaded)
            {
                LoadSettings(snap.Settings);
                _settingsLoaded = true;
            }

            if (snap.Status is { } st)
            {
                ApplyStatus(st);
                await FetchLogsAsync();
            }
            else
            {
                SetStateVisual("未连接", 0x6B, 0x72, 0x80, 0xF3, 0xF4, 0xF6);
                StatState = "未连接";
                if (!string.IsNullOrEmpty(snap.LastError)) StatusBar = snap.LastError;
            }
        }
        catch (Exception ex)
        {
            StatusBar = "刷新状态失败: " + ex.Message;
        }
        finally
        {
            _busy = false;
        }
    }

    private void LoadSettings(ClientSettings s)
    {
        SettingServer = s.Server;
        SettingToken = s.Token;
        SettingClientId = s.ClientId;
        SettingName = s.Name;
        SettingControlPort = s.ControlPort.ToString();
        SettingLogLevel = string.IsNullOrEmpty(s.LogLevel) ? "info" : s.LogLevel;
        SettingAutoConnect = s.AutoConnect;
    }

    private void ApplyStatus(EngineStatus st)
    {
        VersionText = "v" + st.Version;

        var c = st.Client;
        var registered = c.Tunnels.Count(t => t.Registered);
        StatTunnels = $"{registered}/{c.Tunnels.Count}";
        StatStreams = c.ActiveStreams.ToString();
        StatUptime = c.UptimeSec > 0 ? Fmt.Duration(c.UptimeSec) : "-";

        switch (c.State)
        {
            case "ready":
                SetStateVisual("已连接", 0x16, 0xA3, 0x4A, 0xEC, 0xFD, 0xF3);
                StatState = "已连接";
                break;
            case "connecting":
                SetStateVisual("连接中", 0xB4, 0x53, 0x09, 0xFF, 0xFB, 0xEB);
                StatState = "连接中";
                break;
            case "handshaking":
                SetStateVisual("握手中", 0xB4, 0x53, 0x09, 0xFF, 0xFB, 0xEB);
                StatState = "握手中";
                break;
            case "backoff":
                SetStateVisual("重连中", 0xB4, 0x53, 0x09, 0xFF, 0xFB, 0xEB);
                StatState = "重连中";
                break;
            default:
                SetStateVisual("未连接", 0x6B, 0x72, 0x80, 0xF3, 0xF4, 0xF6);
                StatState = "未连接";
                break;
        }

        InfoServer = string.IsNullOrEmpty(c.Server) ? "-" : c.Server;
        InfoClientId = string.IsNullOrEmpty(c.ClientId) ? "-" : c.ClientId;
        InfoName = string.IsNullOrEmpty(c.Name) ? "-" : c.Name;
        InfoControlPort = st.Control.Port.ToString();
        InfoLastError = string.IsNullOrEmpty(c.LastError) ? "-" : c.LastError!;

        Sync(Tunnels, c.Tunnels, t => $"{t.Name}|{t.Registered}|{t.PublicPort}|{t.Connections}|{t.LastError}");
        TunnelSummary = c.Tunnels.Count == 0 ? "尚未配置隧道" : $"{c.Tunnels.Count} 条隧道";

        StatusBar = c.State == "ready"
            ? $"已连接 {c.Server} · 隧道 {registered}/{c.Tunnels.Count} 条已注册 · 在线 {Fmt.Duration(c.UptimeSec)}"
            : $"当前状态：{StateText}" + (string.IsNullOrEmpty(c.LastError) ? "" : " · " + c.LastError);
        StatusRight = $"控制接口 127.0.0.1:{st.Control.Port}";
    }

    private void SetStateVisual(string text, byte r, byte g, byte b, byte wr, byte wg, byte wb)
    {
        StateText = text;
        StateBrush = new SolidColorBrush(Color.FromRgb(r, g, b));
        StateWeakBrush = new SolidColorBrush(Color.FromRgb(wr, wg, wb));
    }

    private async Task FetchLogsAsync()
    {
        var client = _runtime.Client;
        if (client is null) return;
        try
        {
            var result = await client.CallAsync<LogsResult>("logs", new { sinceId = _lastLogId, limit = 200 });
            if (result?.Entries is not { Count: > 0 }) return;

            foreach (var item in result.Entries)
            {
                if (item.Id <= _lastLogId) continue;
                var row = LogRow.From(item);
                Logs.Add(row);
                RecentEvents.Add(row);
            }

            _lastLogId = result.LastId > 0 ? result.LastId : _lastLogId;

            while (Logs.Count > 500) Logs.RemoveAt(0);
            while (RecentEvents.Count > 6) RecentEvents.RemoveAt(0);

            LogsAppended?.Invoke(this, EventArgs.Empty);
        }
        catch
        {
            // 日志拉取失败不影响主状态
        }
    }

    private static void Sync<T>(ObservableCollection<T> target, IList<T> source, Func<T, string> signature)
    {
        if (target.Count == source.Count)
        {
            var same = true;
            for (var i = 0; i < source.Count; i++)
            {
                if (signature(target[i]) == signature(source[i])) continue;
                same = false;
                break;
            }
            if (same) return;
        }

        target.Clear();
        foreach (var item in source) target.Add(item);
    }

    // ---------------- 动作 ----------------
    private async Task ConnectAsync()
    {
        StatusBar = "正在启动 agent…";
        var ok = await _runtime.StartAsync();
        StatusBar = ok ? "agent 已启动" : "启动失败：未找到 orsend.exe";
        await RefreshAsync();
    }

    private async Task DisconnectAsync()
    {
        await _runtime.StopAsync();
        StatusBar = "agent 已停止";
        await RefreshAsync();
    }

    private async Task AddTunnelAsync()
    {
        if (_runtime.Client is null) return;

        var name = NewTunnelName.Trim();
        if (name.Length == 0)
        {
            StatusBar = "请填写隧道名";
            return;
        }
        if (!int.TryParse(NewTunnelLocalPort, out var localPort) || localPort <= 0 || localPort > 65535)
        {
            StatusBar = "本地端口不合法";
            return;
        }

        var remotePort = 0;
        if (!string.IsNullOrWhiteSpace(NewTunnelRemotePort) &&
            (!int.TryParse(NewTunnelRemotePort, out remotePort) || remotePort < 0 || remotePort > 65535))
        {
            StatusBar = "公网端口不合法";
            return;
        }

        var host = string.IsNullOrWhiteSpace(NewTunnelHost) ? "127.0.0.1" : NewTunnelHost.Trim();

        try
        {
            await _runtime.Client.CallAsync("client.addTunnel", new
            {
                name,
                localHost = host,
                localPort,
                remotePort
            });

            _runtime.Settings.Tunnels.RemoveAll(t => t.Name == name);
            _runtime.Settings.Tunnels.Add(new TunnelSetting
            {
                Name = name,
                LocalHost = host,
                LocalPort = localPort,
                RemotePort = remotePort
            });
            _runtime.SaveSettings();

            NewTunnelName = "";
            NewTunnelLocalPort = "";
            NewTunnelRemotePort = "";
            StatusBar = $"已添加隧道 {name}";
            await RefreshAsync();
        }
        catch (Exception ex)
        {
            StatusBar = "添加失败: " + ex.Message;
        }
    }

    private async Task RemoveTunnelAsync(ClientTunnelInfo? tunnel)
    {
        if (tunnel is null || _runtime.Client is null) return;
        try
        {
            await _runtime.Client.CallAsync("client.removeTunnel", new { name = tunnel.Name });
            _runtime.Settings.Tunnels.RemoveAll(t => t.Name == tunnel.Name);
            _runtime.SaveSettings();
            StatusBar = $"已删除隧道 {tunnel.Name}";
            await RefreshAsync();
        }
        catch (Exception ex)
        {
            StatusBar = "删除失败: " + ex.Message;
        }
    }

    private async Task SaveSettingsAsync()
    {
        SettingsHint = "";
        if (!int.TryParse(SettingControlPort, out var port) || port <= 0 || port > 65535)
        {
            SettingsHint = "控制端口不合法";
            return;
        }

        var settings = new ClientSettings
        {
            Server = SettingServer.Trim(),
            Token = SettingToken,
            ClientId = SettingClientId.Trim(),
            Name = SettingName.Trim(),
            ControlPort = port,
            LogLevel = string.IsNullOrWhiteSpace(SettingLogLevel) ? "info" : SettingLogLevel,
            AutoConnect = SettingAutoConnect,
            Tunnels = _runtime.Settings.Tunnels
        };

        SettingsHint = "正在重启 agent…";
        var ok = await _runtime.RestartAsync(settings);
        SettingsHint = ok ? "已保存并重连" : "重启失败，请查看引擎输出";
        await RefreshAsync();
    }

    // ---------------- INotifyPropertyChanged ----------------
    public event PropertyChangedEventHandler? PropertyChanged;

    private bool Set<T>(ref T field, T value, [CallerMemberName] string? name = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value)) return false;
        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
        return true;
    }
}
