#pragma once

#include <string>

// TAG mechanism: define LOG_TAG before #include to set a custom tag
#ifdef LOG_TAG
#  define ATLAS_LOG_TAG LOG_TAG
#else
#  define ATLAS_LOG_TAG __FILE__
#endif

// Log macros always expand; output gated at runtime by Logger::global_level_
#  define ATLAS_LOGD(fmt, ...) \
    ::atlas::utils::Logger::Log(::atlas::utils::Logger::Level::Debug, \
                                ATLAS_LOG_TAG, fmt, ##__VA_ARGS__)
#  define ATLAS_LOGI(fmt, ...) \
    ::atlas::utils::Logger::Log(::atlas::utils::Logger::Level::Info, \
                                ATLAS_LOG_TAG, fmt, ##__VA_ARGS__)
#  define ATLAS_LOGW(fmt, ...) \
    ::atlas::utils::Logger::Log(::atlas::utils::Logger::Level::Warn, \
                                ATLAS_LOG_TAG, fmt, ##__VA_ARGS__)
#  define ATLAS_LOGE(fmt, ...) \
    ::atlas::utils::Logger::Log(::atlas::utils::Logger::Level::Error, \
                                ATLAS_LOG_TAG, fmt, ##__VA_ARGS__)

namespace atlas {
namespace utils {

/// Lightweight cross-platform logging
///
/// Dual interface: macros + instance methods
///   - Convenience macros: ATLAS_LOGD / ATLAS_LOGI / ATLAS_LOGW / ATLAS_LOGE
///   - Instance methods: Logger logger("tag"); logger.Info(...);
///
/// Platform adaptation:
///   - Android: __android_log_print() → logcat
///   - Others: std::printf + timestamp
class Logger {
 public:
    // Levels ordered most→least verbose (0 = most, 3 = least)
    enum class Level {
        Info = 0,   // Routine info (init, state changes)
        Debug = 1,  // Debug info, development only
        Warn = 2,   // Warning (non-fatal anomaly)
        Error = 3,  // Error (operation failed)
    };

    /// Construct with a fixed tag
    explicit Logger(const char* tag);
    ~Logger() = default;

    /// Instance methods (use bound tag)
    void Debug(const char* fmt, ...);
    void Info(const char* fmt, ...);
    void Warn(const char* fmt, ...);
    void Error(const char* fmt, ...);

    /// Static method (used by macros, tag passed each call)
    static void Log(Level level, const char* tag, const char* fmt, ...);

    /// Global log level control
    static void SetLevel(Level level);
    static Level GetLevel();

 private:
    std::string tag_;
    static Level global_level_;  // Defaults to Level::Info
    static const char* LevelToStr(Level level);
    static void Sink(Level level, const char* tag, const char* message);
};

}  // namespace utils
}  // namespace atlas