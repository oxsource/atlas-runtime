#include "src/pipeline/nodes/normalize_node.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "src/pipeline/pipeline_node_factory.h"
#include "src/utils/types.h"

namespace {
constexpr std::string_view kNodeName = "atlas::normalize";
}  // namespace

namespace atlas {
namespace pipeline {

namespace {
constexpr float kInvScale255 = 1.0f / 255.0f;
}  // namespace

NormalizeNode::NormalizeNode(const std::vector<float>& mean,
                               const std::vector<float>& std)
    : mean_(mean), std_(std) {}

std::string_view NormalizeNode::Name() const { return kNodeName; }

utils::ErrorCode NormalizeNode::Process(const Context& ctx,
                                       const utils::Tensor& input, utils::Tensor* output) {
    if (output == nullptr) return utils::ErrorCode::kInvalidArgument;
    if (input.info.dtype != utils::DataType::kFloat32) {
        return utils::ErrorCode::kInvalidArgument;
    }
    // Expects CHW layout: shape = {C, H, W}.
    if (input.info.shape.size() != 3) return utils::ErrorCode::kInvalidArgument;

    const int c  = input.info.shape[0];
    const int hw = input.info.shape[1] * input.info.shape[2];

    if (static_cast<int>(mean_.size()) != c ||
        static_cast<int>(std_.size())  != c) {
        return utils::ErrorCode::kInvalidArgument;
    }

    // Allocate output (same shape and size as input).
    output->info      = input.info;
    output->byte_size = input.byte_size;
    output->data      = malloc(input.byte_size);
    output->owns_data = true;

    if (output->data == nullptr) return utils::ErrorCode::kInvalidArgument;

    const float* src = static_cast<const float*>(input.data);
    float*       dst = static_cast<float*>(output->data);

    for (int ci = 0; ci < c; ++ci) {
        const float inv_std = 1.0f / std_[ci];
        for (int i = 0; i < hw; ++i) {
            const int idx = ci * hw + i;
            dst[idx] = (src[idx] * kInvScale255 - mean_[ci]) * inv_std;
        }
    }
    return utils::ErrorCode::kOk;
}

namespace {

// Parses a comma-separated list of floats from |s| into |out|.
// Uses C strtof instead of std::stringstream + std::stof to avoid
// locale-dependent static initialization issues on Android.
//
// Platform note: std::stringstream constructs a std::locale internally,
// whose static initialization may fail with std::bad_cast on Android NDK
// due to undefined static initialization order across translation units.
// C strtof has no such dependency and is portable across all platforms.
//
// Returns false on parse error.
bool ParseFloatList(const std::string& s, std::vector<float>* out) {
    out->clear();
    if (s.empty()) return false;
    const char* p = s.c_str();
    const char* const end = p + s.size();
    while (p < end) {
        char* next = nullptr;
        const float val = std::strtof(p, &next);
        if (next == p) return false;  // no digits parsed
        out->push_back(val);
        p = next;
        if (*p == ',') ++p;
        // Skip any trailing whitespace after comma for robustness.
        while (*p == ' ') ++p;
    }
    return !out->empty();
}

}  // namespace

// static
std::unique_ptr<IPipelineNode> NormalizeNode::CreateFromParams(
    const std::unordered_map<std::string, std::string>& params) {
    auto mean_it = params.find("mean");
    auto std_it  = params.find("std");
    if (mean_it == params.end() || std_it == params.end()) return nullptr;

    std::vector<float> mean;
    std::vector<float> std_dev;
    if (!ParseFloatList(mean_it->second, &mean)) return nullptr;
    if (!ParseFloatList(std_it->second, &std_dev)) return nullptr;
    if (mean.size() != std_dev.size()) return nullptr;

    return std::make_unique<NormalizeNode>(mean, std_dev);
}

}  // namespace pipeline
}  // namespace atlas

ATLAS_REGISTER_PIPELINE_NODE(kNodeName, atlas::pipeline::NormalizeNode)

