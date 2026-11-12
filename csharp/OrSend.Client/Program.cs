using OrSend.Shared;

var builder = WebApplication.CreateBuilder(args);

builder.WebHost.UseUrls(builder.Configuration["App:Urls"] ?? "http://127.0.0.1:5081");

var contentRoot = builder.Environment.ContentRootPath;
var dataDir = Path.Combine(contentRoot, "App_Data");
Directory.CreateDirectory(dataDir);

var store = new JsonFileStore<ClientSettings>(Path.Combine(dataDir, "client-settings.json"));
var engineOptions = new EngineOptions
{
    ExePath = builder.Configuration["Engine:ExePath"] ?? "",
    ControlHost = builder.Configuration["Engine:ControlHost"] ?? "127.0.0.1"
};

builder.Services.AddSingleton(store);
builder.Services.AddSingleton(engineOptions);
builder.Services.AddSingleton(new ClientRuntime(store, engineOptions));

var app = builder.Build();

app.UseDefaultFiles();
app.UseStaticFiles();

var runtime = app.Services.GetRequiredService<ClientRuntime>();
await runtime.InitializeAsync();

app.MapGet("/", () => Results.Redirect("/index.html"));

app.MapGet("/api/status", async (ClientRuntime rt, CancellationToken ct) => await rt.GetSnapshotAsync(ct));

app.MapGet("/api/health", () => Results.Ok(new { ok = true }));

app.MapPost("/api/client/start", async (ConnectRequest req, ClientRuntime rt, CancellationToken ct) =>
{
    if (!string.IsNullOrWhiteSpace(req.Server)) rt.Settings.Server = req.Server!;
    if (req.Token is not null) rt.Settings.Token = req.Token;
    if (!string.IsNullOrWhiteSpace(req.ClientId)) rt.Settings.ClientId = req.ClientId!;
    if (!string.IsNullOrWhiteSpace(req.Name)) rt.Settings.Name = req.Name!;
    if (req.ControlPort is > 0) rt.Settings.ControlPort = req.ControlPort!.Value;

    var ok = await rt.RestartAsync(rt.Settings, ct);
    return ok
        ? Results.Ok(await rt.GetSnapshotAsync(ct))
        : Results.Json(new { error = "引擎启动失败，请查看日志" }, statusCode: 500);
});

app.MapPost("/api/client/stop", async (ClientRuntime rt) =>
{
    await rt.StopAsync();
    return Results.Ok(await rt.GetSnapshotAsync());
});

app.MapPost("/api/tunnel/add", async (TunnelRequest req, ClientRuntime rt, CancellationToken ct) =>
{
    if (string.IsNullOrWhiteSpace(req.Name)) return Results.BadRequest(new { error = "隧道名不能为空" });
    if (req.LocalPort is null or <= 0 or > 65535) return Results.BadRequest(new { error = "本地端口无效" });
    if (req.RemotePort is null or < 0 or > 65535) return Results.BadRequest(new { error = "公网端口无效" });

    var name = req.Name!.Trim();
    if (rt.Settings.Tunnels.Any(t => t.Name == name))
        return Results.BadRequest(new { error = $"隧道 {name} 已存在" });

    var setting = new TunnelSetting
    {
        Name = name,
        LocalHost = string.IsNullOrWhiteSpace(req.LocalHost) ? "127.0.0.1" : req.LocalHost!,
        LocalPort = req.LocalPort!.Value,
        RemotePort = req.RemotePort!.Value
    };

    var client = rt.Client;
    if (client is not null)
    {
        try
        {
            await client.CallAsync("client.addTunnel", new
            {
                name = setting.Name,
                localHost = setting.LocalHost,
                localPort = setting.LocalPort,
                remotePort = setting.RemotePort
            }, ct);
        }
        catch (EngineException ex)
        {
            return Results.BadRequest(new { error = ex.Message });
        }
    }

    rt.Settings.Tunnels.Add(setting);
    rt.SaveSettings();
    return Results.Ok(await rt.GetSnapshotAsync(ct));
});

app.MapPost("/api/tunnel/remove", async (RemoveRequest req, ClientRuntime rt, CancellationToken ct) =>
{
    var client = rt.Client;
    if (client is not null)
    {
        try
        {
            await client.CallAsync("client.removeTunnel", new { name = req.Name }, ct);
        }
        catch (EngineException)
        {
            // 引擎里可能已经没有这条隧道（例如未连接），继续清理本地配置
        }
    }

    rt.Settings.Tunnels.RemoveAll(t => t.Name == req.Name);
    rt.SaveSettings();
    return Results.Ok(await rt.GetSnapshotAsync(ct));
});

app.MapGet("/api/logs", async (ClientRuntime rt, long sinceId = 0, int limit = 200, CancellationToken ct = default) =>
{
    var client = rt.Client;
    if (client is null) return Results.Json(new { error = "引擎未运行" }, statusCode: 409);
    try
    {
        return Results.Ok(await client.CallAsync<LogsResult>("logs", new { sinceId, limit }, ct));
    }
    catch (EngineException ex)
    {
        return Results.BadRequest(new { error = ex.Message });
    }
});

app.MapPost("/api/settings", async (SettingsRequest req, ClientRuntime rt, CancellationToken ct) =>
{
    if (!string.IsNullOrWhiteSpace(req.Server)) rt.Settings.Server = req.Server!;
    if (req.Token is not null) rt.Settings.Token = req.Token;
    if (!string.IsNullOrWhiteSpace(req.ClientId)) rt.Settings.ClientId = req.ClientId!;
    if (!string.IsNullOrWhiteSpace(req.Name)) rt.Settings.Name = req.Name!;
    if (req.ControlPort is > 0) rt.Settings.ControlPort = req.ControlPort!.Value;
    if (req.AutoConnect.HasValue) rt.Settings.AutoConnect = req.AutoConnect.Value;
    if (!string.IsNullOrWhiteSpace(req.LogLevel)) rt.Settings.LogLevel = req.LogLevel!;

    var ok = await rt.RestartAsync(rt.Settings, ct);
    return ok
        ? Results.Ok(await rt.GetSnapshotAsync(ct))
        : Results.Json(new { error = "引擎重启失败，请查看日志" }, statusCode: 500);
});

app.Run();

public sealed record ConnectRequest(string? Server, string? Token, string? ClientId, string? Name, int? ControlPort);
public sealed record TunnelRequest(string? Name, string? LocalHost, int? LocalPort, int? RemotePort);
public sealed record RemoveRequest(string Name);
public sealed record SettingsRequest(string? Server, string? Token, string? ClientId, string? Name,
    int? ControlPort, bool? AutoConnect, string? LogLevel);
