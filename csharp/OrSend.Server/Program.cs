using OrSend.Shared;

var builder = WebApplication.CreateBuilder(args);

builder.WebHost.UseUrls(builder.Configuration["App:Urls"] ?? "http://127.0.0.1:5080");

var contentRoot = builder.Environment.ContentRootPath;
var dataDir = Path.Combine(contentRoot, "App_Data");
Directory.CreateDirectory(dataDir);

var store = new JsonFileStore<ServerSettings>(Path.Combine(dataDir, "server-settings.json"));
var engineOptions = new EngineOptions
{
    ExePath = builder.Configuration["Engine:ExePath"] ?? "",
    ControlHost = builder.Configuration["Engine:ControlHost"] ?? "127.0.0.1"
};

builder.Services.AddSingleton(store);
builder.Services.AddSingleton(engineOptions);
builder.Services.AddSingleton(new ServerRuntime(store, engineOptions));

var app = builder.Build();

app.UseDefaultFiles();
app.UseStaticFiles();

var runtime = app.Services.GetRequiredService<ServerRuntime>();
await runtime.InitializeAsync();

app.MapGet("/", () => Results.Redirect("/index.html"));

app.MapGet("/api/status", async (ServerRuntime rt, CancellationToken ct) => await rt.GetSnapshotAsync(ct));

app.MapGet("/api/health", () => Results.Ok(new { ok = true, time = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() }));

app.MapPost("/api/server/start", async (ServerStartRequest req, ServerRuntime rt, CancellationToken ct) =>
{
    if (!string.IsNullOrWhiteSpace(req.Bind)) rt.Settings.Bind = req.Bind!;
    if (req.Token is not null) rt.Settings.Token = req.Token;
    if (!string.IsNullOrWhiteSpace(req.PublicHost)) rt.Settings.PublicHost = req.PublicHost!;
    if (req.ControlPort is > 0) rt.Settings.ControlPort = req.ControlPort!.Value;

    var ok = await rt.RestartAsync(rt.Settings, ct);
    return ok
        ? Results.Ok(await rt.GetSnapshotAsync(ct))
        : Results.Json(new { error = "引擎启动失败，请查看日志" }, statusCode: 500);
});

app.MapPost("/api/server/stop", async (ServerRuntime rt) =>
{
    await rt.StopAsync();
    return Results.Ok(await rt.GetSnapshotAsync());
});

app.MapPost("/api/server/kick", async (KickRequest req, ServerRuntime rt, CancellationToken ct) =>
{
    var client = RequireClient(rt);
    if (client is null) return EngineOffline();
    try
    {
        await client.CallAsync("server.kick", new { clientId = req.ClientId }, ct);
        return Results.Ok(await rt.GetSnapshotAsync(ct));
    }
    catch (EngineException ex)
    {
        return Results.BadRequest(new { error = ex.Message });
    }
});

app.MapPost("/api/forward/add", async (ForwardRequest req, ServerRuntime rt, CancellationToken ct) =>
{
    var client = RequireClient(rt);
    if (client is null) return EngineOffline();
    if (string.IsNullOrWhiteSpace(req.Listen) || string.IsNullOrWhiteSpace(req.Target))
        return Results.BadRequest(new { error = "监听地址与目标地址不能为空" });

    var name = string.IsNullOrWhiteSpace(req.Name) ? $"forward-{req.Listen}" : req.Name!;
    try
    {
        await client.CallAsync("forward.add", new { name, listen = req.Listen, target = req.Target }, ct);
        rt.Settings.Forward.RemoveAll(r => r.Name == name);
        rt.Settings.Forward.Add(new ForwardRuleSetting { Name = name, Listen = req.Listen!, Target = req.Target! });
        rt.SaveSettings();
        return Results.Ok(await rt.GetSnapshotAsync(ct));
    }
    catch (EngineException ex)
    {
        return Results.BadRequest(new { error = ex.Message });
    }
});

app.MapPost("/api/forward/remove", async (RemoveRequest req, ServerRuntime rt, CancellationToken ct) =>
{
    var client = RequireClient(rt);
    if (client is null) return EngineOffline();
    try
    {
        await client.CallAsync("forward.remove", new { name = req.Name }, ct);
    }
    catch (EngineException ex)
    {
        return Results.BadRequest(new { error = ex.Message });
    }
    rt.Settings.Forward.RemoveAll(r => r.Name == req.Name);
    rt.SaveSettings();
    return Results.Ok(await rt.GetSnapshotAsync(ct));
});

app.MapPost("/api/mapping/add", async (MappingRequest req, ServerRuntime rt, CancellationToken ct) =>
{
    var client = RequireClient(rt);
    if (client is null) return EngineOffline();
    if (string.IsNullOrWhiteSpace(req.Listen) || req.Targets is null || req.Targets.Count == 0)
        return Results.BadRequest(new { error = "监听地址与目标列表不能为空" });

    var name = string.IsNullOrWhiteSpace(req.Name) ? $"mapping-{req.Listen}" : req.Name!;
    var strategy = string.IsNullOrWhiteSpace(req.Strategy) ? "roundrobin" : req.Strategy!;
    try
    {
        await client.CallAsync("mapping.add", new
        {
            name,
            listen = req.Listen,
            targets = req.Targets,
            strategy,
            healthCheck = req.HealthCheck ?? true
        }, ct);

        rt.Settings.Mapping.RemoveAll(r => r.Name == name);
        rt.Settings.Mapping.Add(new MappingRuleSetting
        {
            Name = name,
            Listen = req.Listen!,
            Targets = req.Targets,
            Strategy = strategy,
            HealthCheck = req.HealthCheck ?? true
        });
        rt.SaveSettings();
        return Results.Ok(await rt.GetSnapshotAsync(ct));
    }
    catch (EngineException ex)
    {
        return Results.BadRequest(new { error = ex.Message });
    }
});

app.MapPost("/api/mapping/remove", async (RemoveRequest req, ServerRuntime rt, CancellationToken ct) =>
{
    var client = RequireClient(rt);
    if (client is null) return EngineOffline();
    try
    {
        await client.CallAsync("mapping.remove", new { name = req.Name }, ct);
    }
    catch (EngineException ex)
    {
        return Results.BadRequest(new { error = ex.Message });
    }
    rt.Settings.Mapping.RemoveAll(r => r.Name == req.Name);
    rt.SaveSettings();
    return Results.Ok(await rt.GetSnapshotAsync(ct));
});

app.MapGet("/api/logs", async (ServerRuntime rt, long sinceId = 0, int limit = 200, CancellationToken ct = default) =>
{
    var client = RequireClient(rt);
    if (client is null) return EngineOffline();
    try
    {
        var logs = await client.CallAsync<LogsResult>("logs", new { sinceId, limit }, ct);
        return Results.Ok(logs);
    }
    catch (EngineException ex)
    {
        return Results.BadRequest(new { error = ex.Message });
    }
});

app.MapPost("/api/settings", async (SettingsRequest req, ServerRuntime rt, CancellationToken ct) =>
{
    if (!string.IsNullOrWhiteSpace(req.Bind)) rt.Settings.Bind = req.Bind!;
    if (req.Token is not null) rt.Settings.Token = req.Token;
    if (!string.IsNullOrWhiteSpace(req.PublicHost)) rt.Settings.PublicHost = req.PublicHost!;
    if (req.ControlPort is > 0) rt.Settings.ControlPort = req.ControlPort!.Value;
    if (req.AutoStart.HasValue) rt.Settings.AutoStart = req.AutoStart.Value;
    if (!string.IsNullOrWhiteSpace(req.LogLevel)) rt.Settings.LogLevel = req.LogLevel!;

    var ok = await rt.RestartAsync(rt.Settings, ct);
    return ok
        ? Results.Ok(await rt.GetSnapshotAsync(ct))
        : Results.Json(new { error = "引擎重启失败，请查看日志" }, statusCode: 500);
});

app.Run();

static EngineClient? RequireClient(ServerRuntime rt)
{
    var client = rt.Client;
    return client;
}

static IResult EngineOffline() =>
    Results.Json(new { error = "引擎未运行，请先启动服务端" }, statusCode: 409);

public sealed record ServerStartRequest(string? Bind, string? Token, string? PublicHost, int? ControlPort);
public sealed record KickRequest(string ClientId);
public sealed record ForwardRequest(string? Name, string? Listen, string? Target);
public sealed record RemoveRequest(string Name);
public sealed record MappingRequest(string? Name, string? Listen, List<string>? Targets, string? Strategy, bool? HealthCheck);
public sealed record SettingsRequest(string? Bind, string? Token, string? PublicHost, int? ControlPort, bool? AutoStart, string? LogLevel);
