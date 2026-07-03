#include "src/utils/logger.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>

#if defined(__ANDROID__)
#   include <android/log.h>
#endif

#define LOG_BUFFER_SIZE 1024

// 格式化可变参数到栈缓冲区
#define FORMAT_MESSAGE(msgbuf, fmt) \
    char msgbuf[LOG_BUFFER_SIZE]; \
    va_list args; \
    va_start(args, fmt); \
    std::vsnprintf(msgbuf, sizeof(msgbuf), fmt, args); \
    va_end(args)

namespace atlas {
namespace utils {

Logger::Level Logger::global_level_ = Logger::Level::Info;

Logger::Logger(const char* tag) : tag_(tag ? tag : "") {}

const char* Logger::LevelToStr(Level level) {
    switch (level) {
        case Level::Info:  return "I";
        case Level::Debug: return "D";
        case Level::Warn:  return "W";
        case Level::Error: return "E";
        default:           return "-";
    }
}

void Logger::Sink(Level level, const char* tag, const char* message) {
    if (level < global_level_) return;

#if defined(__ANDROID__)
    int prio = ANDROID_LOG_INFO;
    switch (level) {
        case Level::Debug: prio = ANDROID_LOG_DEBUG; break;
        case Level::Warn:  prio = ANDROID_LOG_WARN;  break;
        case Level::Error: prio = ANDROID_LOG_ERROR; break;
        default:           prio = ANDROID_LOG_INFO;  break;
    }
    __android_log_print(prio, tag, "%s", message);
#else
    std::time_t t = std::time(nullptr);
    char timebuf[20];
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S",
                  std::localtime(&t));
    std::printf("%s %s %s %s\n", timebuf, LevelToStr(level), tag, message);
#endif
}

void Logger::Log(Level level, const char* tag, const char* fmt, ...) {
    FORMAT_MESSAGE(msgbuf, fmt);
    Sink(level, tag, msgbuf);
}

void Logger::Debug(const char* fmt, ...) {
    FORMAT_MESSAGE(msgbuf, fmt);
    Sink(Level::Debug, tag_.c_str(), msgbuf);
}

void Logger::Info(const char* fmt, ...) {
    FORMAT_MESSAGE(msgbuf, fmt);
    Sink(Level::Info, tag_.c_str(), msgbuf);
}

void Logger::Warn(const char* fmt, ...) {
    FORMAT_MESSAGE(msgbuf, fmt);
    Sink(Level::Warn, tag_.c_str(), msgbuf);
}

void Logger::Error(const char* fmt, ...) {
    FORMAT_MESSAGE(msgbuf, fmt);
    Sink(Level::Error, tag_.c_str(), msgbuf);
}

void Logger::SetLevel(Level level) {
    global_level_ = level;
}

Logger::Level Logger::GetLevel() {
    return global_level_;
}

}  // namespace utils
}  // namespace atlas
