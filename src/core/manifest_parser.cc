#include "src/core/manifest_parser.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "nlohmann/json.hpp"

#include "src/core/manifest_config.h"
#include "src/utils/types.h"

namespace atlas {
namespace core {

namespace {

// ---------------------------------------------------------------------------
// Numeric constants
// ---------------------------------------------------------------------------

// Major version the parser currently supports.
constexpr int kSupportedMajorVersion = 1;

// Offset from the start of "${" to the first character of the variable name.
constexpr size_t kEnvVarNameOffset = 2;

// ---------------------------------------------------------------------------
// dtype string constants
// ---------------------------------------------------------------------------

constexpr std::string_view kDtypeFloat32 = "float32";
constexpr std::string_view kDtypeFloat16 = "float16";
constexpr std::string_view kDtypeInt8    = "int8";
constexpr std::string_view kDtypeUInt8   = "uint8";
constexpr std::string_view kDtypeInt32   = "int32";

// ---------------------------------------------------------------------------
// Environment-variable expansion delimiters
// ---------------------------------------------------------------------------

constexpr std::string_view kEnvVarOpen  = "${";
constexpr char             kEnvVarClose = '}';

// ---------------------------------------------------------------------------
// Manifest JSON field key constants
// ---------------------------------------------------------------------------

// Top-level keys
constexpr const char* kKeyVersion = "version";
constexpr const char* kKeyName    = "name";
constexpr const char* kKeyModels  = "models";

// Model-level keys
constexpr const char* kKeyId           = "id";
constexpr const char* kKeyBackend      = "backend";
constexpr const char* kKeyModelPath    = "model_path";
constexpr const char* kKeyLoadStrategy = "load_strategy";
constexpr const char* kKeyInputs       = "inputs";
constexpr const char* kKeyOutputs      = "outputs";
constexpr const char* kKeyConfig       = "config";

// load_strategy values
constexpr std::string_view kLoadStrategyLazy  = "lazy";
constexpr std::string_view kLoadStrategyEager = "eager";

// Tensor-level keys
constexpr const char* kKeyShape     = "shape";
constexpr const char* kKeyDtype     = "dtype";
constexpr const char* kKeyLayout    = "layout";
constexpr const char* kKeyNormalize = "normalize";
constexpr const char* kKeyMean      = "mean";
constexpr const char* kKeyStd       = "std";

// ---------------------------------------------------------------------------
// Helper functions
// ---------------------------------------------------------------------------

// Splits a "major.minor" version string.  Returns false on malformed input.
bool ParseVersionString(const std::string& version, int* major, int* minor) {
    std::istringstream ss(version);
    char dot = 0;
    return static_cast<bool>(ss >> *major >> dot >> *minor) && dot == '.';
}

// Maps a dtype string to the corresponding DataType enum value.
utils::DataType StringToDataType(const std::string& dtype) {
    if (dtype == kDtypeFloat32) return utils::DataType::kFloat32;
    if (dtype == kDtypeFloat16) return utils::DataType::kFloat16;
    if (dtype == kDtypeInt8)    return utils::DataType::kInt8;
    if (dtype == kDtypeUInt8)   return utils::DataType::kUInt8;
    if (dtype == kDtypeInt32)   return utils::DataType::kInt32;
    return utils::DataType::kUnknown;
}

// Expands all ${VAR} patterns in |str| using environment variables.
// Returns false (and leaves |str| unchanged) if any variable is not set.
bool ExpandEnvVars(std::string* str) {
    const std::string& src = *str;
    std::string result;
    result.reserve(src.size());

    size_t pos = 0;
    while (pos < src.size()) {
        size_t start = src.find(kEnvVarOpen, pos);
        if (start == std::string::npos) {
            result += src.substr(pos);
            break;
        }
        result += src.substr(pos, start - pos);

        size_t end = src.find(kEnvVarClose, start + kEnvVarNameOffset);
        if (end == std::string::npos) {
            // Unclosed '${'  — treat as literal.
            result += src.substr(start);
            break;
        }

        std::string var_name =
            src.substr(start + kEnvVarNameOffset,
                       end - start - kEnvVarNameOffset);
        const char* val = std::getenv(var_name.c_str());
        if (val == nullptr) {
            return false;
        }
        result += val;
        pos = end + 1;
    }

    *str = std::move(result);
    return true;
}

// Parses a single TensorInfo object from |j|.
utils::ErrorCode ParseTensorInfo(const nlohmann::json& j,
                                  utils::TensorInfo* info) {
    if (!j.contains(kKeyName) || !j[kKeyName].is_string()) {
        return utils::ErrorCode::kParseError;
    }
    info->name = j[kKeyName].get<std::string>();

    if (!j.contains(kKeyShape) || !j[kKeyShape].is_array()) {
        return utils::ErrorCode::kParseError;
    }
    for (const auto& dim : j[kKeyShape]) {
        if (!dim.is_number_integer()) return utils::ErrorCode::kParseError;
        info->shape.push_back(dim.get<int>());
    }

    if (!j.contains(kKeyDtype) || !j[kKeyDtype].is_string()) {
        return utils::ErrorCode::kParseError;
    }
    info->dtype = StringToDataType(j[kKeyDtype].get<std::string>());
    if (info->dtype == utils::DataType::kUnknown) {
        return utils::ErrorCode::kParseError;
    }

    if (j.contains(kKeyLayout) && j[kKeyLayout].is_string()) {
        info->layout = j[kKeyLayout].get<std::string>();
    }

    if (j.contains(kKeyNormalize) && j[kKeyNormalize].is_object()) {
        const auto& norm = j[kKeyNormalize];
        info->has_normalize = true;
        if (norm.contains(kKeyMean) && norm[kKeyMean].is_array()) {
            for (const auto& v : norm[kKeyMean]) {
                info->normalize.mean.push_back(v.get<float>());
            }
        }
        if (norm.contains(kKeyStd) && norm[kKeyStd].is_array()) {
            for (const auto& v : norm[kKeyStd]) {
                info->normalize.std.push_back(v.get<float>());
            }
        }
    }

    return utils::ErrorCode::kOk;
}

// Parses the JSON object at |j| (one entry in the "models" array) into
// |model|.  |index| is used only for constructing diagnostic field paths.
utils::ErrorCode ParseModelConfig(const nlohmann::json& j,
                                   int index,
                                   ModelConfig* model) {
    (void)index;  // Reserved for future error-path reporting.

    if (!j.contains(kKeyId) || !j[kKeyId].is_string()) {
        return utils::ErrorCode::kParseError;
    }
    model->id = j[kKeyId].get<std::string>();

    if (!j.contains(kKeyBackend) || !j[kKeyBackend].is_string()) {
        return utils::ErrorCode::kParseError;
    }
    model->backend = j[kKeyBackend].get<std::string>();

    if (!j.contains(kKeyModelPath) || !j[kKeyModelPath].is_string()) {
        return utils::ErrorCode::kParseError;
    }
    model->model_path = j[kKeyModelPath].get<std::string>();
    if (!ExpandEnvVars(&model->model_path)) {
        return utils::ErrorCode::kInvalidArgument;
    }

    if (j.contains(kKeyName) && j[kKeyName].is_string()) {
        model->name = j[kKeyName].get<std::string>();
    }

    // Optional: load_strategy (default: eager)
    if (j.contains(kKeyLoadStrategy) && j[kKeyLoadStrategy].is_string()) {
        const auto val = j[kKeyLoadStrategy].get<std::string>();
        if (val == kLoadStrategyLazy) {
            model->load_strategy = LoadStrategy::kLazy;
        } else if (val == kLoadStrategyEager) {
            model->load_strategy = LoadStrategy::kEager;
        } else {
            model->load_strategy = LoadStrategy::kEager;
        }
    }

    if (!j.contains(kKeyInputs) || !j[kKeyInputs].is_array()) {
        return utils::ErrorCode::kParseError;
    }
    for (const auto& input_json : j[kKeyInputs]) {
        utils::TensorInfo info;
        auto ret = ParseTensorInfo(input_json, &info);
        if (ret != utils::ErrorCode::kOk) return ret;
        model->inputs.push_back(std::move(info));
    }

    if (!j.contains(kKeyOutputs) || !j[kKeyOutputs].is_array()) {
        return utils::ErrorCode::kParseError;
    }
    for (const auto& output_json : j[kKeyOutputs]) {
        utils::TensorInfo info;
        auto ret = ParseTensorInfo(output_json, &info);
        if (ret != utils::ErrorCode::kOk) return ret;
        model->outputs.push_back(std::move(info));
    }

    if (j.contains(kKeyConfig) && j[kKeyConfig].is_object()) {
        for (const auto& [key, val] : j[kKeyConfig].items()) {
            if (val.is_string()) {
                model->config[key] = val.get<std::string>();
            }
        }
    }

    return utils::ErrorCode::kOk;
}

}  // namespace

utils::ErrorCode ManifestParser::Parse(const std::string& path,
                                        ManifestConfig* config) const {
    if (config == nullptr) {
        return utils::ErrorCode::kInvalidArgument;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        return utils::ErrorCode::kFileNotFound;
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (const nlohmann::json::parse_error&) {
        return utils::ErrorCode::kParseError;
    }

    // Validate and parse version.
    if (!j.contains(kKeyVersion) || !j[kKeyVersion].is_string()) {
        return utils::ErrorCode::kParseError;
    }
    config->version = j[kKeyVersion].get<std::string>();

    int major = 0;
    int minor = 0;
    if (!ParseVersionString(config->version, &major, &minor)) {
        return utils::ErrorCode::kParseError;
    }
    if (major != kSupportedMajorVersion) {
        return utils::ErrorCode::kVersionMismatch;
    }

    // Validate and parse name.
    if (!j.contains(kKeyName) || !j[kKeyName].is_string()) {
        return utils::ErrorCode::kParseError;
    }
    config->name = j[kKeyName].get<std::string>();

    // Validate models array is present and non-empty.
    if (!j.contains(kKeyModels) || !j[kKeyModels].is_array() ||
        j[kKeyModels].empty()) {
        return utils::ErrorCode::kInvalidArgument;
    }

    // Parse each model entry.
    int idx = 0;
    for (const auto& model_json : j[kKeyModels]) {
        ModelConfig model;
        auto ret = ParseModelConfig(model_json, idx, &model);
        if (ret != utils::ErrorCode::kOk) return ret;
        config->models.push_back(std::move(model));
        ++idx;
    }

    // Enforce unique model ids.
    std::unordered_set<std::string> seen_ids;
    for (const auto& model : config->models) {
        if (!seen_ids.insert(model.id).second) {
            return utils::ErrorCode::kInvalidArgument;
        }
    }

    return utils::ErrorCode::kOk;
}

}  // namespace core
}  // namespace atlas
