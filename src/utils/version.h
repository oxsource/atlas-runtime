#pragma once

namespace atlas {
namespace utils {

constexpr int kVersionMajor = 1;
constexpr int kVersionMinor = 0;
constexpr int kVersionPatch = 0;

// Returns the version string, e.g. "1.0.0".
const char* VersionString();

}  // namespace utils
}  // namespace atlas
