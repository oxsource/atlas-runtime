#include "src/backend/snpe/snpe_backend.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

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
#include "src/backend/snpe/snpe_aligned_buffer.h"
#include "src/backend/snpe/snpe_backend_context.h"
#include "src/backend/snpe/snpe_memory_pool.h"
#include "src/utils/types.h"

#define LOG_TAG "Atlas::SnpeBE_V2"
#include "src/utils/logger.h"

namespace atlas {
namespace backend {

// Define nested type matching header fwd declaration.
struct SnpeBackend::SnpeImpl {
    std::unique_ptr<DlContainer::IDlContainer> container;
    std::unique_ptr<SNPE::SNPE>                snpe;
    std::vector<std::string>                   input_names;
    std::vector<std::string>                   output_names;

    // ── ITensor path fields (use_buffer == false) ──
    std::vector<std::unique_ptr<DlSystem::ITensor>> input_tensors;
    DlSystem::TensorMap                             output_map;

    // ── UserBuffer path fields (use_buffer == true) ──
    bool use_buffer = false;
    std::vector<std::unique_ptr<DlSystem::IUserBuffer>> user_input_buffers;
    std::vector<std::unique_ptr<DlSystem::IUserBuffer>> user_output_buffers;
    std::vector<std::unique_ptr<DlSystem::UserBufferEncoding>>
        user_input_encodings;
    std::vector<std::unique_ptr<DlSystem::UserBufferEncoding>>
        user_output_encodings;
    std::vector<AlignedBuffer> user_input_raw;
    std::vector<AlignedBuffer> user_output_raw;
    std::vector<void*> user_input_external;
    std::vector<size_t> input_buffer_stride;
    std::vector<size_t> output_buffer_stride;

    // ── Shared buffer support (use_buffer == true, optional) ──
    // When non-empty, input backing memory is owned by SnpeMemoryPool
    // in the shared context.  user_input_raw[i] holds a non-owning
    // reference (data pointer borrowed from the pool).
    std::string shared_input_key;
};

namespace {

// Config key constants.
constexpr char kConfigRuntime[]    = "runtime";
constexpr char kConfigPerfProfile[] = "performance_profile";
constexpr char kConfigUseBuffer[]  = "use_buffer";
constexpr char kConfigSharedInput[] = "shared_input";

// Runtime name strings accepted from manifest config.
constexpr char kRuntimeCpu[] = "cpu";
constexpr char kRuntimeGpu[] = "gpu";
constexpr char kRuntimeDsp[] = "dsp";

// Performance profile name strings accepted from manifest config.
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
constexpr char kPerfExtremePowerSaver[] = "extreme_power_saver";

constexpr int kRank3 = 3;
constexpr int kRank4 = 4;
constexpr size_t kSingleChannel = 1;
constexpr size_t kRgbChannels = 3;
constexpr size_t kRgbaChannels = 4;

std::string InferLayoutFromShape(const DlSystem::TensorShape& shape) {
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
    DlSystem::UserBufferEncoding::ElementType_t encoding_type) {
    switch (encoding_type) {
        case DlSystem::UserBufferEncoding::ElementType_t::FLOAT:
            return utils::DataType::kFloat32;
        case DlSystem::UserBufferEncoding::ElementType_t::FLOAT16:
            return utils::DataType::kFloat16;
        case DlSystem::UserBufferEncoding::ElementType_t::INT8:
            return utils::DataType::kInt8;
        case DlSystem::UserBufferEncoding::ElementType_t::UINT8:
        case DlSystem::UserBufferEncoding::ElementType_t::UNSIGNED8BIT:
            return utils::DataType::kUInt8;
        case DlSystem::UserBufferEncoding::ElementType_t::INT32:
            return utils::DataType::kInt32;
        default:
            return utils::DataType::kUnknown;
    }
}

int ParseRuntime(const std::string& runtime_str) {
    if (runtime_str == kRuntimeCpu) {
        return static_cast<int>(DlSystem::Runtime_t::CPU);
    }
    if (runtime_str == kRuntimeGpu) {
        return static_cast<int>(DlSystem::Runtime_t::GPU);
    }
    if (runtime_str == kRuntimeDsp) {
        return static_cast<int>(DlSystem::Runtime_t::DSP);
    }
    return static_cast<int>(DlSystem::Runtime_t::GPU);
}

int ParsePerformanceProfile(const std::string& profile_str) {
    if (profile_str == kPerfDefault || profile_str == kPerfBalanced) {
        return static_cast<int>(DlSystem::PerformanceProfile_t::BALANCED);
    }
    if (profile_str == kPerfHighPerformance) {
        return static_cast<int>(DlSystem::PerformanceProfile_t::HIGH_PERFORMANCE);
    }
    if (profile_str == kPerfPowerSaver) {
        return static_cast<int>(DlSystem::PerformanceProfile_t::POWER_SAVER);
    }
    if (profile_str == kPerfSystemSettings) {
        return static_cast<int>(DlSystem::PerformanceProfile_t::SYSTEM_SETTINGS);
    }
    if (profile_str == kPerfSustainedHighPerf) {
        return static_cast<int>(DlSystem::PerformanceProfile_t::SUSTAINED_HIGH_PERFORMANCE);
    }
    if (profile_str == kPerfBurst) {
        return static_cast<int>(DlSystem::PerformanceProfile_t::BURST);
    }
    if (profile_str == kPerfLowPowerSaver) {
        return static_cast<int>(DlSystem::PerformanceProfile_t::LOW_POWER_SAVER);
    }
    if (profile_str == kPerfHighPowerSaver) {
        return static_cast<int>(DlSystem::PerformanceProfile_t::HIGH_POWER_SAVER);
    }
    if (profile_str == kPerfLowBalanced) {
        return static_cast<int>(DlSystem::PerformanceProfile_t::LOW_BALANCED);
    }
    if (profile_str == kPerfExtremePowerSaver) {
        return static_cast<int>(DlSystem::PerformanceProfile_t::EXTREME_POWER_SAVER);
    }
    return static_cast<int>(DlSystem::PerformanceProfile_t::BALANCED);
}

// Aligned memory alignment constant for DSP/HTP buffers.
constexpr size_t kBufferAlignment = 128;

// Computes per-dimension byte strides for a UserBuffer.
std::vector<size_t> ComputeUserBufferStride(const std::vector<int>& shape,
                                             size_t element_size) {
    const size_t rank = shape.size();
    if (rank == 0) return {};
    std::vector<size_t> stride(rank, element_size);
    for (size_t i = rank; i > 1; --i) {
        stride[i - 2] = stride[i - 1] *
            static_cast<size_t>(std::max(shape[i - 1], 1));
    }
    return stride;
}

// Adapts uint8_t input data to int8_t (TF8) by subtracting 128.
void AdaptU8ToTf8(const void* src, void* dst, size_t count) {
    const auto* u8_src = static_cast<const uint8_t*>(src);
    auto* s8_dst = static_cast<int8_t*>(dst);
    for (size_t i = 0; i < count; ++i) {
        s8_dst[i] = static_cast<int8_t>(static_cast<int>(u8_src[i]) - 128);
    }
}

// Returns true if the input at |index| needs U8->TF8 conversion.
bool NeedsQuantizationAdaptation(
    const std::vector<utils::TensorInfo>& info,
    size_t index, utils::DataType input_dtype) {
    return (info[index].dtype == utils::DataType::kInt8 &&
            input_dtype == utils::DataType::kUInt8);
}

// Creates the appropriate UserBufferEncoding subclass for the given
// dtype and quantization parameters.  Returns nullptr on error.
// Manifest-overridden dtype takes effect here (via info.dtype from
// input_info_ / output_info_, which already includes manifest overrides).
std::unique_ptr<DlSystem::UserBufferEncoding> CreateEncoding(
    utils::DataType dtype,
    float scale,
    int32_t zero_point,
    uint32_t bandwidth) {
    switch (dtype) {
        case utils::DataType::kFloat32:
            return std::make_unique<DlSystem::UserBufferEncodingFloat>();
        case utils::DataType::kFloat16:
            return std::make_unique<DlSystem::UserBufferEncodingFloat16>();
        case utils::DataType::kInt8:
            return std::make_unique<DlSystem::UserBufferEncodingTfN>(
                zero_point, scale, bandwidth);
        case utils::DataType::kUInt8:
            return std::make_unique<DlSystem::UserBufferEncodingUint8>();
        case utils::DataType::kInt32:
            return std::make_unique<DlSystem::UserBufferEncodingInt32>();
        default:
            return nullptr;
    }
}

// Extracts quantization parameters (scale, zero_point, bandwidth) from
// SNPE IBufferAttributes encoding.  Returns default QuantParams when the
// encoding is not a quantized type (TF8/TF16).
SnpeBackend::QuantParams ExtractQuantParamsFromAttrs(
    DlSystem::IBufferAttributes* attrs) {
    SnpeBackend::QuantParams qp;
    auto encoding_type = attrs->getEncodingType();
    if (encoding_type == DlSystem::UserBufferEncoding::ElementType_t::TF8 ||
        encoding_type == DlSystem::UserBufferEncoding::ElementType_t::TF16) {
        auto* tfN = static_cast<DlSystem::UserBufferEncodingTfN*>(attrs->getEncoding());
        qp.scale      = tfN->getQuantizedStepSize();
        qp.zero_point = tfN->getStepExactly0();
        qp.bandwidth  = tfN->getBandWidth();
    }
    return qp;
}

// Returns default quantization parameters for the given data type.
// This is used when manifest overrides the runtime dtype, resetting
// quantization params to sensible defaults for the new dtype.
SnpeBackend::QuantParams QuantParamsForDtype(utils::DataType dtype) {
    SnpeBackend::QuantParams qp;
    if (dtype == utils::DataType::kInt8) {
        qp = {1.0f, 0, 8};
    }
    return qp;
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

    // Store model config for builder.setOutputTensorNames().
    model_config_ = config;

    if (ctx != nullptr) {
        active_ctx_ = static_cast<SnpeBackendContext*>(ctx);
        auto ret = active_ctx_->Init(config.config);
        if (ret != utils::ErrorCode::kOk) {
            ATLAS_LOGE("SnpeBackendContext::Init failed (error=%d)", static_cast<int>(ret));
            return ret;
        }
    }

    DlSystem::Runtime_t runtime = DlSystem::Runtime_t::GPU;
    {
        auto it = config.config.find(kConfigRuntime);
        if (it != config.config.end()) {
            runtime = static_cast<DlSystem::Runtime_t>(ParseRuntime(it->second));
        }
    }

    auto perf_profile = DlSystem::PerformanceProfile_t::HIGH_PERFORMANCE;
    {
        auto it = config.config.find(kConfigPerfProfile);
        if (it != config.config.end()) {
            perf_profile = static_cast<DlSystem::PerformanceProfile_t>(
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

    impl_->container = DlContainer::IDlContainer::open(model_path);
    if (impl_->container == nullptr) {
        ATLAS_LOGE("Failed to open DLC container: %s", model_path.c_str());
        Unload();
        return utils::ErrorCode::kFileNotFound;
    }

    DlSystem::RuntimeList runtime_list;
    runtime_list.add(runtime);
    runtime_list.add(DlSystem::Runtime_t::CPU);

    DlSystem::PlatformConfig platform_config;

    SNPE::SNPEBuilder builder(impl_->container.get());
    builder.setRuntimeProcessorOrder(runtime_list);
    builder.setPerformanceProfile(perf_profile);
    builder.setUseUserSuppliedBuffers(use_buffer);
    builder.setPlatformConfig(platform_config);
    builder.setCPUFallbackMode(false);

    // ── Tell SNPE which outputs to expose (manifest order preserved) ──
    if (!model_config_.outputs.empty()) {
        DlSystem::StringList out_names;
        for (const auto& mo : model_config_.outputs) {
            out_names.append(mo.name.c_str());
        }
        builder.setOutputTensorNames(out_names);
        ATLAS_LOGD("setOutputTensorNames: %zu output(s) from manifest",
                   model_config_.outputs.size());
    }

    impl_->snpe = builder.build();
    if (impl_->snpe == nullptr) {
        ATLAS_LOGE("SNPE builder.build() failed for model: %s", model_path.c_str());
        Unload();
        return utils::ErrorCode::kInferFailed;
    }

    {
        auto opt_in_names = impl_->snpe->getInputTensorNames();
        if (!opt_in_names) {
            ATLAS_LOGE("Failed to get input tensor names from SNPE model");
            Unload();
            return utils::ErrorCode::kInferFailed;
        }
        impl_->input_names.clear();
        for (const auto& name : *opt_in_names) {
            impl_->input_names.emplace_back(static_cast<const std::string&>(name));
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
            impl_->output_names.emplace_back(static_cast<const std::string&>(name));
        }
    }

    auto ret = BuildTensorInfos();
    if (ret != utils::ErrorCode::kOk) {
        ATLAS_LOGE("BuildTensorInfos failed (error=%d)", static_cast<int>(ret));
        Unload();
        return ret;
    }

    impl_->use_buffer = (use_buffer != 0);

    // 7. Pre-allocate inference resources based on the selected path.
    if (!impl_->use_buffer) {
        // ── ITensor path: pre-allocate input ITensors (reused across Infer). ──
        auto& tensor_factory = SNPE::SNPEFactory::getTensorFactory();
        impl_->input_tensors.clear();
        impl_->input_tensors.reserve(input_info_.size());
        for (size_t i = 0; i < input_info_.size(); ++i) {
            const utils::TensorInfo& info = input_info_[i];
            std::vector<size_t> shape;
            shape.reserve(info.shape.size());
            for (int d : info.shape) {
                shape.push_back(static_cast<size_t>(d > 0 ? d : 1));
            }
            DlSystem::TensorShape tensor_shape(shape);
            auto itensor = tensor_factory.createTensor(tensor_shape);
            if (itensor == nullptr) {
                ATLAS_LOGE("Failed to pre-allocate input ITensor[%zu]", i);
                Unload();
                return utils::ErrorCode::kInferFailed;
            }
            impl_->input_tensors.push_back(std::move(itensor));
        }
    } else {
        // ── UserBuffer path: create input and output UserBuffers. ──
        impl_->user_input_buffers.clear();
        impl_->user_output_buffers.clear();
        impl_->user_input_encodings.clear();
        impl_->user_output_encodings.clear();
        impl_->user_input_raw.clear();
        impl_->user_output_raw.clear();
        impl_->user_input_external.clear();
        impl_->input_buffer_stride.clear();
        impl_->output_buffer_stride.clear();

        // ── Read shared_input config (optional) ──
        impl_->shared_input_key.clear();
        {
            auto it = config.config.find(kConfigSharedInput);
            if (it != config.config.end() && !it->second.empty()) {
                impl_->shared_input_key = it->second;
                ATLAS_LOGD("Using shared input buffer key='%s'",
                           impl_->shared_input_key.c_str());
            }
        }

        // Get pool reference when shared key is set and context is available.
        SnpeMemoryPool* pool = nullptr;
        if (!impl_->shared_input_key.empty() && active_ctx_ != nullptr) {
            pool = &active_ctx_->GetMemoryPool();
        }

        impl_->user_input_buffers.reserve(input_info_.size());
        impl_->user_input_encodings.reserve(input_info_.size());
        impl_->user_input_raw.reserve(input_info_.size());
        impl_->user_input_external.reserve(input_info_.size());
        impl_->input_buffer_stride.reserve(input_info_.size());

        for (size_t i = 0; i < input_info_.size(); ++i) {
            const std::string& name = impl_->input_names[i];
            const utils::TensorInfo& info = input_info_[i];
            const QuantParams& qp = input_quant_params_[i];

            // Use pre-computed info + quant params from BuildTensorInfos(),
            // no redundant getInputOutputBufferAttributes() call.
            auto encoding = CreateEncoding(info.dtype, qp.scale, qp.zero_point, qp.bandwidth);
            if (encoding == nullptr) {
                ATLAS_LOGE("Unsupported input[%zu] dtype %d for UserBuffer",
                           i, static_cast<int>(info.dtype));
                Unload();
                return utils::ErrorCode::kInferFailed;
            }

            const size_t element_size = ElementByteSize(info.dtype);
            std::vector<size_t> stride = ComputeUserBufferStride(info.shape,
                                                                  element_size);
            const size_t buffer_size = stride.empty() ? 0 : stride[0] *
                static_cast<size_t>(std::max(info.shape[0], 1));

            // ── Allocate input buffer: from pool (shared) or local ──
            AlignedBuffer raw;
            if (pool != nullptr) {
                // Acquire from pool: raw gets a non-owning AlignedBuffer.
                // The pool owns the memory — we nullify data in Unload().
                void* shared_mem = pool->AcquireShared(
                    impl_->shared_input_key, buffer_size, kBufferAlignment);
                if (shared_mem == nullptr && buffer_size > 0) {
                    ATLAS_LOGE("Failed to acquire shared buffer[%zu] (%zu bytes)",
                               i, buffer_size);
                    Unload();
                    return utils::ErrorCode::kInferFailed;
                }
                raw.data = shared_mem;
                raw.size = buffer_size;
            } else {
                raw = AlignedBuffer(buffer_size, kBufferAlignment);
                if (buffer_size > 0 && raw.data == nullptr) {
                    ATLAS_LOGE("Failed to allocate input buffer[%zu] (%zu bytes)",
                               i, buffer_size);
                    Unload();
                    return utils::ErrorCode::kInferFailed;
                }
            }

            auto user_buf = impl_->snpe->createInputBuffer(
                name.c_str(),
                static_cast<size_t>(raw.size),
                stride.data(),
                encoding.get());
            if (user_buf == nullptr) {
                ATLAS_LOGE("Failed to create input UserBuffer[%zu]: %s",
                           i, name.c_str());
                Unload();
                return utils::ErrorCode::kInferFailed;
            }

            impl_->user_input_encodings.push_back(std::move(encoding));
            impl_->user_input_raw.push_back(std::move(raw));
            impl_->user_input_buffers.push_back(std::move(user_buf));
            impl_->user_input_external.push_back(nullptr);
            impl_->input_buffer_stride.push_back(stride.empty() ? 0 : stride[0]);
        }

        // Create output UserBuffers.
        impl_->user_output_buffers.reserve(output_info_.size());
        impl_->user_output_encodings.reserve(output_info_.size());
        impl_->user_output_raw.reserve(output_info_.size());
        impl_->output_buffer_stride.reserve(output_info_.size());

        for (size_t i = 0; i < output_info_.size(); ++i) {
            const std::string& name = impl_->output_names[i];
            const utils::TensorInfo& info = output_info_[i];
            const QuantParams& qp = output_quant_params_[i];

            // Use pre-computed info + quant params from BuildTensorInfos().
            auto encoding = CreateEncoding(info.dtype, qp.scale, qp.zero_point, qp.bandwidth);
            if (encoding == nullptr) {
                ATLAS_LOGE("Unsupported output[%zu] dtype %d for UserBuffer",
                           i, static_cast<int>(info.dtype));
                Unload();
                return utils::ErrorCode::kInferFailed;
            }

            const size_t element_size = ElementByteSize(info.dtype);
            std::vector<size_t> stride = ComputeUserBufferStride(info.shape,
                                                                  element_size);
            const size_t buffer_size = stride.empty() ? 0 : stride[0] *
                static_cast<size_t>(std::max(info.shape[0], 1));

            AlignedBuffer raw(buffer_size, kBufferAlignment);
            if (buffer_size > 0 && raw.data == nullptr) {
                ATLAS_LOGE("Failed to allocate output buffer[%zu] (%zu bytes)",
                           i, buffer_size);
                Unload();
                return utils::ErrorCode::kInferFailed;
            }

            auto user_buf = impl_->snpe->createOutputBuffer(
                name.c_str(),
                static_cast<size_t>(raw.size),
                stride.data(),
                encoding.get());
            if (user_buf == nullptr) {
                ATLAS_LOGE("Failed to create output UserBuffer[%zu]: %s",
                           i, name.c_str());
                Unload();
                return utils::ErrorCode::kInferFailed;
            }

            impl_->user_output_encodings.push_back(std::move(encoding));
            impl_->user_output_raw.push_back(std::move(raw));
            impl_->user_output_buffers.push_back(std::move(user_buf));
            impl_->output_buffer_stride.push_back(stride.empty() ? 0 : stride[0]);
        }
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

    if (!impl_->use_buffer) {
        return InferWithTensor(inputs, outputs);
    }
    return InferWithBuffer(inputs, outputs);
}

// ── ITensor inference path (use_buffer == false) ──────────────────────────

utils::ErrorCode SnpeBackend::InferWithTensor(
    const std::vector<utils::Tensor>& inputs,
    std::vector<utils::Tensor>& outputs) {
    DlSystem::TensorMap input_map;
    for (size_t i = 0; i < inputs.size(); ++i) {
        const utils::Tensor& t = inputs[i];
        const utils::TensorInfo& info = input_info_[i];
        auto* itensor = impl_->input_tensors[i].get();

        if (t.data == GetInputBuffer(i).data) {
            goto add_to_map;
        }

        if (info.dtype == utils::DataType::kFloat32 &&
            t.data != nullptr &&
            t.byte_size % sizeof(float) == 0) {
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
            std::memcpy(&(*itensor->begin()), t.data, t.byte_size);
        }

    add_to_map:
        if (inputs.size() > 1) {
            input_map.add(impl_->input_names[i].c_str(), itensor);
        }
    }

    if (output_info_.size() != impl_->output_names.size()) {
        ATLAS_LOGE("Output metadata mismatch: expected %zu infos, got %zu names",
                   output_info_.size(), impl_->output_names.size());
        return utils::ErrorCode::kInferFailed;
    }

    const bool execute_ok =
        inputs.size() == 1
            ? impl_->snpe->execute(impl_->input_tensors[0].get(), impl_->output_map)
            : impl_->snpe->execute(input_map, impl_->output_map);
    if (!execute_ok) {
        ATLAS_LOGE("SNPE execute failed");
        return utils::ErrorCode::kInferFailed;
    }

    outputs.clear();
    outputs.reserve(impl_->output_names.size());
    for (size_t i = 0; i < impl_->output_names.size(); ++i) {
        DlSystem::ITensor* out_itensor =
            impl_->output_map.getTensor(impl_->output_names[i].c_str());
        if (out_itensor == nullptr) {
            ATLAS_LOGE("Failed to get output tensor[%zu]: %s", i,
                       impl_->output_names[i].c_str());
            return utils::ErrorCode::kInferFailed;
        }

        const utils::TensorInfo& info = output_info_[i];
        auto raw_ptr = out_itensor->cbegin();
        utils::Tensor out;
        out.info      = info;
        out.byte_size = out_itensor->getSize() * ElementByteSize(info.dtype);
        out.data      = const_cast<void*>(static_cast<const void*>(&(*raw_ptr)));
        out.owns_data = false;
        outputs.push_back(std::move(out));
    }

    return utils::ErrorCode::kOk;
}

// ── UserBuffer inference path (use_buffer == true) ─────────────────────────

utils::ErrorCode SnpeBackend::InferWithBuffer(
    const std::vector<utils::Tensor>& inputs,
    std::vector<utils::Tensor>& outputs) {
    // Step 1: Fill input buffers.
    for (size_t i = 0; i < inputs.size(); ++i) {
        const utils::Tensor& t = inputs[i];
        uint8_t* target = impl_->user_input_raw[i].AsU8();
        const size_t target_size = impl_->user_input_raw[i].size;

        if (target == nullptr || target_size == 0) {
            ATLAS_LOGE("UserBuffer input[%zu] has null or empty buffer", i);
            return utils::ErrorCode::kInferFailed;
        }

        // External memory injection via SetInputBuffer().
        if (impl_->user_input_external[i] != nullptr &&
            t.data == impl_->user_input_external[i]) {
            std::memcpy(target, t.data, std::min(t.byte_size, target_size));
            continue;
        }

        // Zero-copy: caller wrote directly into internal buffer.
        if (t.data == static_cast<void*>(target)) {
            continue;
        }

        // Quantization adaptation: U8 -> TF8.
        if (NeedsQuantizationAdaptation(input_info_, i, t.info.dtype)) {
            const size_t count = std::min(t.byte_size, target_size);
            AdaptU8ToTf8(t.data, target, count);
            continue;
        }

        // Default: direct memcpy.
        std::memcpy(target, t.data, std::min(t.byte_size, target_size));
    }

    // Step 2: Build UserBufferMap and execute.
    DlSystem::UserBufferMap input_map, output_map;
    for (size_t i = 0; i < impl_->input_names.size(); ++i) {
        input_map.add(impl_->input_names[i].c_str(),
                      impl_->user_input_buffers[i].get());
    }
    for (size_t i = 0; i < impl_->output_names.size(); ++i) {
        output_map.add(impl_->output_names[i].c_str(),
                       impl_->user_output_buffers[i].get());
    }

    if (!impl_->snpe->execute(input_map, output_map)) {
        ATLAS_LOGE("SNPE UserBuffer execute failed");
        return utils::ErrorCode::kInferFailed;
    }

    // Step 3: Wrap output buffers as zero-copy Tensors.
    outputs.clear();
    outputs.reserve(impl_->output_names.size());
    for (size_t i = 0; i < impl_->output_names.size(); ++i) {
        utils::Tensor out;
        out.info      = output_info_[i];
        out.byte_size = impl_->user_output_raw[i].size;
        out.data      = impl_->user_output_raw[i].AsU8();
        out.owns_data = false;
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
    // ── Release shared buffer back to pool before clearing anything ──
    if (!impl_->shared_input_key.empty() && active_ctx_ != nullptr) {
        auto& pool = active_ctx_->GetMemoryPool();
        pool.ReleaseShared(impl_->shared_input_key);
        // Nullify data pointers in user_input_raw so their destructors
        // do not double-free pool-owned memory when the vector is cleared.
        for (auto& buf : impl_->user_input_raw) {
            buf.data = nullptr;
            buf.size = 0;
        }
        impl_->shared_input_key.clear();
    }

    // UserBuffer path cleanup.
    impl_->user_output_raw.clear();
    impl_->user_input_raw.clear();
    impl_->user_output_encodings.clear();
    impl_->user_input_encodings.clear();
    impl_->user_output_buffers.clear();
    impl_->user_input_buffers.clear();
    impl_->user_input_external.clear();
    impl_->input_buffer_stride.clear();
    impl_->output_buffer_stride.clear();

    // ITensor path cleanup.
    impl_->snpe.reset();
    impl_->container.reset();
    impl_->input_names.clear();
    impl_->output_names.clear();
    impl_->input_tensors.clear();
    impl_->use_buffer = false;

    input_info_.clear();
    output_info_.clear();
    input_quant_params_.clear();
    output_quant_params_.clear();
    model_config_ = core::ModelConfig();
    active_ctx_ = nullptr;
    loaded_ = false;
}

bool SnpeBackend::IsLoaded() const { return loaded_; }

std::string SnpeBackend::Version() const {
    return std::string(SNPE::SNPEFactory::getLibraryVersion().toString());
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

utils::Span<void> SnpeBackend::GetInputBuffer(size_t index) const {
    if (!loaded_) return {};

    if (impl_->use_buffer) {
        if (index >= impl_->user_input_raw.size()) return {};
        const AlignedBuffer& buf = impl_->user_input_raw[index];
        return { buf.data, buf.size };
    }

    if (index >= impl_->input_tensors.size()) return {};
    auto* itensor = impl_->input_tensors[index].get();
    auto raw = itensor->begin();
    return utils::Span<void>(
        static_cast<void*>(&(*raw)),
        itensor->getSize() * sizeof(float));
}

utils::Span<void> SnpeBackend::GetOutputBuffer(size_t index) const {
    if (!loaded_) return {};

    if (impl_->use_buffer) {
        if (index >= impl_->user_output_raw.size()) return {};
        const AlignedBuffer& buf = impl_->user_output_raw[index];
        return { buf.data, buf.size };
    }

    if (index >= impl_->output_names.size()) return {};
    DlSystem::ITensor* out_itensor =
        impl_->output_map.getTensor(impl_->output_names[index].c_str());
    if (out_itensor == nullptr) return {};
    auto raw_ptr = out_itensor->cbegin();
    return utils::Span<void>(
        const_cast<void*>(static_cast<const void*>(&(*raw_ptr))),
        out_itensor->getSize() * sizeof(float));
}

utils::ErrorCode SnpeBackend::SetInputBuffer(size_t index, void* external_mem,
                                              size_t byte_size) {
    if (!loaded_) return utils::ErrorCode::kNotInitialized;
    if (!impl_->use_buffer) {
        ATLAS_LOGE("SetInputBuffer requires use_buffer=true");
        return utils::ErrorCode::kInvalidArgument;
    }
    if (index >= impl_->user_input_raw.size()) {
        ATLAS_LOGE("SetInputBuffer index %zu out of range (max %zu)",
                   index, impl_->user_input_raw.size());
        return utils::ErrorCode::kInvalidArgument;
    }
    if (external_mem != nullptr &&
        byte_size < impl_->user_input_raw[index].size) {
        ATLAS_LOGE("SetInputBuffer[%zu] external buffer too small: "
                   "need %zu, got %zu", index,
                   impl_->user_input_raw[index].size, byte_size);
        return utils::ErrorCode::kInvalidArgument;
    }

    impl_->user_input_external[index] = external_mem;
    ATLAS_LOGD("SetInputBuffer[%zu] = %p (size=%zu)", index,
               external_mem, byte_size);
    return utils::ErrorCode::kOk;
}

utils::ErrorCode SnpeBackend::BuildTensorInfos() {
    ATLAS_LOGD("%s called", __FUNCTION__);
    input_info_.clear();
    output_info_.clear();
    input_quant_params_.clear();
    output_quant_params_.clear();

    for (const auto& name : impl_->input_names) {
        auto opt_attrs = impl_->snpe->getInputOutputBufferAttributes(name.c_str());
        if (!opt_attrs || *opt_attrs == nullptr) {
            ATLAS_LOGE("Failed to get input buffer attributes for tensor: %s", name.c_str());
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

        // Extract quantization params from runtime buffer attributes.
        QuantParams qp = ExtractQuantParamsFromAttrs(opt_attrs->get());

        // Manifest config overrides (if present) — manifest is first priority.
        for (const auto& mi : model_config_.inputs) {
            if (mi.name != name) continue;
            if (!mi.shape.empty())  info.shape  = mi.shape;
            if (!mi.layout.empty()) info.layout = mi.layout;
            if (mi.dtype != utils::DataType::kUnknown) {
                info.dtype = mi.dtype;
                qp = QuantParamsForDtype(mi.dtype);  // reset quant params for new dtype
            }
            break;
        }

        input_info_.push_back(std::move(info));
        input_quant_params_.push_back(std::move(qp));
    }

    for (const auto& name : impl_->output_names) {
        auto opt_attrs = impl_->snpe->getInputOutputBufferAttributes(name.c_str());
        if (!opt_attrs || *opt_attrs == nullptr) {
            ATLAS_LOGE("Failed to get output buffer attributes for tensor: %s", name.c_str());
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

        // Extract quantization params from runtime buffer attributes.
        QuantParams qp = ExtractQuantParamsFromAttrs(opt_attrs->get());

        // Manifest config overrides (if present) — manifest is first priority.
        for (const auto& mo : model_config_.outputs) {
            if (mo.name != name) continue;
            if (!mo.shape.empty())  info.shape  = mo.shape;
            if (!mo.layout.empty()) info.layout = mo.layout;
            if (mo.dtype != utils::DataType::kUnknown) {
                info.dtype = mo.dtype;
                qp = QuantParamsForDtype(mo.dtype);
            }
            break;
        }

        output_info_.push_back(std::move(info));
        output_quant_params_.push_back(std::move(qp));
    }

    return utils::ErrorCode::kOk;
}

}  // namespace backend
}  // namespace atlas

ATLAS_REGISTER_BACKEND("snpe", atlas::backend::SnpeBackend)