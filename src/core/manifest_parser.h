#pragma once

#include <string>

#include "src/core/manifest_config.h"
#include "src/utils/types.h"

namespace atlas {
namespace core {

// Parses a manifest JSON file and produces a ManifestConfig.
//
// All public methods are thread-compatible (concurrent reads are safe as long
// as no writes occur simultaneously).  The parser is stateless; a single
// instance may be reused across multiple Parse() calls.
class ManifestParser {
 public:
    ManifestParser() = default;
    ~ManifestParser() = default;

    // Parses the manifest file at |path| and populates |config|.
    //
    // @param path   Absolute or relative path to the manifest JSON file.
    // @param config Output parameter; must not be nullptr.
    // @return kOk on success.
    //         kFileNotFound   if the file cannot be opened.
    //         kParseError     if JSON is malformed or a required field is missing
    //                         or has an invalid value.
    //         kVersionMismatch if the manifest's major version differs from the
    //                         parser's supported major version.
    //         kInvalidArgument if model ids are duplicated, models array is empty,
    //                         or an environment variable referenced in model_path
    //                         is not set.
    utils::ErrorCode Parse(const std::string& path, ManifestConfig* config) const;
};

}  // namespace core
}  // namespace atlas
