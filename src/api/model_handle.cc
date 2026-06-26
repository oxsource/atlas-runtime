#include "src/api/model_handle.h"

#include <vector>

#include "src/core/model_manager.h"
#include "src/utils/types.h"

namespace atlas {
namespace api {

ModelHandle::ModelHandle(core::ModelEntry* entry) : entry_(entry) {}

bool ModelHandle::IsValid() const {
    return entry_ != nullptr && entry_->loaded;
}

utils::ErrorCode ModelHandle::Run(const utils::Tensor& raw_input,
                                   std::vector<utils::Tensor>* outputs) {
    if (outputs == nullptr) return utils::ErrorCode::kInvalidArgument;
    if (!IsValid())         return utils::ErrorCode::kNotInitialized;

    // Step 1: run preprocessing pipeline.
    utils::Tensor preprocessed;
    auto ret = entry_->pipeline.Run(raw_input, &preprocessed);
    if (ret != utils::ErrorCode::kOk) return ret;

    // Step 2: prepare inputs vector and run inference.
    std::vector<utils::Tensor> inputs;
    // Add batch dimension to shape if pipeline output is 3-D (C×H×W).
    if (preprocessed.info.shape.size() == 3) {
        preprocessed.info.shape.insert(
            preprocessed.info.shape.begin(), 1);
    }
    inputs.push_back(std::move(preprocessed));

    return entry_->backend->Infer(inputs, *outputs);
}

std::vector<utils::TensorInfo> ModelHandle::GetInputInfo() const {
    if (!IsValid()) return {};
    return entry_->backend->GetInputInfo();
}

std::vector<utils::TensorInfo> ModelHandle::GetOutputInfo() const {
    if (!IsValid()) return {};
    return entry_->backend->GetOutputInfo();
}

}  // namespace api
}  // namespace atlas
