using System.Diagnostics;
using System.Text;

namespace OrSend.Shared;

public sealed class EngineOptions
{
    public string ExePath { get; set; } = "";
    public string ControlHost { get; set; } = "127.0.0.1";
    public int ControlPort { get; set; } = 7401;
    public string WorkingDirectory { get; set; } = "";
    public string LogLevel { get; set; } = "info";
}

/// <summary>
/// 托管 C++ 引擎进程（orsend）。页面本身不实现转发逻辑，
/// 只通过控制接口驱动引擎，这样命令行和页面共用同一套核心。
/// </summary>
public sealed class EngineHost : IAsyncDisposable
{
    private readonly EngineOptions _options;
    private readonly Action<string>? _log;
    private readonly object _sync = new();
    private readonly Queue<string> _recentOutput = new();
    private Process? _process;
    private bool _attached;
    private string _lastError = "";

    public EngineHost(EngineOptions options, Action<string>? log = null)
    {
        _options = options;
        _log = log;
        Client = new EngineClient(options.ControlHost, options.ControlPort);
    }

    public EngineClient Client { get; }
    public bool ProcessRunning
    {
        get
        {
            lock (_sync) return _process is { HasExited: false };
        }
    }

    public bool AttachedToExternal => _attached;
    public string LastError => _lastError;
    public string ResolvedExe => ResolveExe();

    /// <summary>最近若干行引擎输出，便于页面排查启动失败。</summary>
    public string RecentOutput
    {
        get
        {
            lock (_sync) return string.Join(Environment.NewLine, _recentOutput);
        }
    }

    /// <summary>启动引擎；若控制端口上已有引擎在跑，则直接复用（attach）。</summary>
    public async Task<bool> StartAsync(IEnumerable<string> args, CancellationToken ct = default)
    {
        await StopAsync().ConfigureAwait(false);

        if (await Client.PingAsync(ct).ConfigureAwait(false))
        {
            _attached = true;
            _log?.Invoke($"检测到已有引擎在 {_options.ControlHost}:{_options.ControlPort} 运行，直接复用");
            return true;
        }
        _attached = false;

        var exe = ResolveExe();
        if (string.IsNullOrEmpty(exe) || !File.Exists(exe))
        {
            _lastError = $"未找到 orsend 可执行文件（已尝试 {exe ?? "自动搜索"}），请先执行 build.ps1 构建 C++ 引擎";
            _log?.Invoke(_lastError);
            return false;
        }

        var psi = new ProcessStartInfo
        {
            FileName = exe,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8,
            WorkingDirectory = string.IsNullOrEmpty(_options.WorkingDirectory)
                ? Path.GetDirectoryName(exe) ?? Environment.CurrentDirectory
                : _options.WorkingDirectory
        };
        foreach (var a in args) psi.ArgumentList.Add(a);

        try
        {
            var proc = new Process { StartInfo = psi, EnableRaisingEvents = true };
            proc.OutputDataReceived += (_, e) => AppendOutput(e.Data);
            proc.ErrorDataReceived += (_, e) => AppendOutput(e.Data);

            lock (_sync)
            {
                _process = proc;
                _recentOutput.Clear();
            }

            if (!proc.Start())
            {
                _lastError = "引擎进程启动失败";
                return false;
            }
            proc.BeginOutputReadLine();
            proc.BeginErrorReadLine();
            _log?.Invoke($"引擎已启动: {exe} {string.Join(' ', psi.ArgumentList)}");

            // 等待控制接口就绪
            var deadline = DateTime.UtcNow.AddSeconds(12);
            while (DateTime.UtcNow < deadline)
            {
                ct.ThrowIfCancellationRequested();
                if (proc.HasExited)
                {
                    _lastError = $"引擎进程已退出（退出码 {proc.ExitCode}）：{RecentOutput}";
                    _log?.Invoke(_lastError);
                    return false;
                }
                if (await Client.PingAsync(ct).ConfigureAwait(false))
                {
                    _lastError = "";
                    return true;
                }
                await Task.Delay(200, ct).ConfigureAwait(false);
            }

            _lastError = "等待引擎控制接口就绪超时";
            _log?.Invoke(_lastError);
            return false;
        }
        catch (Exception ex)
        {
            _lastError = "启动引擎失败: " + ex.Message;
            _log?.Invoke(_lastError);
            return false;
        }
    }

    public async Task StopAsync()
    {
        Process? proc;
        lock (_sync) proc = _process;

        if (proc is null)
        {
            _attached = false;
            return;
        }

        try
        {
            if (!proc.HasExited)
            {
                try
                {
                    await Client.CallAsync("shutdown", null, new CancellationTokenSource(1500).Token)
                        .ConfigureAwait(false);
                }
                catch { /* 引擎可能已不可用，直接杀进程 */ }

                if (!proc.WaitForExit(2500)) proc.Kill(entireProcessTree: true);
            }
        }
        catch { /* ignore */ }
        finally
        {
            try { proc.Dispose(); } catch { /* ignore */ }
            lock (_sync)
            {
                if (ReferenceEquals(_process, proc)) _process = null;
            }
        }
    }

    private void AppendOutput(string? line)
    {
        if (string.IsNullOrWhiteSpace(line)) return;
        lock (_sync)
        {
            _recentOutput.Enqueue(line);
            while (_recentOutput.Count > 200) _recentOutput.Dequeue();
        }
        _log?.Invoke(line);
    }

    /// <summary>按「配置目录 → 向上若干级 → PATH」的顺序查找引擎可执行文件。</summary>
    public string ResolveExe()
    {
        if (!string.IsNullOrWhiteSpace(_options.ExePath) && File.Exists(_options.ExePath))
            return Path.GetFullPath(_options.ExePath);

        var names = OperatingSystem.IsWindows()
            ? new[] { "orsend.exe" }
            : new[] { "orsend" };

        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (int i = 0; i < 8 && dir is not null; i++, dir = dir.Parent)
        {
            foreach (var name in names)
            {
                var candidates = new[]
                {
                    Path.Combine(dir.FullName, name),
                    Path.Combine(dir.FullName, "build", "bin", name),
                    Path.Combine(dir.FullName, "bin", name),
                };
                foreach (var c in candidates)
                {
                    if (File.Exists(c)) return Path.GetFullPath(c);
                }
            }
        }

        var pathVar = Environment.GetEnvironmentVariable("PATH") ?? "";
        foreach (var part in pathVar.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries))
        {
            foreach (var name in names)
            {
                try
                {
                    var candidate = Path.Combine(part.Trim(), name);
                    if (File.Exists(candidate)) return Path.GetFullPath(candidate);
                }
                catch { /* 非法 PATH 项忽略 */ }
            }
        }
        return "";
    }

    public async ValueTask DisposeAsync()
    {
        await StopAsync().ConfigureAwait(false);
        await Client.DisposeAsync().ConfigureAwait(false);
    }
}
