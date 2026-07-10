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

    // Run preprocessing pipeline for the first input.
    utils::Tensor preprocessed;
    if (!entry_->input_pipelines.empty()) {
        auto ret = entry_->input_pipelines[0].Run(raw_input, &preprocessed);
        if (ret != utils::ErrorCode::kOk) return ret;
    } else {
        preprocessed.info      = raw_input.info;
        preprocessed.data      = raw_input.data;
        preprocessed.byte_size = raw_input.byte_size;
        preprocessed.owns_data = false;
    }

    std::vector<utils::Tensor> inputs;
    // Add batch dimension to shape if pipeline output is 3-D (C×H×W).
    if (preprocessed.info.shape.size() == 3) {
        preprocessed.info.shape.insert(
            preprocessed.info.shape.begin(), 1);
    }
    inputs.push_back(std::move(preprocessed));

    std::vector<utils::Tensor> raw_outputs;
    auto ret = entry_->backend->Infer(inputs, raw_outputs);
    if (ret != utils::ErrorCode::kOk) return ret;

    // Run output pipelines.
    outputs->clear();
    for (size_t i = 0; i < raw_outputs.size(); ++i) {
        if (i < entry_->output_pipelines.size() &&
            !entry_->output_pipelines[i].IsEmpty()) {
            utils::Tensor postprocessed;
            ret = entry_->output_pipelines[i].Run(raw_outputs[i], &postprocessed);
            if (ret != utils::ErrorCode::kOk) return ret;
            outputs->push_back(std::move(postprocessed));
        } else {
            outputs->push_back(std::move(raw_outputs[i]));
        }
    }
    return utils::ErrorCode::kOk;
}

utils::ErrorCode ModelHandle::Run(const std::vector<utils::Tensor>& raw_inputs,
                                   std::vector<utils::Tensor>* outputs) {
    if (outputs == nullptr) return utils::ErrorCode::kInvalidArgument;
    if (!IsValid())         return utils::ErrorCode::kNotInitialized;

    std::vector<utils::Tensor> preprocessed;
    preprocessed.reserve(raw_inputs.size());

    for (size_t i = 0; i < raw_inputs.size(); ++i) {
        utils::Tensor processed;
        if (i < entry_->input_pipelines.size() &&
            !entry_->input_pipelines[i].IsEmpty()) {
            auto ret = entry_->input_pipelines[i].Run(raw_inputs[i], &processed);
            if (ret != utils::ErrorCode::kOk) return ret;
        } else {
            processed.info      = raw_inputs[i].info;
            processed.data      = raw_inputs[i].data;
            processed.byte_size = raw_inputs[i].byte_size;
            processed.owns_data = false;
        }
        // Add batch dimension to shape if pipeline output is 3-D (C×H×W).
        if (processed.info.shape.size() == 3) {
            processed.info.shape.insert(
                processed.info.shape.begin(), 1);
        }
        preprocessed.push_back(std::move(processed));
    }

    std::vector<utils::Tensor> raw_outputs;
    auto ret = entry_->backend->Infer(preprocessed, raw_outputs);
    if (ret != utils::ErrorCode::kOk) return ret;

    // Run output pipelines.
    outputs->clear();
    for (size_t i = 0; i < raw_outputs.size(); ++i) {
        if (i < entry_->output_pipelines.size() &&
            !entry_->output_pipelines[i].IsEmpty()) {
            utils::Tensor postprocessed;
            ret = entry_->output_pipelines[i].Run(raw_outputs[i], &postprocessed);
            if (ret != utils::ErrorCode::kOk) return ret;
            outputs->push_back(std::move(postprocessed));
        } else {
            outputs->push_back(std::move(raw_outputs[i]));
        }
    }
    return utils::ErrorCode::kOk;
}

std::vector<utils::TensorInfo> ModelHandle::GetInputInfo() const {
    if (!IsValid()) return {};
    return entry_->backend->GetInputInfo();
}

std::vector<utils::TensorInfo> ModelHandle::GetOutputInfo() const {
    if (!IsValid()) return {};
    return entry_->backend->GetOutputInfo();
}

utils::TensorInfo ModelHandle::GetInputInfoAt(size_t index) const {
    if (!IsValid()) return {};
    return entry_->backend->GetInputInfoAt(index);
}

utils::TensorInfo ModelHandle::GetOutputInfoAt(size_t index) const {
    if (!IsValid()) return {};
    return entry_->backend->GetOutputInfoAt(index);
}

utils::Tensor ModelHandle::GetInputTensor(size_t index) const {
    if (!IsValid()) return {};
    auto buf = entry_->backend->GetInputBuffer(index);
    if (buf.data == nullptr || buf.size == 0) return {};

    utils::Tensor t;
    t.data      = buf.data;
    t.byte_size = buf.size;
    t.owns_data = false;
    t.info      = entry_->backend->GetInputInfoAt(index);
    return t;
}

utils::Tensor ModelHandle::GetOutputTensor(size_t index) const {
    if (!IsValid()) return {};
    auto buf = entry_->backend->GetOutputBuffer(index);
    if (buf.data == nullptr || buf.size == 0) return {};

    utils::Tensor t;
    t.data      = buf.data;
    t.byte_size = buf.size;
    t.owns_data = false;
    t.info      = entry_->backend->GetOutputInfoAt(index);
    return t;
}

utils::ErrorCode ModelHandle::SetInputBuffer(size_t index,
                                              void* external_mem,
                                              size_t byte_size) const {
    if (!IsValid()) return utils::ErrorCode::kNotInitialized;
    return entry_->backend->SetInputBuffer(index, external_mem, byte_size);
}

std::string ModelHandle::GetBackend() const {
    if (!IsValid()) return {};
    return entry_->config.backend;
}

std::string ModelHandle::GetModelPath() const {
    if (!IsValid()) return {};
    return entry_->config.model_path;
}

int ModelHandle::GetLoadStrategy() const {
    if (!IsValid()) return 0;
    return static_cast<int>(entry_->config.load_strategy);
}

std::unordered_map<std::string, std::string> ModelHandle::GetConfig() const {
    if (!IsValid()) return {};
    return entry_->config.config;
}

}  // namespace api
}  // namespace atlas
