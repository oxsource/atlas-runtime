#include "src/core/manifest_parser.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "nlohmann/json.hpp"

#include "src/core/manifest_config.h"
#include "src/utils/types.h"

#define LOG_TAG "Atlas::ManifestParser"
#include "src/utils/logger.h"

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

// Pipeline-level keys (optional "pipeline" array on inputs/outputs).
constexpr const char* kKeyPipeline         = "pipeline";
constexpr const char* kKeyDisablePipeline  = "disable_pipeline";
constexpr const char* kKeyParams           = "params";

// ---------------------------------------------------------------------------
// Helper functions
// ---------------------------------------------------------------------------

// Splits a "major.minor" version string.  Returns false on malformed input.
//
// Uses std::sscanf instead of std::istringstream to avoid locale-related
// static initialization issues on Android NDK. See BUG-003.
//
// Uses C stdio (fopen/fread) instead of std::ifstream for the same reason:
// std::ifstream construction triggers locale initialization that may fail
// on Android NDK at runtime. See BUG-003.
bool ParseVersionString(const std::string& version, int* major, int* minor) {
    return std::sscanf(version.c_str(), "%d.%d", major, minor) == 2;
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

// Parses a single ManifestTensorInfo object from |j|.
utils::ErrorCode ParseTensorInfo(const nlohmann::json& j,
                                  ManifestTensorInfo* info) {
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

    // Optional: when true, skip automatic BuildInputPipeline.
    if (j.contains(kKeyDisablePipeline) && j[kKeyDisablePipeline].is_boolean()) {
        info->disable_pipeline = j[kKeyDisablePipeline].get<bool>();
    }

    // Optional: explicit pipeline node chain.  Empty = auto-build.
    if (j.contains(kKeyPipeline) && j[kKeyPipeline].is_array()) {
        std::unordered_set<std::string> seen_names;
        for (const auto& node_json : j[kKeyPipeline]) {
            ManifestPipelineNode node;
            if (!node_json.contains(kKeyName) || !node_json[kKeyName].is_string()) {
                return utils::ErrorCode::kParseError;
            }
            node.name = node_json[kKeyName].get<std::string>();
            if (!seen_names.insert(node.name).second) {
                return utils::ErrorCode::kParseError;  // Duplicate name
            }
            if (node_json.contains(kKeyParams) && node_json[kKeyParams].is_object()) {
                for (const auto& [k, v] : node_json[kKeyParams].items()) {
                    if (v.is_string()) {
                        node.params[k] = v.get<std::string>();
                    } else if (v.is_number_integer()) {
                        node.params[k] = std::to_string(v.get<int64_t>());
                    } else if (v.is_number_float()) {
                        node.params[k] = std::to_string(v.get<double>());
                    } else if (v.is_boolean()) {
                        node.params[k] = v.get<bool>() ? "true" : "false";
                    }
                }
            }
            info->pipeline.push_back(std::move(node));
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
        ManifestTensorInfo info;
        auto ret = ParseTensorInfo(input_json, &info);
        if (ret != utils::ErrorCode::kOk) return ret;
        model->inputs.push_back(std::move(info));
    }

    if (!j.contains(kKeyOutputs) || !j[kKeyOutputs].is_array()) {
        return utils::ErrorCode::kParseError;
    }
    for (const auto& output_json : j[kKeyOutputs]) {
        ManifestTensorInfo info;
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
    ATLAS_LOGD("parsing manifest: %s", path.c_str());

    if (config == nullptr) {
        ATLAS_LOGE("config is nullptr");
        return utils::ErrorCode::kInvalidArgument;
    }

    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        ATLAS_LOGE("failed to open file: %s", path.c_str());
        return utils::ErrorCode::kFileNotFound;
    }
    ATLAS_LOGD("file opened, parsing JSON");

    // Read entire file into a string, then parse from string.
    // Uses C stdio instead of std::ifstream to avoid locale-dependent
    // runtime initialization issues on Android NDK. See BUG-003.
    std::fseek(file, 0, SEEK_END);
    const long file_size = std::ftell(file);
    std::rewind(file);

    std::string content(static_cast<size_t>(file_size), '\0');
    const size_t read_bytes = std::fread(content.data(), 1,
                                          static_cast<size_t>(file_size), file);
    std::fclose(file);

    if (static_cast<long>(read_bytes) != file_size) {
        ATLAS_LOGE("failed to read file: %s", path.c_str());
        return utils::ErrorCode::kFileNotFound;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(content);
    } catch (const nlohmann::json::parse_error&) {
        ATLAS_LOGE("JSON parse error in: %s", path.c_str());
        return utils::ErrorCode::kParseError;
    }

    // Validate and parse version.
    if (!j.contains(kKeyVersion) || !j[kKeyVersion].is_string()) {
        ATLAS_LOGE("missing or invalid 'version' field");
        return utils::ErrorCode::kParseError;
    }
    config->version = j[kKeyVersion].get<std::string>();

    int major = 0;
    int minor = 0;
    if (!ParseVersionString(config->version, &major, &minor)) {
        ATLAS_LOGE("malformed version string: %s", config->version.c_str());
        return utils::ErrorCode::kParseError;
    }
    if (major != kSupportedMajorVersion) {
        ATLAS_LOGE("version mismatch: got %d.%d, supported major %d",
                   major, minor, kSupportedMajorVersion);
        return utils::ErrorCode::kVersionMismatch;
    }
    ATLAS_LOGD("manifest version: %d.%d", major, minor);

    // Validate and parse name.
    if (!j.contains(kKeyName) || !j[kKeyName].is_string()) {
        ATLAS_LOGE("missing or invalid 'name' field");
        return utils::ErrorCode::kParseError;
    }
    config->name = j[kKeyName].get<std::string>();
    ATLAS_LOGD("manifest name: %s", config->name.c_str());

    // Validate models array is present and non-empty.
    if (!j.contains(kKeyModels) || !j[kKeyModels].is_array() ||
        j[kKeyModels].empty()) {
        ATLAS_LOGE("missing or empty 'models' array");
        return utils::ErrorCode::kInvalidArgument;
    }

    // Parse each model entry.
    int idx = 0;
    for (const auto& model_json : j[kKeyModels]) {
        ModelConfig model;
        auto ret = ParseModelConfig(model_json, idx, &model);
        if (ret != utils::ErrorCode::kOk) {
            ATLAS_LOGE("failed to parse model entry at index %d (error=%d)",
                       idx, static_cast<int>(ret));
            return ret;
        }
        ATLAS_LOGD("model[%d]: id=%s, backend=%s, path=%s, inputs=%zu, outputs=%zu",
                   idx, model.id.c_str(), model.backend.c_str(),
                   model.model_path.c_str(),
                   model.inputs.size(), model.outputs.size());
        config->models.push_back(std::move(model));
        ++idx;
    }

    // Enforce unique model ids.
    std::unordered_set<std::string> seen_ids;
    for (const auto& model : config->models) {
        if (!seen_ids.insert(model.id).second) {
            ATLAS_LOGE("duplicate model id: %s", model.id.c_str());
            return utils::ErrorCode::kInvalidArgument;
        }
    }

    ATLAS_LOGD("parse complete: %zu model(s) loaded from '%s'",
               config->models.size(), config->name.c_str());
    return utils::ErrorCode::kOk;
}

}  // namespace core
}  // namespace atlas
