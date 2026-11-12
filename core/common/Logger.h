// 线程安全日志：控制台 + 文件 + 内存环形缓冲（供 C# 页面拉取）
#pragma once
#ifndef ORSEND_LOGGER_H
#define ORSEND_LOGGER_H

#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace orsend {

enum class LogLevel : int { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4, Fatal = 5, Off = 6 };

const char* LogLevelName(LogLevel level);
LogLevel ParseLogLevel(const std::string& text);

struct LogEntry {
    uint64_t id = 0;
    int64_t timeMs = 0;
    LogLevel level = LogLevel::Info;
    std::string tag;
    std::string message;
};

class Logger {
public:
    static Logger& Instance();

    void SetLevel(LogLevel level);
    LogLevel GetLevel() const;
    void SetConsoleEnabled(bool enabled);
    bool OpenFile(const std::string& path);
    void CloseFile();
    void SetBufferCapacity(size_t capacity);

    void Log(LogLevel level, const std::string& tag, const std::string& message);

    // 取出 id 大于 sinceId 的日志（最多 maxCount 条），用于控制接口 / 页面轮询
    std::vector<LogEntry> Tail(uint64_t sinceId, size_t maxCount) const;
    uint64_t LastId() const;

private:
    Logger() = default;

    mutable std::mutex mutex_;
    LogLevel level_ = LogLevel::Info;
    bool console_ = true;
    std::ofstream file_;
    std::deque<LogEntry> buffer_;
    size_t capacity_ = 2000;
    uint64_t nextId_ = 1;
};

#define ORSEND_LOG_STREAM(level, tag, expr)                        \
    do {                                                           \
        if (static_cast<int>(level) >=                             \
            static_cast<int>(::orsend::Logger::Instance().GetLevel())) { \
            std::ostringstream _orsend_os;                         \
            _orsend_os << expr;                                    \
            ::orsend::Logger::Instance().Log(level, tag, _orsend_os.str()); \
        }                                                          \
    } while (0)

#define LOG_TRACE(tag, expr) ORSEND_LOG_STREAM(::orsend::LogLevel::Trace, tag, expr)
#define LOG_DEBUG(tag, expr) ORSEND_LOG_STREAM(::orsend::LogLevel::Debug, tag, expr)
#define LOG_INFO(tag, expr) ORSEND_LOG_STREAM(::orsend::LogLevel::Info, tag, expr)
#define LOG_WARN(tag, expr) ORSEND_LOG_STREAM(::orsend::LogLevel::Warn, tag, expr)
#define LOG_ERROR(tag, expr) ORSEND_LOG_STREAM(::orsend::LogLevel::Error, tag, expr)

}  // namespace orsend

#endif  // ORSEND_LOGGER_H
