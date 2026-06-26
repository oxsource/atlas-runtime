#include "src/utils/version.h"

namespace atlas {
namespace utils {

namespace {
constexpr char kVersionStr[] = "1.0.0";
}  // namespace

const char* VersionString() { return kVersionStr; }

}  // namespace utils
}  // namespace atlas
