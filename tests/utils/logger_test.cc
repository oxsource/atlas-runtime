#define LOG_TAG "atlas@logger_test"
#include "src/utils/logger.h"

#include <gtest/gtest.h>
#include <sstream>

namespace atlas {
namespace utils {

class LoggerTest : public ::testing::Test {
 protected:
    void SetUp() override {
        Logger::SetLevel(Logger::Level::Debug);
    }

    void TearDown() override {
        Logger::SetLevel(Logger::Level::Info);
    }
};

TEST_F(LoggerTest, MacrosCompile) {
    // 验证四个级别的宏均可正常编译和调用
    ATLAS_LOGD("Debug message: %d", 42);
    ATLAS_LOGI("Info message: %s", "hello");
    ATLAS_LOGW("Warn message: %.2f", 3.14);
    ATLAS_LOGE("Error message: 0x%X", 255);
}

TEST_F(LoggerTest, InstanceMethods) {
    Logger logger("TestInstance");
    logger.Debug("debug: %d", 100);
    logger.Info("info: %s", "test");
    logger.Warn("warn: %.3f", 2.718);
    logger.Error("error: %s", "occurred");
}

TEST_F(LoggerTest, LevelFiltering) {
    Logger logger("LevelTest");

    // 设置为 Warn 级别，Debug 和 Info 应被过滤
    Logger::SetLevel(Logger::Level::Warn);
    EXPECT_EQ(Logger::GetLevel(), Logger::Level::Warn);

    // Debug 和 Info 低于 Warn，Sink 中会 return 而不输出
    // 此处仅验证调用不崩溃
    logger.Debug("should be filtered");
    logger.Info("should be filtered");

    logger.Warn("should be printed");
    logger.Error("should be printed");
}

TEST_F(LoggerTest, LevelToStr) {
    // LevelToStr 为私有方法，通过输出格式间接验证
    // 此处仅验证 SetLevel / GetLevel 一致性
    Logger::SetLevel(Logger::Level::Debug);
    EXPECT_EQ(Logger::GetLevel(), Logger::Level::Debug);

    Logger::SetLevel(Logger::Level::Info);
    EXPECT_EQ(Logger::GetLevel(), Logger::Level::Info);

    Logger::SetLevel(Logger::Level::Warn);
    EXPECT_EQ(Logger::GetLevel(), Logger::Level::Warn);

    Logger::SetLevel(Logger::Level::Error);
    EXPECT_EQ(Logger::GetLevel(), Logger::Level::Error);
}

}  // namespace utils
}  // namespace atlas
