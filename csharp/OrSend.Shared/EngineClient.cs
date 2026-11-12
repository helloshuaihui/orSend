using System.Net.Sockets;
using System.Text;
using System.Text.Json;

namespace OrSend.Shared;

/// <summary>
/// 与 C++ 引擎的本地控制接口通信（127.0.0.1 上「一行一个 JSON」的 RPC）。
/// 内部复用一条长连接，请求串行化。
/// </summary>
public sealed class EngineClient : IAsyncDisposable
{
    public static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        PropertyNameCaseInsensitive = true,
        WriteIndented = false
    };

    private readonly string _host;
    private readonly int _port;
    private readonly SemaphoreSlim _lock = new(1, 1);
    private TcpClient? _tcp;
    private StreamReader? _reader;
    private StreamWriter? _writer;
    private long _nextId;

    public EngineClient(string host, int port)
    {
        _host = host;
        _port = port;
    }

    public string Endpoint => $"{_host}:{_port}";

    public async Task<JsonElement> CallAsync(string cmd, object? args = null, CancellationToken ct = default)
    {
        await _lock.WaitAsync(ct).ConfigureAwait(false);
        try
        {
            try
            {
                return await CallOnceAsync(cmd, args, ct).ConfigureAwait(false);
            }
            catch (IOException)
            {
                // 连接可能已被对端关闭，重连后重试一次
                CloseConnection();
                return await CallOnceAsync(cmd, args, ct).ConfigureAwait(false);
            }
            catch (SocketException)
            {
                CloseConnection();
                return await CallOnceAsync(cmd, args, ct).ConfigureAwait(false);
            }
        }
        finally
        {
            _lock.Release();
        }
    }

    private async Task<JsonElement> CallOnceAsync(string cmd, object? args, CancellationToken ct)
    {
        EnsureConnected();

        var id = Interlocked.Increment(ref _nextId);
        var request = JsonSerializer.Serialize(new { id, cmd, args = args ?? new { } }, JsonOptions);
        await _writer!.WriteLineAsync(request).ConfigureAwait(false);
        await _writer.FlushAsync().ConfigureAwait(false);

        using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(ct);
        timeoutCts.CancelAfter(TimeSpan.FromSeconds(15));
        var line = await _reader!.ReadLineAsync(timeoutCts.Token).ConfigureAwait(false);
        if (line is null)
        {
            CloseConnection();
            throw new IOException("控制接口连接已断开");
        }

        using var doc = JsonDocument.Parse(line);
        var root = doc.RootElement;
        var ok = root.TryGetProperty("ok", out var okProp) && okProp.ValueKind == JsonValueKind.True;
        if (!ok)
        {
            var err = root.TryGetProperty("error", out var e) ? e.GetString() : "未知错误";
            throw new EngineException(err ?? "未知错误");
        }

        return root.TryGetProperty("data", out var data) ? data.Clone() : default;
    }

    public async Task<T?> CallAsync<T>(string cmd, object? args = null, CancellationToken ct = default)
    {
        var element = await CallAsync(cmd, args, ct).ConfigureAwait(false);
        if (element.ValueKind is JsonValueKind.Undefined or JsonValueKind.Null) return default;
        return element.Deserialize<T>(JsonOptions);
    }

    public async Task<bool> PingAsync(CancellationToken ct = default)
    {
        try
        {
            await CallAsync("ping", null, ct).ConfigureAwait(false);
            return true;
        }
        catch
        {
            return false;
        }
    }

    private void EnsureConnected()
    {
        if (_tcp is { Connected: true } && _reader is not null && _writer is not null) return;
        CloseConnection();

        var tcp = new TcpClient { NoDelay = true };
        tcp.Connect(_host, _port);
        _tcp = tcp;
        var stream = tcp.GetStream();
        _reader = new StreamReader(stream, new UTF8Encoding(false));
        _writer = new StreamWriter(stream, new UTF8Encoding(false)) { AutoFlush = false, NewLine = "\n" };
    }

    private void CloseConnection()
    {
        try { _reader?.Dispose(); } catch { /* ignore */ }
        try { _writer?.Dispose(); } catch { /* ignore */ }
        try { _tcp?.Close(); } catch { /* ignore */ }
        _reader = null;
        _writer = null;
        _tcp = null;
    }

    public ValueTask DisposeAsync()
    {
        CloseConnection();
        _lock.Dispose();
        return ValueTask.CompletedTask;
    }
}
