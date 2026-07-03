#pragma once

#include <string>

// TAG 机制：在 #include "src/utils/logger.h" 之前定义 LOG_TAG 自定义标签
#ifdef LOG_TAG
#  define ATLAS_LOG_TAG LOG_TAG
#else
#  define ATLAS_LOG_TAG __FILE__
#endif

// 编译期可裁剪：定义 ATLAS_ENABLE_LOGGING 时，日志宏展开为真实调用；
// 否则展开为 ((void)0)，零运行时开销。
#ifdef ATLAS_ENABLE_LOGGING
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
#else
#  define ATLAS_LOGD(fmt, ...)  ((void)0)
#  define ATLAS_LOGI(fmt, ...)  ((void)0)
#  define ATLAS_LOGW(fmt, ...)  ((void)0)
#  define ATLAS_LOGE(fmt, ...)  ((void)0)
#endif

namespace atlas {
namespace utils {

/// 轻量级跨平台日志输出管理
///
/// 提供宏 + 类双接口：
///   - 便捷宏：ATLAS_LOGD / ATLAS_LOGI / ATLAS_LOGW / ATLAS_LOGE
///   - 实例方法：Logger logger("tag"); logger.Info(...);
///
/// 平台适配：
///   - Android：__android_log_print() → logcat
///   - 其他：std::printf + 时间戳
class Logger {
 public:
    enum class Level {
        Debug,  // 调试信息，仅开发期开启
        Info,   // 常规信息（初始化、状态变更）
        Warn,   // 警告（非致命异常）
        Error,  // 错误（操作失败）
    };

    /// 构造实例，绑定固定 TAG
    explicit Logger(const char* tag);
    ~Logger() = default;

    /// 实例方法（使用构造时绑定的 TAG）
    void Debug(const char* fmt, ...);
    void Info(const char* fmt, ...);
    void Warn(const char* fmt, ...);
    void Error(const char* fmt, ...);

    /// 静态方法（宏使用此接口，每次传入 TAG）
    static void Log(Level level, const char* tag, const char* fmt, ...);

    /// 全局日志级别控制
    static void SetLevel(Level level);
    static Level GetLevel();

 private:
    std::string tag_;
    static Level global_level_;  // 默认 Level::Info
    static const char* LevelToStr(Level level);
    static void Sink(Level level, const char* tag, const char* message);
};

}  // namespace utils
}  // namespace atlas
