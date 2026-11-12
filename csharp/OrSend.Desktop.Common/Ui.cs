using System.Windows;
using System.Windows.Media;

namespace OrSend.Desktop.Common;

/// <summary>把引擎里的字节数 / 秒数转成适合界面显示的短文本。</summary>
public static class Fmt
{
    public static string Bytes(long bytes)
    {
        if (bytes < 1024) return bytes + " B";
        double v = bytes;
        string[] units = { "KB", "MB", "GB", "TB" };
        foreach (var u in units)
        {
            v /= 1024;
            if (v < 1024 || u == "TB") return v.ToString(v < 10 ? "0.##" : "0.#") + " " + u;
        }
        return bytes + " B";
    }

    public static string Duration(long seconds)
    {
        if (seconds < 60) return seconds + " 秒";
        if (seconds < 3600) return (seconds / 60) + " 分 " + (seconds % 60) + " 秒";
        if (seconds < 86400) return (seconds / 3600) + " 时 " + (seconds % 3600 / 60) + " 分";
        return (seconds / 86400) + " 天 " + (seconds % 86400 / 3600) + " 时";
    }

    public static string Clock(long unixMs)
    {
        if (unixMs <= 0) return "-";
        return DateTimeOffset.FromUnixTimeMilliseconds(unixMs).ToLocalTime().ToString("HH:mm:ss");
    }

    public static string TimeOfDay(long unixMs) => Clock(unixMs);
}

/// <summary>日志行（界面用），带颜色。</summary>
public sealed class LogRow
{
    public string Time { get; init; } = "";
    public string Level { get; init; } = "";
    public string Tag { get; init; } = "";
    public string Message { get; init; } = "";
    public Brush LevelBrush { get; init; } = Brushes.Gray;
    public long Id { get; init; }

    private static readonly Brush Info = new SolidColorBrush(Color.FromRgb(0x6B, 0x72, 0x80));
    private static readonly Brush Warn = new SolidColorBrush(Color.FromRgb(0xB4, 0x53, 0x09));
    private static readonly Brush Error = new SolidColorBrush(Color.FromRgb(0xDC, 0x26, 0x26));
    private static readonly Brush Debug = new SolidColorBrush(Color.FromRgb(0x9C, 0xA3, 0xAF));

    public static LogRow From(OrSend.Shared.LogItem item)
    {
        var level = (item.Level ?? "").ToUpperInvariant();
        var brush = level switch
        {
            "WARN" or "WARNING" => Warn,
            "ERROR" or "FATAL" => Error,
            "DEBUG" or "TRACE" => Debug,
            _ => Info
        };
        return new LogRow
        {
            Id = item.Id,
            Time = Fmt.TimeOfDay(item.Time),
            Level = level,
            Tag = item.Tag ?? "",
            Message = item.Message ?? "",
            LevelBrush = brush
        };
    }
}

/// <summary>左侧导航项。</summary>
public sealed class NavItem
{
    public string Key { get; init; } = "";
    public string Icon { get; init; } = "";
    public string Title { get; init; } = "";
    public override string ToString() => Title;
}

/// <summary>窗口尺寸兜底：小屏 / 高缩放时不让窗口超出可用工作区。</summary>
public static class WindowFit
{
    public static void ClampToWorkArea(Window window, double margin = 24)
    {
        var area = SystemParameters.WorkArea;
        if (area.Width <= 0 || area.Height <= 0) return;

        double maxW = Math.Max(window.MinWidth, area.Width - margin * 2);
        double maxH = Math.Max(window.MinHeight, area.Height - margin * 2);

        if (!double.IsNaN(window.Width) && window.Width > maxW) window.Width = maxW;
        if (!double.IsNaN(window.Height) && window.Height > maxH) window.Height = maxH;
    }
}
