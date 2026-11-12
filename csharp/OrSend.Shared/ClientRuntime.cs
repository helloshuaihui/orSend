namespace OrSend.Shared;

/// <summary>客户端快照：引擎状态 + 配置 + 进程输出。</summary>
public sealed record ClientSnapshot(
    bool EngineOnline,
    string LastError,
    string RecentOutput,
    string ExePath,
    ClientSettings Settings,
    EngineStatus? Status);

/// <summary>
/// 客户端运行时：托管 agent 进程、维护隧道列表、把配置同步给引擎。
/// 桌面端和网页端共用，不依赖 ASP.NET Core。
/// </summary>
public sealed class ClientRuntime : IAsyncDisposable
{
    private readonly JsonFileStore<ClientSettings> _store;
    private readonly EngineOptions _engineOptions;
    private readonly Action<string>? _log;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly object _logSync = new();
    private readonly Queue<string> _engineLog = new();

    private ClientSettings _settings;
    private EngineHost? _engine;

    public ClientRuntime(JsonFileStore<ClientSettings> store, EngineOptions engineOptions,
        Action<string>? log = null)
    {
        _store = store;
        _engineOptions = engineOptions;
        _log = log;
        _settings = store.Load();

        if (string.IsNullOrWhiteSpace(_settings.ClientId))
        {
            _settings.ClientId = "agent-" + Environment.MachineName.ToLowerInvariant();
        }
        if (string.IsNullOrWhiteSpace(_settings.Name))
        {
            _settings.Name = Environment.MachineName;
        }
    }

    public ClientSettings Settings => _settings;
    public EngineClient? Client => _engine?.Client;
    public bool EngineOnline => _engine is not null;

    public string RecentOutput
    {
        get { lock (_logSync) return string.Join("\n", _engineLog); }
    }

    public async Task InitializeAsync(CancellationToken ct = default)
    {
        if (_settings.AutoConnect) await StartAsync(ct).ConfigureAwait(false);
    }

    public async Task<bool> StartAsync(CancellationToken ct = default)
    {
        await _gate.WaitAsync(ct).ConfigureAwait(false);
        try
        {
            return await StartInternalAsync(ct).ConfigureAwait(false);
        }
        finally { _gate.Release(); }
    }

    private async Task<bool> StartInternalAsync(CancellationToken ct)
    {
        if (_engine is not null)
        {
            await _engine.DisposeAsync().ConfigureAwait(false);
            _engine = null;
        }

        _engine = new EngineHost(new EngineOptions
        {
            ExePath = _engineOptions.ExePath,
            ControlHost = _engineOptions.ControlHost,
            ControlPort = _settings.ControlPort,
            LogLevel = _settings.LogLevel
        }, AppendEngineLog);

        var args = new List<string>
        {
            "client",
            "--server", _settings.Server,
            "--token", _settings.Token,
            "--client-id", _settings.ClientId,
            "--name", _settings.Name,
            "--control-port", _settings.ControlPort.ToString(),
            "--log-level", _settings.LogLevel
        };

        foreach (var t in _settings.Tunnels)
        {
            var spec = string.IsNullOrWhiteSpace(t.LocalHost) || t.LocalHost == "127.0.0.1"
                ? $"{t.Name}:{t.LocalPort}:{t.RemotePort}"
                : $"{t.Name}:{t.LocalHost}:{t.LocalPort}:{t.RemotePort}";
            args.Add("--tunnel");
            args.Add(spec);
        }

        var ok = await _engine.StartAsync(args, ct).ConfigureAwait(false);
        if (!ok) _log?.Invoke("引擎启动失败: " + _engine.LastError);
        return ok;
    }

    public async Task StopAsync()
    {
        await _gate.WaitAsync().ConfigureAwait(false);
        try
        {
            if (_engine is not null)
            {
                await _engine.DisposeAsync().ConfigureAwait(false);
                _engine = null;
            }
        }
        finally { _gate.Release(); }
    }

    public async Task<bool> RestartAsync(ClientSettings settings, CancellationToken ct = default)
    {
        await _gate.WaitAsync(ct).ConfigureAwait(false);
        try
        {
            if (_engine is not null)
            {
                await _engine.DisposeAsync().ConfigureAwait(false);
                _engine = null;
            }
            _settings = settings;
            _store.Save(_settings);
            return await StartInternalAsync(ct).ConfigureAwait(false);
        }
        finally { _gate.Release(); }
    }

    public void SaveSettings() => _store.Save(_settings);

    public async Task<ClientSnapshot> GetSnapshotAsync(CancellationToken ct = default)
    {
        EngineStatus? status = null;
        if (_engine is not null)
        {
            try
            {
                status = await _engine.Client.CallAsync<EngineStatus>("status", null, ct).ConfigureAwait(false);
            }
            catch { status = null; }
        }

        return new ClientSnapshot(
            status is not null && status.Running,
            _engine?.LastError ?? "",
            RecentOutput,
            _engine?.ResolvedExe ?? "",
            _settings,
            status);
    }

    private void AppendEngineLog(string line)
    {
        lock (_logSync)
        {
            _engineLog.Enqueue(line);
            while (_engineLog.Count > 300) _engineLog.Dequeue();
        }
        _log?.Invoke(line);
    }

    public async ValueTask DisposeAsync()
    {
        await StopAsync().ConfigureAwait(false);
        _gate.Dispose();
    }
}
