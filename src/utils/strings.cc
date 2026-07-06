#include "src/utils/strings.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>

namespace atlas {
namespace utils {

// static
ErrorCode Strings::ParseInt(const std::string& s, int* out) {
    return ParseInt(s.c_str(), out, 10);
}

// static
ErrorCode Strings::ParseInt(const char* s, int* out) {
    return ParseInt(s, out, 10);
}

// static
ErrorCode Strings::ParseInt(const std::string& s, int* out, int base) {
    return ParseInt(s.c_str(), out, base);
}

// static
ErrorCode Strings::ParseInt(const char* s, int* out, int base) {
    if (s == nullptr || out == nullptr) {
        return ErrorCode::kInvalidArgument;
    }

    char* end = nullptr;
    errno = 0;

    // strtol returns long; cast to int after range check below.
    long val = std::strtol(s, &end, base);  // NOLINT(runtime/int)

    if (errno == ERANGE) {
        return ErrorCode::kInvalidArgument;  // strtol detected overflow
    }
    if (end == s) {
        return ErrorCode::kInvalidArgument;  // no digits parsed
    }
    if (*end != '\0') {
        return ErrorCode::kInvalidArgument;  // trailing characters
    }

    // Range check: value must fit in int.  On 64-bit platforms long may be
    // larger than int, so strtol may succeed even if out of int range.
    if (val < static_cast<long>(INT_MIN) || val > static_cast<long>(INT_MAX)) {  // NOLINT(runtime/int)
        return ErrorCode::kInvalidArgument;
    }

    *out = static_cast<int>(val);
    return ErrorCode::kOk;
}

}  // namespace utils
}  // namespace atlas