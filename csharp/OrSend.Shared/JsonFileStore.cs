using System.Text.Json;

namespace OrSend.Shared;

/// <summary>极简的 JSON 文件持久化，用于保存页面配置。</summary>
public sealed class JsonFileStore<T> where T : new()
{
    private readonly string _path;
    private readonly object _sync = new();

    public JsonFileStore(string path)
    {
        _path = path;
        var dir = Path.GetDirectoryName(path);
        if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
    }

    public string FilePath => _path;

    public T Load()
    {
        lock (_sync)
        {
            try
            {
                if (!File.Exists(_path)) return new T();
                var text = File.ReadAllText(_path);
                if (string.IsNullOrWhiteSpace(text)) return new T();
                return JsonSerializer.Deserialize<T>(text, EngineClient.JsonOptions) ?? new T();
            }
            catch
            {
                return new T();
            }
        }
    }

    public void Save(T value)
    {
        lock (_sync)
        {
            try
            {
                var json = JsonSerializer.Serialize(value, new JsonSerializerOptions(EngineClient.JsonOptions)
                {
                    WriteIndented = true
                });
                File.WriteAllText(_path, json);
            }
            catch
            {
                // 配置写入失败不应影响主流程
            }
        }
    }
}
