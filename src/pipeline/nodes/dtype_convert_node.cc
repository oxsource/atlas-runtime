#include "src/pipeline/nodes/dtype_convert_node.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "src/utils/types.h"

namespace atlas {
namespace pipeline {

namespace {
constexpr std::string_view kNodeName = "DtypeConvert";
}  // namespace

DtypeConvertNode::DtypeConvertNode(utils::DataType target_dtype)
    : target_dtype_(target_dtype) {}

std::string_view DtypeConvertNode::Name() const { return kNodeName; }

utils::ErrorCode DtypeConvertNode::Process(const utils::Tensor& input,
                                            utils::Tensor* output) {
    if (output == nullptr) return utils::ErrorCode::kInvalidArgument;

    // If no conversion needed, shallow-copy metadata and reuse buffer.
    if (input.info.dtype == target_dtype_) {
        *output = utils::Tensor{};
        output->info      = input.info;
        output->data      = input.data;
        output->byte_size = input.byte_size;
        output->owns_data = false;
        return utils::ErrorCode::kOk;
    }

    // Only uint8 → float32 is supported in this implementation.
    if (input.info.dtype  != utils::DataType::kUInt8 ||
        target_dtype_     != utils::DataType::kFloat32) {
        return utils::ErrorCode::kInvalidArgument;
    }

    const size_t count = input.byte_size;  // 1 byte per uint8 element
    const size_t out_bytes = count * sizeof(float);

    output->info           = input.info;
    output->info.dtype     = target_dtype_;
    output->byte_size      = out_bytes;
    output->data           = malloc(out_bytes);
    output->owns_data      = true;

    if (output->data == nullptr) return utils::ErrorCode::kInvalidArgument;

    const uint8_t* src = static_cast<const uint8_t*>(input.data);
    float*         dst = static_cast<float*>(output->data);
    for (size_t i = 0; i < count; ++i) {
        dst[i] = static_cast<float>(src[i]);
    }
    return utils::ErrorCode::kOk;
}

}  // namespace pipeline
}  // namespace atlas
