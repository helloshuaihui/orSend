#include "Logger.h"

#include <chrono>
#include <cstdio>
#include <iostream>

#include "Common.h"

namespace orsend {

const char* LogLevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        default: return "OFF";
    }
}

LogLevel ParseLogLevel(const std::string& text) {
    const std::string s = ToLowerAscii(Trim(text));
    if (s == "trace") return LogLevel::Trace;
    if (s == "debug") return LogLevel::Debug;
    if (s == "info") return LogLevel::Info;
    if (s == "warn" || s == "warning") return LogLevel::Warn;
    if (s == "error") return LogLevel::Error;
    if (s == "fatal") return LogLevel::Fatal;
    if (s == "off" || s == "none") return LogLevel::Off;
    return LogLevel::Info;
}

Logger& Logger::Instance() {
    static Logger inst;
    return inst;
}

void Logger::SetLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

LogLevel Logger::GetLevel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

void Logger::SetConsoleEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    console_ = enabled;
}

bool Logger::OpenFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) file_.close();
    if (path.empty()) return true;
    file_.open(path, std::ios::out | std::ios::app);
    return file_.is_open();
}

void Logger::CloseFile() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

void Logger::SetBufferCapacity(size_t capacity) {
    std::lock_guard<std::mutex> lock(mutex_);
    capacity_ = capacity < 100 ? 100 : capacity;
    while (buffer_.size() > capacity_) buffer_.pop_front();
}

void Logger::Log(LogLevel level, const std::string& tag, const std::string& message) {
    const int64_t now = NowMs();
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<int>(level) < static_cast<int>(level_)) return;

    LogEntry entry;
    entry.id = nextId_++;
    entry.timeMs = now;
    entry.level = level;
    entry.tag = tag;
    entry.message = message;
    buffer_.push_back(entry);
    while (buffer_.size() > capacity_) buffer_.pop_front();

    const std::string line = "[" + FormatTimeLocal(now) + "] [" + LogLevelName(level) + "] [" + tag + "] " + message;
    if (console_) {
        std::ostream& os = (level >= LogLevel::Warn) ? std::cerr : std::cout;
        os << line << std::endl;
    }
    if (file_.is_open()) {
        file_ << line << "\n";
        file_.flush();
    }
}

std::vector<LogEntry> Logger::Tail(uint64_t sinceId, size_t maxCount) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LogEntry> out;
    if (maxCount == 0) maxCount = 200;
    for (const auto& e : buffer_) {
        if (e.id > sinceId) out.push_back(e);
    }
    if (out.size() > maxCount) {
        out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(maxCount));
    }
    return out;
}

uint64_t Logger::LastId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return nextId_ - 1;
}

}  // namespace orsend
