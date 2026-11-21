#pragma once

#include <iostream>
#include <string>

namespace ba {

enum class LogLevel {
    Error = 0,
    Warn = 1,
    Info = 2,
    Debug = 3
};

class Logger {
public:
    explicit Logger(std::ostream& stream = std::cerr, LogLevel level = LogLevel::Info)
        : Stream_(&stream), Level_(level) {}

    void SetLevel(LogLevel level) noexcept { Level_ = level; }
    LogLevel Level() const noexcept { return Level_; }

    void Error(const std::string& message) { Write(LogLevel::Error, "ERROR", message); }
    void Warn(const std::string& message) { Write(LogLevel::Warn, "WARN", message); }
    void Info(const std::string& message) { Write(LogLevel::Info, "INFO", message); }
    void Debug(const std::string& message) { Write(LogLevel::Debug, "DEBUG", message); }

private:
    void Write(LogLevel level, const char* name, const std::string& message) {
        if (static_cast<int>(level) > static_cast<int>(Level_)) {
            return;
        }
        *Stream_ << "[buyer-assistant] " << name << ": " << message << '\n';
        Stream_->flush();
    }

    std::ostream* Stream_;
    LogLevel Level_ = LogLevel::Info;
};

} // namespace ba
