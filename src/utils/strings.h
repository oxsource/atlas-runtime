#pragma once

#include <string>

#include "src/utils/types.h"

namespace atlas {
namespace utils {

// Pure-static utility class for locale-safe C-style string parsing.
// All methods use std::strtol / std::strtof internally and never construct a
// std::locale, making them safe for Android NDK static-initialization contexts.
// See docs/code_spec.md §11.1 for platform compatibility details.
class Strings {
 public:
    Strings() = delete;

    // Parses a decimal integer from |s|.  Returns kOk on success and stores
    // the result in |out|.  Returns kInvalidArgument on empty string, non-digit
    // characters, trailing characters, or overflow.
    static ErrorCode ParseInt(const std::string& s, int* out);

    // Overload for C-string input.
    static ErrorCode ParseInt(const char* s, int* out);

    // Parses an integer in the given |base| (2–36, or 0 for auto-detection).
    static ErrorCode ParseInt(const std::string& s, int* out, int base);

    // Overload for C-string input with explicit base.
    static ErrorCode ParseInt(const char* s, int* out, int base);
};

}  // namespace utils
}  // namespace atlas