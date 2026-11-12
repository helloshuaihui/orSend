using System.Text.Json.Serialization;

namespace OrSend.Shared;

/// <summary>引擎整体状态，对应控制接口的 status 命令。</summary>
public sealed class EngineStatus
{
    public string Version { get; set; } = "";
    public long StartedAt { get; set; }
    public long UptimeSec { get; set; }
    public string ConfigPath { get; set; } = "";
    public bool Running { get; set; }
    public ControlInfo Control { get; set; } = new();
    public ServerInfo Server { get; set; } = new();
    public ClientInfo Client { get; set; } = new();
    public List<RuleInfo> Forward { get; set; } = new();
    public List<RuleInfo> Mapping { get; set; } = new();
    public TotalsInfo Totals { get; set; } = new();
}

public sealed class ControlInfo
{
    public bool Running { get; set; }
    public int Port { get; set; }
    public int Clients { get; set; }
}

public sealed class TotalsInfo
{
    public long Rules { get; set; }
    public long Tunnels { get; set; }
    public long Agents { get; set; }
    public long Streams { get; set; }
    public long BytesIn { get; set; }
    public long BytesOut { get; set; }
}

public sealed class ServerInfo
{
    public bool Running { get; set; }
    public string Listen { get; set; } = "";
    public string PublicHost { get; set; } = "";
    public long StartedAt { get; set; }
    public long UptimeSec { get; set; }
    public int AgentCount { get; set; }
    public int TunnelCount { get; set; }
    public int ActiveStreams { get; set; }
    public long BytesIn { get; set; }
    public long BytesOut { get; set; }
    public List<AgentInfo> Agents { get; set; } = new();
    public List<TunnelInfo> Tunnels { get; set; } = new();
}

public sealed class AgentInfo
{
    public long Id { get; set; }
    public string ClientId { get; set; } = "";
    public string Name { get; set; } = "";
    public string Version { get; set; } = "";
    public string RemoteAddr { get; set; } = "";
    public bool Authed { get; set; }
    public long ConnectedAt { get; set; }
    public long UptimeSec { get; set; }
    public long StreamsTotal { get; set; }
    public long BytesIn { get; set; }
    public long BytesOut { get; set; }
    public List<string> Tunnels { get; set; } = new();
}

public sealed class TunnelInfo
{
    public string Key { get; set; } = "";
    public string Name { get; set; } = "";
    public string ClientId { get; set; } = "";
    public int PublicPort { get; set; }
    public string PublicHost { get; set; } = "";
    public string Target { get; set; } = "";
    public long ActiveStreams { get; set; }
    public long Connections { get; set; }
    public long BytesIn { get; set; }
    public long BytesOut { get; set; }
    public long CreatedAt { get; set; }
}

public sealed class ClientInfo
{
    public bool Running { get; set; }
    public string State { get; set; } = "idle";
    public string Server { get; set; } = "";
    public string ClientId { get; set; } = "";
    public string Name { get; set; } = "";
    public long ConnectedAt { get; set; }
    public long UptimeSec { get; set; }
    public long ActiveStreams { get; set; }
    public long ReconnectBackoffMs { get; set; }
    public string? LastError { get; set; }
    public List<ClientTunnelInfo> Tunnels { get; set; } = new();
}

public sealed class ClientTunnelInfo
{
    public string Name { get; set; } = "";
    public string Target { get; set; } = "";
    public int RemotePort { get; set; }
    public int PublicPort { get; set; }
    public bool Registered { get; set; }
    public long Connections { get; set; }
    public string? LastError { get; set; }
}

public sealed class RuleInfo
{
    public string Name { get; set; } = "";
    public string Listen { get; set; } = "";
    public int ListenPort { get; set; }
    public string Strategy { get; set; } = "";
    public bool HealthCheck { get; set; }
    public long Active { get; set; }
    public long Connections { get; set; }
    public long Failed { get; set; }
    public long BytesIn { get; set; }
    public long BytesOut { get; set; }
    public string Type { get; set; } = "";
    public string Target { get; set; } = "";
    public List<RuleTargetInfo> Targets { get; set; } = new();
}

public sealed class RuleTargetInfo
{
    public string Endpoint { get; set; } = "";
    public bool Up { get; set; }
    public long Active { get; set; }
    public string? LastError { get; set; }
}

public sealed class LogItem
{
    public long Id { get; set; }
    public long Time { get; set; }
    public string Level { get; set; } = "";
    public string Tag { get; set; } = "";
    public string Message { get; set; } = "";
}

public sealed class LogsResult
{
    public List<LogItem> Entries { get; set; } = new();
    public long LastId { get; set; }
}

/// <summary>服务端页面持久化的设置。</summary>
public sealed class ServerSettings
{
    public string Bind { get; set; } = "0.0.0.0:7000";
    public string Token { get; set; } = "";
    public string PublicHost { get; set; } = "0.0.0.0";
    public int ControlPort { get; set; } = 7401;
    public bool AutoStart { get; set; } = true;
    public string LogLevel { get; set; } = "info";
    public List<ForwardRuleSetting> Forward { get; set; } = new();
    public List<MappingRuleSetting> Mapping { get; set; } = new();
}

public sealed class ForwardRuleSetting
{
    public string Name { get; set; } = "";
    public string Listen { get; set; } = "";
    public string Target { get; set; } = "";
}

public sealed class MappingRuleSetting
{
    public string Name { get; set; } = "";
    public string Listen { get; set; } = "";
    public List<string> Targets { get; set; } = new();
    public string Strategy { get; set; } = "roundrobin";
    public bool HealthCheck { get; set; } = true;
}

/// <summary>客户端页面持久化的设置。</summary>
public sealed class ClientSettings
{
    public string Server { get; set; } = "127.0.0.1:7000";
    public string Token { get; set; } = "";
    public string ClientId { get; set; } = "";
    public string Name { get; set; } = "";
    public int ControlPort { get; set; } = 7402;
    public bool AutoConnect { get; set; } = true;
    public string LogLevel { get; set; } = "info";
    public List<TunnelSetting> Tunnels { get; set; } = new();
}

public sealed class TunnelSetting
{
    public string Name { get; set; } = "";
    public string LocalHost { get; set; } = "127.0.0.1";
    public int LocalPort { get; set; }
    public int RemotePort { get; set; }
}

public sealed class EngineException : Exception
{
    public EngineException(string message) : base(message) { }
}
