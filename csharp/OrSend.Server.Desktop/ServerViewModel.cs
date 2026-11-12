using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Windows.Media;
using System.Windows.Threading;
using OrSend.Desktop.Common;
using OrSend.Shared;

namespace OrSend.ServerDesktop;

public sealed class ServerViewModel : INotifyPropertyChanged
{
    private readonly ServerRuntime _runtime;
    private readonly DispatcherTimer _timer;
    private long _lastLogId;
    private bool _busy;
    private bool _settingsLoaded;

    public ServerViewModel(ServerRuntime runtime)
    {
        _runtime = runtime;

        NavItems = new ObservableCollection<NavItem>
        {
            new() { Key = "overview", Icon = "\uE80F", Title = "概览" },
            new() { Key = "agents",   Icon = "\uE968", Title = "客户端与隧道" },
            new() { Key = "forward",  Icon = "\uE8AB", Title = "端口转发" },
            new() { Key = "mapping",  Icon = "\uE71D", Title = "端口映射" },
            new() { Key = "logs",     Icon = "\uE7C3", Title = "运行日志" },
            new() { Key = "settings", Icon = "\uE713", Title = "设置" }
        };

        StartCommand = new RelayCommand(StartAsync, () => !EngineOnline);
        StopCommand = new RelayCommand(StopAsync, () => EngineOnline);
        KickAgentCommand = new RelayCommand(p => KickAgentAsync(p as AgentInfo), _ => EngineOnline);
        AddForwardCommand = new RelayCommand(AddForwardAsync, () => EngineOnline);
        RemoveForwardCommand = new RelayCommand(p => RemoveForwardAsync(p as RuleInfo), _ => EngineOnline);
        AddMappingCommand = new RelayCommand(AddMappingAsync, () => EngineOnline);
        RemoveMappingCommand = new RelayCommand(p => RemoveMappingAsync(p as RuleInfo), _ => EngineOnline);
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

    // ---------------- 集合 ----------------
    public ObservableCollection<NavItem> NavItems { get; }
    public ObservableCollection<AgentInfo> Agents { get; } = new();
    public ObservableCollection<TunnelInfo> Tunnels { get; } = new();
    public ObservableCollection<RuleInfo> ForwardRules { get; } = new();
    public ObservableCollection<RuleInfo> MappingRules { get; } = new();
    public ObservableCollection<LogRow> Logs { get; } = new();
    public ObservableCollection<LogRow> RecentEvents { get; } = new();

    public ObservableCollection<string> Strategies { get; } = new() { "roundrobin", "leastconn", "random", "failover" };
    public ObservableCollection<string> LogLevels { get; } = new() { "info", "debug", "warn", "error" };

    // ---------------- 命令 ----------------
    public RelayCommand StartCommand { get; }
    public RelayCommand StopCommand { get; }
    public RelayCommand KickAgentCommand { get; }
    public RelayCommand AddForwardCommand { get; }
    public RelayCommand RemoveForwardCommand { get; }
    public RelayCommand AddMappingCommand { get; }
    public RelayCommand RemoveMappingCommand { get; }
    public RelayCommand SaveSettingsCommand { get; }
    public RelayCommand ClearLogsCommand { get; }

    public event EventHandler? LogsAppended;

    // ---------------- 状态 ----------------
    private string _selectedTab = "overview";
    public string SelectedTab
    {
        get => _selectedTab;
        set => Set(ref _selectedTab, value);
    }

    private bool _engineOnline;
    public bool EngineOnline
    {
        get => _engineOnline;
        set
        {
            if (!Set(ref _engineOnline, value)) return;
            StartCommand.RaiseCanExecuteChanged();
            StopCommand.RaiseCanExecuteChanged();
            KickAgentCommand.RaiseCanExecuteChanged();
            AddForwardCommand.RaiseCanExecuteChanged();
            RemoveForwardCommand.RaiseCanExecuteChanged();
            AddMappingCommand.RaiseCanExecuteChanged();
            RemoveMappingCommand.RaiseCanExecuteChanged();
        }
    }

    private string _stateText = "未启动";
    public string StateText { get => _stateText; set => Set(ref _stateText, value); }

    private Brush _stateBrush = new SolidColorBrush(Color.FromRgb(0x6B, 0x72, 0x80));
    public Brush StateBrush { get => _stateBrush; set => Set(ref _stateBrush, value); }

    private Brush _stateWeakBrush = new SolidColorBrush(Color.FromRgb(0xF3, 0xF4, 0xF6));
    public Brush StateWeakBrush { get => _stateWeakBrush; set => Set(ref _stateWeakBrush, value); }

    private string _versionText = "";
    public string VersionText { get => _versionText; set => Set(ref _versionText, value); }

    private string _exePath = "";
    public string ExePath { get => _exePath; set => Set(ref _exePath, value); }

    private string _exeName = "未找到 orsend.exe";
    public string ExeName { get => _exeName; set => Set(ref _exeName, value); }

    private string _statusBar = "就绪";
    public string StatusBar { get => _statusBar; set => Set(ref _statusBar, value); }

    private string _statusRight = "";
    public string StatusRight { get => _statusRight; set => Set(ref _statusRight, value); }

    private string _settingsHint = "";
    public string SettingsHint { get => _settingsHint; set => Set(ref _settingsHint, value); }

    private string _engineOutput = "";
    public string EngineOutput { get => _engineOutput; set => Set(ref _engineOutput, value); }

    private string _statAgents = "0";
    public string StatAgents { get => _statAgents; set => Set(ref _statAgents, value); }

    private string _statTunnels = "0";
    public string StatTunnels { get => _statTunnels; set => Set(ref _statTunnels, value); }

    private string _statStreams = "0";
    public string StatStreams { get => _statStreams; set => Set(ref _statStreams, value); }

    private string _statTraffic = "0 B";
    public string StatTraffic { get => _statTraffic; set => Set(ref _statTraffic, value); }

    private string _trafficUp = "0 B";
    public string TrafficUp { get => _trafficUp; set => Set(ref _trafficUp, value); }

    private string _trafficDown = "0 B";
    public string TrafficDown { get => _trafficDown; set => Set(ref _trafficDown, value); }

    private string _infoBind = "-";
    public string InfoBind { get => _infoBind; set => Set(ref _infoBind, value); }

    private string _infoPublicHost = "-";
    public string InfoPublicHost { get => _infoPublicHost; set => Set(ref _infoPublicHost, value); }

    private string _infoControlPort = "-";
    public string InfoControlPort { get => _infoControlPort; set => Set(ref _infoControlPort, value); }

    private string _infoUptime = "-";
    public string InfoUptime { get => _infoUptime; set => Set(ref _infoUptime, value); }

    private string _infoToken = "-";
    public string InfoToken { get => _infoToken; set => Set(ref _infoToken, value); }

    // 设置表单
    private string _settingBind = "";
    public string SettingBind { get => _settingBind; set => Set(ref _settingBind, value); }

    private string _settingToken = "";
    public string SettingToken { get => _settingToken; set => Set(ref _settingToken, value); }

    private string _settingPublicHost = "";
    public string SettingPublicHost { get => _settingPublicHost; set => Set(ref _settingPublicHost, value); }

    private string _settingControlPort = "";
    public string SettingControlPort { get => _settingControlPort; set => Set(ref _settingControlPort, value); }

    private string _settingLogLevel = "info";
    public string SettingLogLevel { get => _settingLogLevel; set => Set(ref _settingLogLevel, value); }

    private bool _settingAutoStart = true;
    public bool SettingAutoStart { get => _settingAutoStart; set => Set(ref _settingAutoStart, value); }

    // 新建规则表单
    private string _newForwardName = "";
    public string NewForwardName { get => _newForwardName; set => Set(ref _newForwardName, value); }

    private string _newForwardListen = "";
    public string NewForwardListen { get => _newForwardListen; set => Set(ref _newForwardListen, value); }

    private string _newForwardTarget = "";
    public string NewForwardTarget { get => _newForwardTarget; set => Set(ref _newForwardTarget, value); }

    private string _newMappingName = "";
    public string NewMappingName { get => _newMappingName; set => Set(ref _newMappingName, value); }

    private string _newMappingListen = "";
    public string NewMappingListen { get => _newMappingListen; set => Set(ref _newMappingListen, value); }

    private string _newMappingTargets = "";
    public string NewMappingTargets { get => _newMappingTargets; set => Set(ref _newMappingTargets, value); }

    private string _newMappingStrategy = "roundrobin";
    public string NewMappingStrategy { get => _newMappingStrategy; set => Set(ref _newMappingStrategy, value); }

    private bool _newMappingHealthCheck = true;
    public bool NewMappingHealthCheck { get => _newMappingHealthCheck; set => Set(ref _newMappingHealthCheck, value); }

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
            ExePath = snap.ExePath;
            ExeName = string.IsNullOrEmpty(snap.ExePath) ? "未找到 orsend.exe" : Path.GetFileName(snap.ExePath);

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
                StateText = string.IsNullOrEmpty(snap.LastError) ? "引擎未运行" : "启动失败";
                StateBrush = new SolidColorBrush(Color.FromRgb(0x6B, 0x72, 0x80));
                StateWeakBrush = new SolidColorBrush(Color.FromRgb(0xF3, 0xF4, 0xF6));
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

    private void LoadSettings(ServerSettings s)
    {
        SettingBind = s.Bind;
        SettingToken = s.Token;
        SettingPublicHost = s.PublicHost;
        SettingControlPort = s.ControlPort.ToString();
        SettingLogLevel = string.IsNullOrEmpty(s.LogLevel) ? "info" : s.LogLevel;
        SettingAutoStart = s.AutoStart;
    }

    private void ApplyStatus(EngineStatus st)
    {
        VersionText = "v" + st.Version;
        StatAgents = st.Server.AgentCount.ToString();
        StatTunnels = st.Server.TunnelCount.ToString();
        StatStreams = st.Server.ActiveStreams.ToString();

        var total = st.Server.BytesIn + st.Server.BytesOut;
        StatTraffic = Fmt.Bytes(total);
        TrafficUp = Fmt.Bytes(st.Server.BytesIn);
        TrafficDown = Fmt.Bytes(st.Server.BytesOut);

        InfoBind = st.Server.Listen;
        InfoPublicHost = st.Server.PublicHost;
        InfoControlPort = st.Control.Port.ToString();
        InfoUptime = Fmt.Duration(st.UptimeSec);
        InfoToken = string.IsNullOrEmpty(SettingToken) ? "未设置（不校验）" : "已设置";

        StateText = st.Running ? "运行中" : "已停止";
        StateBrush = st.Running
            ? new SolidColorBrush(Color.FromRgb(0x16, 0xA3, 0x4A))
            : new SolidColorBrush(Color.FromRgb(0x6B, 0x72, 0x80));
        StateWeakBrush = st.Running
            ? new SolidColorBrush(Color.FromRgb(0xEC, 0xFD, 0xF3))
            : new SolidColorBrush(Color.FromRgb(0xF3, 0xF4, 0xF6));

        Sync(Agents, st.Server.Agents, a => $"{a.ClientId}|{a.UptimeSec}|{a.StreamsTotal}|{a.BytesIn}|{a.BytesOut}");
        Sync(Tunnels, st.Server.Tunnels, t => $"{t.Key}|{t.ActiveStreams}|{t.Connections}|{t.BytesIn}|{t.BytesOut}");
        Sync(ForwardRules, st.Forward, r => $"{r.Name}|{r.Active}|{r.Connections}|{r.Failed}|{r.BytesIn}|{r.BytesOut}");
        Sync(MappingRules, st.Mapping, r => $"{r.Name}|{r.Active}|{r.Connections}|{r.Failed}|{r.BytesIn}|{r.BytesOut}");

        StatusBar = st.Running
            ? $"服务端运行中 · 接入 {st.Server.Listen} · 公网 {st.Server.PublicHost} · 已运行 {Fmt.Duration(st.UptimeSec)}"
            : "引擎已启动但服务端未运行";
        StatusRight = $"控制接口 127.0.0.1:{st.Control.Port}";
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
    private async Task StartAsync()
    {
        StatusBar = "正在启动引擎…";
        var ok = await _runtime.StartAsync();
        StatusBar = ok ? "引擎已启动" : "引擎启动失败：" + (_runtime.RecentOutput.Length > 0 ? "请查看日志" : "未找到 orsend.exe");
        await RefreshAsync();
    }

    private async Task StopAsync()
    {
        await _runtime.StopAsync();
        StatusBar = "引擎已停止";
        await RefreshAsync();
    }

    private async Task KickAgentAsync(AgentInfo? agent)
    {
        if (agent is null || _runtime.Client is null) return;
        try
        {
            await _runtime.Client.CallAsync("server.kick", new { clientId = agent.ClientId });
            StatusBar = $"已断开客户端 {agent.ClientId}";
            await RefreshAsync();
        }
        catch (Exception ex)
        {
            StatusBar = "踢下线失败: " + ex.Message;
        }
    }

    private async Task AddForwardAsync()
    {
        if (_runtime.Client is null) return;
        var listen = NewForwardListen.Trim();
        var target = NewForwardTarget.Trim();
        if (listen.Length == 0 || target.Length == 0)
        {
            StatusBar = "请填写监听地址和目标地址，例如 0.0.0.0:8080 与 127.0.0.1:80";
            return;
        }

        var name = NewForwardName.Trim();
        if (name.Length == 0) name = "forward-" + listen.Replace(':', '-').Replace('.', '-');

        try
        {
            await _runtime.Client.CallAsync("forward.add", new { name, listen, target });
            _runtime.Settings.Forward.RemoveAll(r => r.Name == name);
            _runtime.Settings.Forward.Add(new ForwardRuleSetting { Name = name, Listen = listen, Target = target });
            _runtime.SaveSettings();

            NewForwardName = "";
            NewForwardListen = "";
            NewForwardTarget = "";
            StatusBar = $"已添加转发规则 {name}";
            await RefreshAsync();
        }
        catch (Exception ex)
        {
            StatusBar = "添加失败: " + ex.Message;
        }
    }

    private async Task RemoveForwardAsync(RuleInfo? rule)
    {
        if (rule is null || _runtime.Client is null) return;
        try
        {
            await _runtime.Client.CallAsync("forward.remove", new { name = rule.Name });
            _runtime.Settings.Forward.RemoveAll(r => r.Name == rule.Name);
            _runtime.SaveSettings();
            StatusBar = $"已删除转发规则 {rule.Name}";
            await RefreshAsync();
        }
        catch (Exception ex)
        {
            StatusBar = "删除失败: " + ex.Message;
        }
    }

    private async Task AddMappingAsync()
    {
        if (_runtime.Client is null) return;
        var listen = NewMappingListen.Trim();
        var targets = NewMappingTargets
            .Split(new[] { ',', ';', ' ' }, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .ToList();

        if (listen.Length == 0 || targets.Count == 0)
        {
            StatusBar = "请填写监听地址和目标列表，目标之间用逗号分隔";
            return;
        }

        var name = NewMappingName.Trim();
        if (name.Length == 0) name = "mapping-" + listen.Replace(':', '-').Replace('.', '-');
        var strategy = string.IsNullOrWhiteSpace(NewMappingStrategy) ? "roundrobin" : NewMappingStrategy;

        try
        {
            await _runtime.Client.CallAsync("mapping.add", new
            {
                name,
                listen,
                targets,
                strategy,
                healthCheck = NewMappingHealthCheck
            });

            _runtime.Settings.Mapping.RemoveAll(r => r.Name == name);
            _runtime.Settings.Mapping.Add(new MappingRuleSetting
            {
                Name = name,
                Listen = listen,
                Targets = targets,
                Strategy = strategy,
                HealthCheck = NewMappingHealthCheck
            });
            _runtime.SaveSettings();

            NewMappingName = "";
            NewMappingListen = "";
            NewMappingTargets = "";
            StatusBar = $"已添加映射规则 {name}";
            await RefreshAsync();
        }
        catch (Exception ex)
        {
            StatusBar = "添加失败: " + ex.Message;
        }
    }

    private async Task RemoveMappingAsync(RuleInfo? rule)
    {
        if (rule is null || _runtime.Client is null) return;
        try
        {
            await _runtime.Client.CallAsync("mapping.remove", new { name = rule.Name });
            _runtime.Settings.Mapping.RemoveAll(r => r.Name == rule.Name);
            _runtime.SaveSettings();
            StatusBar = $"已删除映射规则 {rule.Name}";
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

        var settings = new ServerSettings
        {
            Bind = SettingBind.Trim(),
            Token = SettingToken,
            PublicHost = SettingPublicHost.Trim(),
            ControlPort = port,
            LogLevel = string.IsNullOrWhiteSpace(SettingLogLevel) ? "info" : SettingLogLevel,
            AutoStart = SettingAutoStart,
            Forward = _runtime.Settings.Forward,
            Mapping = _runtime.Settings.Mapping
        };

        SettingsHint = "正在重启引擎…";
        var ok = await _runtime.RestartAsync(settings);
        SettingsHint = ok ? "已保存并重启" : "重启失败，请查看引擎输出";
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
