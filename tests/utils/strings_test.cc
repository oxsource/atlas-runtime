#include <gtest/gtest.h>

#include "src/utils/strings.h"
#include "src/utils/types.h"

namespace atlas {
namespace utils {
namespace {

TEST(StringsTest, ParseInt_Decimal) {
    int val = 0;
    EXPECT_EQ(Strings::ParseInt("42", &val), ErrorCode::kOk);
    EXPECT_EQ(val, 42);

    EXPECT_EQ(Strings::ParseInt("-7", &val), ErrorCode::kOk);
    EXPECT_EQ(val, -7);

    EXPECT_EQ(Strings::ParseInt("0", &val), ErrorCode::kOk);
    EXPECT_EQ(val, 0);
}

TEST(StringsTest, ParseInt_Empty) {
    int val = 0;
    EXPECT_EQ(Strings::ParseInt("", &val), ErrorCode::kInvalidArgument);
}

TEST(StringsTest, ParseInt_TrailingChars) {
    int val = 0;
    EXPECT_EQ(Strings::ParseInt("42abc", &val), ErrorCode::kInvalidArgument);
    EXPECT_EQ(Strings::ParseInt("42 ", &val), ErrorCode::kInvalidArgument);
}

TEST(StringsTest, ParseInt_NoDigits) {
    int val = 0;
    EXPECT_EQ(Strings::ParseInt("abc", &val), ErrorCode::kInvalidArgument);
    EXPECT_EQ(Strings::ParseInt("  ", &val), ErrorCode::kInvalidArgument);
}

TEST(StringsTest, ParseInt_Overflow) {
    int val = 0;
    // 9999999999 exceeds int range on 32-bit platforms.
    EXPECT_EQ(Strings::ParseInt("9999999999", &val), ErrorCode::kInvalidArgument);
}

TEST(StringsTest, ParseInt_NullInput) {
    int val = 0;
    EXPECT_EQ(Strings::ParseInt(nullptr, &val), ErrorCode::kInvalidArgument);
}

TEST(StringsTest, ParseInt_NullOutput) {
    EXPECT_EQ(Strings::ParseInt("42", nullptr), ErrorCode::kInvalidArgument);
}

TEST(StringsTest, ParseInt_Base16) {
    int val = 0;
    EXPECT_EQ(Strings::ParseInt("2a", &val, 16), ErrorCode::kOk);
    EXPECT_EQ(val, 42);

    EXPECT_EQ(Strings::ParseInt("FF", &val, 16), ErrorCode::kOk);
    EXPECT_EQ(val, 255);
}

TEST(StringsTest, ParseInt_Base8) {
    int val = 0;
    EXPECT_EQ(Strings::ParseInt("10", &val, 8), ErrorCode::kOk);
    EXPECT_EQ(val, 8);
}

TEST(StringsTest, ParseInt_Base0AutoDetect) {
    int val = 0;
    EXPECT_EQ(Strings::ParseInt("0x2a", &val, 0), ErrorCode::kOk);
    EXPECT_EQ(val, 42);

    EXPECT_EQ(Strings::ParseInt("012", &val, 0), ErrorCode::kOk);
    EXPECT_EQ(val, 10);
}

TEST(StringsTest, ParseInt_CString) {
    int val = 0;
    EXPECT_EQ(Strings::ParseInt("100", &val), ErrorCode::kOk);
    EXPECT_EQ(val, 100);
}

TEST(StringsTest, ParseInt_Base2) {
    int val = 0;
    EXPECT_EQ(Strings::ParseInt("101010", &val, 2), ErrorCode::kOk);
    EXPECT_EQ(val, 42);
}

TEST(StringsTest, ParseInt_StringOverload) {
    int val = 0;
    const std::string s = "1234";
    EXPECT_EQ(Strings::ParseInt(s, &val), ErrorCode::kOk);
    EXPECT_EQ(val, 1234);

    // With explicit base via std::string overload.
    EXPECT_EQ(Strings::ParseInt(s, &val, 10), ErrorCode::kOk);
    EXPECT_EQ(val, 1234);
}

}  // namespace
}  // namespace utils
}  // namespace atlas