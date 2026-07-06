#include "src/backend/snpe/snpe_backend.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

// SNPE 1.x headers use the zdl/ nesting layout.
// Types live in zdl::SNPE::, zdl::DlSystem::, zdl::DlContainer::
// (2.x dropped the zdl:: prefix, but the API is otherwise the same).
#include "DlContainer/IDlContainer.hpp"
#include "DlSystem/DlEnums.hpp"
#include "DlSystem/IBufferAttributes.hpp"
#include "DlSystem/ITensor.hpp"
#include "DlSystem/ITensorFactory.hpp"
#include "DlSystem/IUserBuffer.hpp"
#include "DlSystem/StringList.hpp"
#include "DlSystem/TensorMap.hpp"
#include "DlSystem/TensorShape.hpp"
#include "SNPE/SNPE.hpp"
#include "SNPE/SNPEBuilder.hpp"
#include "SNPE/SNPEFactory.hpp"

#include "src/backend/base/backend_factory.h"
#include "src/backend/snpe/snpe_backend_context.h"
#include "src/utils/types.h"

#define LOG_TAG "Atlas::SnpeBE_V1"
#include "src/utils/logger.h"

namespace atlas {
namespace backend {

// Define SnpeBackend::SnpeImpl (nested type matching header fwd declaration).
struct SnpeBackend::SnpeImpl {
    std::unique_ptr<zdl::DlContainer::IDlContainer> container;
    std::unique_ptr<zdl::SNPE::SNPE>                snpe;
    std::vector<std::string>                        input_names;
    std::vector<std::string>                        output_names;
};

namespace {

// Config key constants.
constexpr char kConfigRuntime[]    = "runtime";
constexpr char kConfigPerfProfile[] = "performance_profile";
constexpr char kConfigUseBuffer[]  = "use_buffer";

// Runtime name strings accepted from manifest config.
constexpr char kRuntimeCpu[] = "cpu";
constexpr char kRuntimeGpu[] = "gpu";
constexpr char kRuntimeDsp[] = "dsp";
constexpr char kRuntimeAip[] = "aip";

// Performance profile name strings — 1.50.0 subset
// (LOW_BALANCED is the last; no EXTREME_POWER_SAVER in 1.x).
constexpr char kPerfDefault[]           = "default";
constexpr char kPerfBalanced[]          = "balanced";
constexpr char kPerfHighPerformance[]   = "high_performance";
constexpr char kPerfPowerSaver[]        = "power_saver";
constexpr char kPerfSystemSettings[]    = "system_settings";
constexpr char kPerfSustainedHighPerf[] = "sustained_high_performance";
constexpr char kPerfBurst[]             = "burst";
constexpr char kPerfLowPowerSaver[]     = "low_power_saver";
constexpr char kPerfHighPowerSaver[]    = "high_power_saver";
constexpr char kPerfLowBalanced[]       = "low_balanced";

constexpr int kRank3 = 3;
constexpr int kRank4 = 4;
constexpr size_t kSingleChannel = 1;
constexpr size_t kRgbChannels = 3;
constexpr size_t kRgbaChannels = 4;

std::string InferLayoutFromShape(const zdl::DlSystem::TensorShape& shape) {
    const size_t* dims = shape.getDimensions();
    const size_t rank = shape.rank();
    if (rank == kRank4) {
        const size_t second_dim = dims[1];
        const size_t last_dim = dims[3];
        const bool second_dim_is_channel =
            second_dim == kSingleChannel || second_dim == kRgbChannels ||
            second_dim == kRgbaChannels;
        const bool last_dim_is_channel =
            last_dim == kSingleChannel || last_dim == kRgbChannels ||
            last_dim == kRgbaChannels;
        if (last_dim_is_channel && !second_dim_is_channel) {
            return "NHWC";
        }
        if (second_dim_is_channel && !last_dim_is_channel) {
            return "NCHW";
        }
    }
    if (rank == kRank3) {
        const size_t first_dim = dims[0];
        const size_t last_dim = dims[2];
        const bool first_dim_is_channel =
            first_dim == kSingleChannel || first_dim == kRgbChannels ||
            first_dim == kRgbaChannels;
        const bool last_dim_is_channel =
            last_dim == kSingleChannel || last_dim == kRgbChannels ||
            last_dim == kRgbaChannels;
        if (last_dim_is_channel && !first_dim_is_channel) {
            return "HWC";
        }
        if (first_dim_is_channel && !last_dim_is_channel) {
            return "CHW";
        }
    }
    return "NCHW";
}

utils::DataType ParseDataType(
    zdl::DlSystem::UserBufferEncoding::ElementType_t encoding_type) {
    switch (encoding_type) {
        case zdl::DlSystem::UserBufferEncoding::ElementType_t::FLOAT:
            return utils::DataType::kFloat32;
        case zdl::DlSystem::UserBufferEncoding::ElementType_t::TF8:
            return utils::DataType::kInt8;
        case zdl::DlSystem::UserBufferEncoding::ElementType_t::UNSIGNED8BIT:
        case zdl::DlSystem::UserBufferEncoding::ElementType_t::TF16:
            return utils::DataType::kUInt8;
        default:
            return utils::DataType::kUnknown;
    }
}

int ParseRuntime(const std::string& runtime_str) {
    if (runtime_str == kRuntimeCpu) {
        return static_cast<int>(zdl::DlSystem::Runtime_t::CPU_FLOAT32);
    }
    if (runtime_str == kRuntimeGpu) {
        return static_cast<int>(
            zdl::DlSystem::Runtime_t::GPU_FLOAT32_16_HYBRID);
    }
    if (runtime_str == kRuntimeDsp) {
        return static_cast<int>(zdl::DlSystem::Runtime_t::DSP_FIXED8_TF);
    }
    if (runtime_str == kRuntimeAip) {
        return static_cast<int>(zdl::DlSystem::Runtime_t::AIP_FIXED8_TF);
    }
    return static_cast<int>(
        zdl::DlSystem::Runtime_t::GPU_FLOAT32_16_HYBRID);
}

int ParsePerformanceProfile(const std::string& profile_str) {
    if (profile_str == kPerfDefault || profile_str == kPerfBalanced) {
        return static_cast<int>(
            zdl::DlSystem::PerformanceProfile_t::BALANCED);
    }
    if (profile_str == kPerfHighPerformance) {
        return static_cast<int>(
            zdl::DlSystem::PerformanceProfile_t::HIGH_PERFORMANCE);
    }
    if (profile_str == kPerfPowerSaver) {
        return static_cast<int>(
            zdl::DlSystem::PerformanceProfile_t::POWER_SAVER);
    }
    if (profile_str == kPerfSystemSettings) {
        return static_cast<int>(
            zdl::DlSystem::PerformanceProfile_t::SYSTEM_SETTINGS);
    }
    if (profile_str == kPerfSustainedHighPerf) {
        return static_cast<int>(
            zdl::DlSystem::PerformanceProfile_t::
                SUSTAINED_HIGH_PERFORMANCE);
    }
    if (profile_str == kPerfBurst) {
        return static_cast<int>(
            zdl::DlSystem::PerformanceProfile_t::BURST);
    }
    if (profile_str == kPerfLowPowerSaver) {
        return static_cast<int>(
            zdl::DlSystem::PerformanceProfile_t::LOW_POWER_SAVER);
    }
    if (profile_str == kPerfHighPowerSaver) {
        return static_cast<int>(
            zdl::DlSystem::PerformanceProfile_t::HIGH_POWER_SAVER);
    }
    if (profile_str == kPerfLowBalanced) {
        return static_cast<int>(
            zdl::DlSystem::PerformanceProfile_t::LOW_BALANCED);
    }
    return static_cast<int>(
        zdl::DlSystem::PerformanceProfile_t::BALANCED);
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

SnpeBackend::SnpeBackend() : impl_(std::make_unique<SnpeImpl>()) {}

SnpeBackend::~SnpeBackend() { Unload(); }

// ---------------------------------------------------------------------------
// IBackend interface
// ---------------------------------------------------------------------------

utils::ErrorCode SnpeBackend::Load(const std::string& model_path,
                                    const core::ModelConfig& config,
                                    IBackendContext* ctx) {
    ATLAS_LOGD("%s called, model_path=%s", __FUNCTION__, model_path.c_str());
    Unload();

    // 1. Ensure shared context is initialized (idempotent).
    if (ctx != nullptr) {
        active_ctx_ = static_cast<SnpeBackendContext*>(ctx);
        auto ret = active_ctx_->Init(config.config);
        if (ret != utils::ErrorCode::kOk) {
            ATLAS_LOGE("SnpeBackendContext::Init failed (error=%d)", static_cast<int>(ret));
            return ret;
        }
    }

    // 2. Extract per-model parameters from config.
    zdl::DlSystem::Runtime_t runtime =
        zdl::DlSystem::Runtime_t::GPU_FLOAT32_16_HYBRID;
    {
        auto it = config.config.find(kConfigRuntime);
        if (it != config.config.end()) {
            runtime = static_cast<zdl::DlSystem::Runtime_t>(
                ParseRuntime(it->second));
        }
    }

    auto perf_profile = zdl::DlSystem::PerformanceProfile_t::BALANCED;
    {
        auto it = config.config.find(kConfigPerfProfile);
        if (it != config.config.end()) {
            perf_profile = static_cast<zdl::DlSystem::PerformanceProfile_t>(
                ParsePerformanceProfile(it->second));
        }
    }

    int use_buffer = 0;
    {
        auto it = config.config.find(kConfigUseBuffer);
        if (it != config.config.end() && it->second == "true") {
            use_buffer = 1;
        }
    }

    // 3. Open the .dlc container.
    impl_->container = zdl::DlContainer::IDlContainer::open(model_path);
    if (impl_->container == nullptr) {
        ATLAS_LOGE("Failed to open DLC container: %s", model_path.c_str());
        Unload();
        return utils::ErrorCode::kFileNotFound;
    }

    // 4. Build the runtime list and construct the SNPE network.
    zdl::DlSystem::RuntimeList runtime_list;
    runtime_list.add(runtime);

    zdl::DlSystem::PlatformConfig platform_config;

    zdl::SNPE::SNPEBuilder builder(impl_->container.get());
    builder.setRuntimeProcessorOrder(runtime_list);
    builder.setPerformanceProfile(perf_profile);
    builder.setUseUserSuppliedBuffers(use_buffer);
    builder.setPlatformConfig(platform_config);

    impl_->snpe = builder.build();
    if (impl_->snpe == nullptr) {
        ATLAS_LOGE("SNPE builder.build() failed for model: %s", model_path.c_str());
        Unload();
        return utils::ErrorCode::kInferFailed;
    }

    // 5. Extract tensor names.
    {
        auto opt_in_names = impl_->snpe->getInputTensorNames();
        if (!opt_in_names) {
            ATLAS_LOGE("Failed to get input tensor names from SNPE model");
            Unload();
            return utils::ErrorCode::kInferFailed;
        }
        impl_->input_names.clear();
        for (const auto& name : *opt_in_names) {
            impl_->input_names.emplace_back(
                static_cast<const std::string&>(name));
        }
    }
    {
        auto opt_out_names = impl_->snpe->getOutputTensorNames();
        if (!opt_out_names) {
            ATLAS_LOGE("Failed to get output tensor names from SNPE model");
            Unload();
            return utils::ErrorCode::kInferFailed;
        }
        impl_->output_names.clear();
        for (const auto& name : *opt_out_names) {
            impl_->output_names.emplace_back(
                static_cast<const std::string&>(name));
        }
    }

    // 6. Build tensor info.
    auto ret = BuildTensorInfos();
    if (ret != utils::ErrorCode::kOk) {
        ATLAS_LOGE("BuildTensorInfos failed (error=%d)", static_cast<int>(ret));
        Unload();
        return ret;
    }

    loaded_ = true;
    return utils::ErrorCode::kOk;
}

utils::ErrorCode SnpeBackend::Infer(const std::vector<utils::Tensor>& inputs,
                                     std::vector<utils::Tensor>& outputs) {
    ATLAS_LOGD("%s called, num_inputs=%zu", __FUNCTION__, inputs.size());
    if (!loaded_) {
        ATLAS_LOGE("Infer called before Load");
        return utils::ErrorCode::kNotInitialized;
    }
    if (inputs.size() != input_info_.size()) {
        ATLAS_LOGE("Input count mismatch: expected %zu, got %zu",
                   input_info_.size(), inputs.size());
        return utils::ErrorCode::kInvalidArgument;
    }

    auto& tensor_factory = zdl::SNPE::SNPEFactory::getTensorFactory();

    zdl::DlSystem::TensorMap input_map;
    std::vector<std::unique_ptr<zdl::DlSystem::ITensor>> input_tensors;
    input_tensors.reserve(inputs.size());
    for (size_t i = 0; i < inputs.size(); ++i) {
        const utils::Tensor& t = inputs[i];
        const utils::TensorInfo& info = input_info_[i];

        std::vector<size_t> shape;
        shape.reserve(info.shape.size());
        for (int d : info.shape) {
            shape.push_back(static_cast<size_t>(d > 0 ? d : 1));
        }

        zdl::DlSystem::TensorShape tensor_shape(shape);
        std::unique_ptr<zdl::DlSystem::ITensor> itensor;
        if (info.dtype == utils::DataType::kFloat32 &&
            t.data != nullptr &&
            t.byte_size % sizeof(float) == 0) {
            itensor = tensor_factory.createTensor(tensor_shape);
            if (itensor == nullptr) {
                ATLAS_LOGE("Failed to create input ITensor[%zu]: %s", i,
                           impl_->input_names[i].c_str());
                return utils::ErrorCode::kInferFailed;
            }

            const float* input_data = static_cast<const float*>(t.data);
            const size_t input_count = t.byte_size / sizeof(float);
            if (itensor->getSize() < input_count) {
                ATLAS_LOGE(
                    "Input ITensor[%zu] capacity too small: capacity %zu, need %zu",
                    i, itensor->getSize(), input_count);
                return utils::ErrorCode::kInvalidArgument;
            }
            std::copy(input_data, input_data + input_count, itensor->begin());
        } else {
            itensor = tensor_factory.createTensor(
                tensor_shape,
                static_cast<const unsigned char*>(t.data),
                t.byte_size);
        }
        if (itensor == nullptr) {
            ATLAS_LOGE("Failed to create input ITensor[%zu]: %s", i,
                       impl_->input_names[i].c_str());
            return utils::ErrorCode::kInferFailed;
        }

        if (inputs.size() > 1) {
            input_map.add(impl_->input_names[i].c_str(), itensor.get());
        }
        input_tensors.push_back(std::move(itensor));
    }

    zdl::DlSystem::TensorMap output_map;
    if (output_info_.size() != impl_->output_names.size()) {
        ATLAS_LOGE("Output metadata mismatch: expected %zu infos, got %zu names",
                   output_info_.size(), impl_->output_names.size());
        return utils::ErrorCode::kInferFailed;
    }

    const bool execute_ok =
        inputs.size() == 1
            ? impl_->snpe->execute(input_tensors[0].get(), output_map)
            : impl_->snpe->execute(input_map, output_map);
    if (!execute_ok) {
        ATLAS_LOGE("SNPE execute failed");
        return utils::ErrorCode::kInferFailed;
    }

    outputs.clear();
    outputs.reserve(impl_->output_names.size());
    for (size_t i = 0; i < impl_->output_names.size(); ++i) {
        zdl::DlSystem::ITensor* out_itensor =
            output_map.getTensor(impl_->output_names[i].c_str());
        if (out_itensor == nullptr) {
            ATLAS_LOGE("Failed to get output tensor[%zu]: %s", i,
                       impl_->output_names[i].c_str());
            return utils::ErrorCode::kInferFailed;
        }

        const utils::TensorInfo& info = output_info_[i];
        size_t elem_size  = utils::ElementByteSize(info.dtype);
        size_t byte_size  = out_itensor->getSize() * elem_size;

        utils::Tensor out;
        out.info      = info;
        out.byte_size = byte_size;
        out.data      = malloc(byte_size);
        out.owns_data = true;

        auto raw_ptr = out_itensor->cbegin();
        std::memcpy(out.data, &(*raw_ptr), byte_size);
        outputs.push_back(std::move(out));
    }

    return utils::ErrorCode::kOk;
}

std::vector<utils::TensorInfo> SnpeBackend::GetInputInfo() const {
    return input_info_;
}

std::vector<utils::TensorInfo> SnpeBackend::GetOutputInfo() const {
    return output_info_;
}

void SnpeBackend::Unload() {
    impl_->snpe.reset();
    impl_->container.reset();
    impl_->input_names.clear();
    impl_->output_names.clear();
    input_info_.clear();
    output_info_.clear();
    active_ctx_ = nullptr;
    loaded_ = false;
}

bool SnpeBackend::IsLoaded() const { return loaded_; }

std::string SnpeBackend::Version() const {
    return std::string(zdl::SNPE::SNPEFactory::getLibraryVersion().toString());
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

utils::ErrorCode SnpeBackend::BuildTensorInfos() {
    ATLAS_LOGD("%s called", __FUNCTION__);
    input_info_.clear();
    output_info_.clear();

    for (const auto& name : impl_->input_names) {
        auto opt_attrs = impl_->snpe->getInputOutputBufferAttributes(name.c_str());
        if (!opt_attrs || *opt_attrs == nullptr) {
            ATLAS_LOGE("Failed to get input buffer attributes for tensor: %s",
                       name.c_str());
            return utils::ErrorCode::kInferFailed;
        }

        auto opt_shape = impl_->snpe->getInputDimensions(name.c_str());
        if (!opt_shape) {
            ATLAS_LOGE("Failed to get input dimensions for tensor: %s", name.c_str());
            return utils::ErrorCode::kInferFailed;
        }

        const auto& shape = *opt_shape;
        utils::TensorInfo info;
        info.name = name;
        const size_t* dims = shape.getDimensions();
        for (size_t r = 0; r < shape.rank(); ++r) {
            info.shape.push_back(static_cast<int>(dims[r]));
        }
        info.layout = InferLayoutFromShape(shape);
        info.dtype = ParseDataType((*opt_attrs)->getEncodingType());
        if (info.dtype == utils::DataType::kUnknown) {
            ATLAS_LOGE("Unsupported input dtype for tensor: %s", name.c_str());
            return utils::ErrorCode::kInferFailed;
        }
        input_info_.push_back(std::move(info));
    }

    for (const auto& name : impl_->output_names) {
        auto opt_attrs = impl_->snpe->getInputOutputBufferAttributes(name.c_str());
        if (!opt_attrs || *opt_attrs == nullptr) {
            ATLAS_LOGE("Failed to get output buffer attributes for tensor: %s",
                       name.c_str());
            return utils::ErrorCode::kInferFailed;
        }

        const auto shape = (*opt_attrs)->getDims();
        utils::TensorInfo info;
        info.name = name;
        const size_t* dims = shape.getDimensions();
        for (size_t r = 0; r < shape.rank(); ++r) {
            info.shape.push_back(static_cast<int>(dims[r]));
        }
        info.layout = InferLayoutFromShape(shape);
        info.dtype = ParseDataType((*opt_attrs)->getEncodingType());
        if (info.dtype == utils::DataType::kUnknown) {
            ATLAS_LOGE("Unsupported output dtype for tensor: %s", name.c_str());
            return utils::ErrorCode::kInferFailed;
        }
        output_info_.push_back(std::move(info));
    }

    return utils::ErrorCode::kOk;
}

}  // namespace backend
}  // namespace atlas

ATLAS_REGISTER_BACKEND("snpe", atlas::backend::SnpeBackend)