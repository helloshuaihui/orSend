namespace OrSend.Shared;

/// <summary>服务端快照：引擎状态 + 配置 + 进程输出。</summary>
public sealed record ServerSnapshot(
    bool EngineOnline,
    string LastError,
    string RecentOutput,
    string ExePath,
    ServerSettings Settings,
    EngineStatus? Status);

/// <summary>
/// 服务端运行时：持有配置、托管 orsend 引擎进程、把配置同步到引擎。
/// 桌面端和网页端共用，不依赖 ASP.NET Core。
/// </summary>
public sealed class ServerRuntime : IAsyncDisposable
{
    private readonly JsonFileStore<ServerSettings> _store;
    private readonly EngineOptions _engineOptions;
    private readonly Action<string>? _log;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly object _logSync = new();
    private readonly Queue<string> _engineLog = new();

    private ServerSettings _settings;
    private EngineHost? _engine;

    public ServerRuntime(JsonFileStore<ServerSettings> store, EngineOptions engineOptions,
        Action<string>? log = null)
    {
        _store = store;
        _engineOptions = engineOptions;
        _log = log;
        _settings = store.Load();
    }

    public ServerSettings Settings => _settings;

    public EngineClient? Client => _engine?.Client;

    public bool EngineOnline => _engine is not null;

    /// <summary>引擎进程被托管时返回其输出（最多 300 行）。</summary>
    public string RecentOutput
    {
        get { lock (_logSync) return string.Join("\n", _engineLog); }
    }

    public async Task InitializeAsync(CancellationToken ct = default)
    {
        if (_settings.AutoStart)
        {
            await StartAsync(ct).ConfigureAwait(false);
        }
    }

    public async Task<bool> StartAsync(CancellationToken ct = default)
    {
        await _gate.WaitAsync(ct).ConfigureAwait(false);
        try
        {
            return await StartInternalAsync(ct).ConfigureAwait(false);
        }
        finally
        {
            _gate.Release();
        }
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
            "server",
            "--bind", _settings.Bind,
            "--token", _settings.Token,
            "--public-host", _settings.PublicHost,
            "--control-port", _settings.ControlPort.ToString(),
            "--log-level", _settings.LogLevel
        };

        var ok = await _engine.StartAsync(args, ct).ConfigureAwait(false);
        if (!ok)
        {
            _log?.Invoke("引擎启动失败: " + _engine.LastError);
            return false;
        }

        await ApplyRulesAsync(ct).ConfigureAwait(false);
        return true;
    }

    /// <summary>把持久化的转发 / 映射规则重新灌进引擎（引擎重启后调用）。</summary>
    private async Task ApplyRulesAsync(CancellationToken ct)
    {
        if (_engine is null) return;
        var client = _engine.Client;

        foreach (var rule in _settings.Forward)
        {
            try
            {
                await client.CallAsync("forward.add", new
                {
                    name = rule.Name,
                    listen = rule.Listen,
                    target = rule.Target
                }, ct).ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                _log?.Invoke($"恢复转发规则 {rule.Name} 失败: {ex.Message}");
            }
        }

        foreach (var rule in _settings.Mapping)
        {
            try
            {
                await client.CallAsync("mapping.add", new
                {
                    name = rule.Name,
                    listen = rule.Listen,
                    targets = rule.Targets,
                    strategy = rule.Strategy,
                    healthCheck = rule.HealthCheck
                }, ct).ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                _log?.Invoke($"恢复映射规则 {rule.Name} 失败: {ex.Message}");
            }
        }
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
        finally
        {
            _gate.Release();
        }
    }

    public async Task<bool> RestartAsync(ServerSettings settings, CancellationToken ct = default)
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
        finally
        {
            _gate.Release();
        }
    }

    public void SaveSettings()
    {
        _store.Save(_settings);
    }

    public async Task<ServerSnapshot> GetSnapshotAsync(CancellationToken ct = default)
    {
        EngineStatus? status = null;
        if (_engine is not null)
        {
            try
            {
                status = await _engine.Client.CallAsync<EngineStatus>("status", null, ct).ConfigureAwait(false);
            }
            catch
            {
                status = null;
            }
        }

        return new ServerSnapshot(
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
