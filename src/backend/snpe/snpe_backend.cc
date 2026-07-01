#include "src/backend/snpe/snpe_backend.h"

#include <cstring>
#include <string>
#include <vector>

#include "src/backend/base/backend_factory.h"
#include "src/backend/snpe/snpe_backend_context.h"
#include "src/utils/types.h"

namespace atlas {
namespace backend {

#ifdef ATLAS_SNPE_ENABLED

#include "DlContainer/IDlContainer.hpp"
#include "DlSystem/DlEnums.hpp"
#include "DlSystem/IBufferAttributes.hpp"
#include "DlSystem/ITensor.hpp"
#include "DlSystem/ITensorFactory.hpp"
#include "DlSystem/TensorMap.hpp"
#include "DlSystem/TensorShape.hpp"
#include "DlSystem/StringList.hpp"
#include "SNPE/SNPE.hpp"
#include "SNPE/SNPEBuilder.hpp"
#include "SNPE/SNPEFactory.hpp"

namespace {

// Config key constants.
constexpr char kConfigRuntime[] ATLAS_MAYBE_UNUSED     = "runtime";
constexpr char kConfigPerfProfile[] ATLAS_MAYBE_UNUSED = "performance_profile";
constexpr char kConfigUseBuffer[] ATLAS_MAYBE_UNUSED   = "use_buffer";

// Runtime name strings accepted from manifest config.
constexpr char kRuntimeCpu[] ATLAS_MAYBE_UNUSED = "cpu";
constexpr char kRuntimeGpu[] ATLAS_MAYBE_UNUSED = "gpu";
constexpr char kRuntimeDsp[] ATLAS_MAYBE_UNUSED = "dsp";
constexpr char kRuntimeAip[] ATLAS_MAYBE_UNUSED = "aip";

// Performance profile name strings accepted from manifest config.
constexpr char kPerfDefault[] ATLAS_MAYBE_UNUSED            = "default";
constexpr char kPerfBalanced[] ATLAS_MAYBE_UNUSED           = "balanced";
constexpr char kPerfHighPerformance[] ATLAS_MAYBE_UNUSED    = "high_performance";
constexpr char kPerfPowerSaver[] ATLAS_MAYBE_UNUSED         = "power_saver";
constexpr char kPerfSystemSettings[] ATLAS_MAYBE_UNUSED     = "system_settings";
constexpr char kPerfSustainedHighPerf[] ATLAS_MAYBE_UNUSED  = "sustained_high_performance";
constexpr char kPerfBurst[] ATLAS_MAYBE_UNUSED              = "burst";
constexpr char kPerfLowPowerSaver[] ATLAS_MAYBE_UNUSED      = "low_power_saver";
constexpr char kPerfHighPowerSaver[] ATLAS_MAYBE_UNUSED     = "high_power_saver";
constexpr char kPerfLowBalanced[] ATLAS_MAYBE_UNUSED        = "low_balanced";
constexpr char kPerfExtremePowerSaver[] ATLAS_MAYBE_UNUSED  = "extreme_power_saver";

}  // namespace

struct SnpeImpl {
    std::unique_ptr<DlContainer::IDlContainer> container;
    std::unique_ptr<SNPE::SNPE>                snpe;
    std::vector<std::string>                   input_names;
    std::vector<std::string>                   output_names;
};

namespace {

ATLAS_MAYBE_UNUSED
int ParseRuntime(const std::string& runtime_str) {
    if (runtime_str == kRuntimeCpu) return static_cast<int>(DlSystem::Runtime_t::CPU_FLOAT32);
    if (runtime_str == kRuntimeGpu) return static_cast<int>(DlSystem::Runtime_t::GPU_FLOAT32_16_HYBRID);
    if (runtime_str == kRuntimeDsp) return static_cast<int>(DlSystem::Runtime_t::DSP_FIXED8_TF);
    if (runtime_str == kRuntimeAip) return static_cast<int>(DlSystem::Runtime_t::AIP_FIXED8_TF);
    // Default: GPU.
    return static_cast<int>(DlSystem::Runtime_t::GPU_FLOAT32_16_HYBRID);
}

ATLAS_MAYBE_UNUSED
int ParsePerformanceProfile(const std::string& profile_str) {
    if (profile_str == kPerfDefault ||
        profile_str == kPerfBalanced) {
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
    // Default: BALANCED.
    return static_cast<int>(DlSystem::PerformanceProfile_t::BALANCED);
}

// Converts an SNPE IOBufferDataType_t to atlas DataType.
ATLAS_MAYBE_UNUSED
utils::DataType SnpeDtypeToAtlas(DlSystem::IOBufferDataType_t snpe_dtype) {
    switch (snpe_dtype) {
        case DlSystem::IOBufferDataType_t::FLOATING_POINT_32:
            return utils::DataType::kFloat32;
        case DlSystem::IOBufferDataType_t::FLOATING_POINT_16:
            return utils::DataType::kFloat16;
        case DlSystem::IOBufferDataType_t::FIXED_POINT_8:
            return utils::DataType::kInt8;
        case DlSystem::IOBufferDataType_t::INT_32:
            return utils::DataType::kInt32;
        case DlSystem::IOBufferDataType_t::UINT_8:
            return utils::DataType::kUInt8;
        default:
            return utils::DataType::kUnknown;
    }
}

// Returns the byte size for an IOBufferDataType_t element.
ATLAS_MAYBE_UNUSED
size_t SnpeElementByteSize(DlSystem::IOBufferDataType_t t) {
    switch (t) {
        case DlSystem::IOBufferDataType_t::FLOATING_POINT_32: return 4;
        case DlSystem::IOBufferDataType_t::FLOATING_POINT_16: return 2;
        case DlSystem::IOBufferDataType_t::INT_32:            return 4;
        case DlSystem::IOBufferDataType_t::UINT_32:           return 4;
        case DlSystem::IOBufferDataType_t::FIXED_POINT_8:     return 1;
        case DlSystem::IOBufferDataType_t::INT_8:             return 1;
        case DlSystem::IOBufferDataType_t::UINT_8:            return 1;
        case DlSystem::IOBufferDataType_t::FIXED_POINT_16:    return 2;
        case DlSystem::IOBufferDataType_t::INT_16:            return 2;
        case DlSystem::IOBufferDataType_t::UINT_16:           return 2;
        case DlSystem::IOBufferDataType_t::BOOL_8:            return 1;
        case DlSystem::IOBufferDataType_t::INT_64:            return 8;
        case DlSystem::IOBufferDataType_t::UINT_64:           return 8;
        default:                                              return 0;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction / destruction (full)
// ---------------------------------------------------------------------------

SnpeBackend::SnpeBackend() : impl_(std::make_unique<SnpeImpl>()) {}

SnpeBackend::~SnpeBackend() { Unload(); }

// ---------------------------------------------------------------------------
// IBackend interface (full)
// ---------------------------------------------------------------------------

utils::ErrorCode SnpeBackend::Load(const std::string& model_path,
                                    const core::ModelConfig& config,
                                    IBackendContext* ctx) {
    Unload();

    // 1. Ensure shared context is initialized (idempotent).
    if (ctx != nullptr) {
        active_ctx_ = static_cast<SnpeBackendContext*>(ctx);
        auto ret = active_ctx_->Init(config.config);
        if (ret != utils::ErrorCode::kOk) return ret;
    }

    // 2. Extract per-model parameters from config.
    auto runtime = DlSystem::Runtime_t::GPU_FLOAT32_16_HYBRID;
    {
        auto it = config.config.find(kConfigRuntime);
        if (it != config.config.end()) {
            runtime = static_cast<DlSystem::Runtime_t>(ParseRuntime(it->second));
        }
    }

    auto perf_profile = DlSystem::PerformanceProfile_t::BALANCED;
    {
        auto it = config.config.find(kConfigPerfProfile);
        if (it != config.config.end()) {
            perf_profile = static_cast<DlSystem::PerformanceProfile_t>(
                ParsePerformanceProfile(it->second));
        }
    }

    int use_buffer = 0;  // Default: ITensor mode
    {
        auto it = config.config.find(kConfigUseBuffer);
        if (it != config.config.end() && it->second == "true") {
            use_buffer = 1;
        }
    }

    // 3. Open the .dlc container.
    impl_->container = DlContainer::IDlContainer::open(model_path);
    if (impl_->container == nullptr) {
        Unload();
        return utils::ErrorCode::kFileNotFound;
    }

    // 4. Build the runtime list and construct the SNPE network.
    DlSystem::RuntimeList runtime_list;
    runtime_list.add(runtime);

    DlSystem::PlatformConfig platform_config;

    SNPE::SNPEBuilder builder(impl_->container.get());
    builder.setRuntimeProcessorOrder(runtime_list);
    builder.setPerformanceProfile(perf_profile);
    builder.setUseUserSuppliedBuffers(use_buffer);
    builder.setPlatformConfig(platform_config);

    impl_->snpe = builder.build();
    if (impl_->snpe == nullptr) {
        Unload();
        return utils::ErrorCode::kInferFailed;
    }

    // 5. Extract tensor names.
    {
        auto opt_in_names = impl_->snpe->getInputTensorNames();
        if (!opt_in_names) {
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
            Unload();
            return utils::ErrorCode::kInferFailed;
        }
        impl_->output_names.clear();
        for (const auto& name : *opt_out_names) {
            impl_->output_names.emplace_back(static_cast<const std::string&>(name));
        }
    }

    // 6. Build tensor info.
    auto ret = BuildTensorInfos();
    if (ret != utils::ErrorCode::kOk) {
        Unload();
        return ret;
    }

    loaded_ = true;
    return utils::ErrorCode::kOk;
}

utils::ErrorCode SnpeBackend::Infer(const std::vector<utils::Tensor>& inputs,
                                     std::vector<utils::Tensor>& outputs) {
    if (!loaded_) return utils::ErrorCode::kNotInitialized;

    auto& tensor_factory = SNPE::SNPEFactory::getTensorFactory();

    // Build input TensorMap.
    DlSystem::TensorMap input_map;
    for (size_t i = 0; i < inputs.size(); ++i) {
        const utils::Tensor& t = inputs[i];
        const utils::TensorInfo& info = input_info_[i];

        // Build shape as vector<size_t>.
        std::vector<size_t> shape;
        shape.reserve(info.shape.size());
        for (int d : info.shape) shape.push_back(static_cast<size_t>(d > 0 ? d : 1));

        DlSystem::TensorShape tensor_shape(shape);

        // Create ITensor with data copy.
        auto itensor = tensor_factory.createTensor(
            tensor_shape,
            static_cast<const unsigned char*>(t.data),
            t.byte_size);
        if (itensor == nullptr) {
            return utils::ErrorCode::kInferFailed;
        }

        input_map.add(impl_->input_names[i].c_str(), itensor.release());
    }

    // Create output TensorMap — populate with empty ITensors of the expected
    // shapes so that SNPE has pre-allocated buffers to write into.
    DlSystem::TensorMap output_map;
    for (size_t i = 0; i < impl_->output_names.size(); ++i) {
        const utils::TensorInfo& info = output_info_[i];

        std::vector<size_t> shape;
        shape.reserve(info.shape.size());
        for (int d : info.shape) shape.push_back(static_cast<size_t>(d > 0 ? d : 1));

        DlSystem::TensorShape tensor_shape(shape);
        auto otensor = tensor_factory.createTensor(tensor_shape);
        if (otensor == nullptr) {
            return utils::ErrorCode::kInferFailed;
        }

        output_map.add(impl_->output_names[i].c_str(), otensor.release());
    }

    // Execute.
    if (!impl_->snpe->execute(input_map, output_map)) {
        return utils::ErrorCode::kInferFailed;
    }

    // Copy outputs into caller-owned atlas Tensors.
    outputs.clear();
    outputs.reserve(impl_->output_names.size());

    for (size_t i = 0; i < impl_->output_names.size(); ++i) {
        DlSystem::ITensor* out_itensor =
            output_map.getTensor(impl_->output_names[i].c_str());
        if (out_itensor == nullptr) {
            return utils::ErrorCode::kInferFailed;
        }

        const utils::TensorInfo& info = output_info_[i];
        size_t elem_count = utils::ElementCount(info.shape);
        size_t elem_size  = utils::ElementByteSize(info.dtype);
        size_t byte_size  = elem_count * elem_size;

        utils::Tensor out;
        out.info      = info;
        out.byte_size = byte_size;
        out.data      = malloc(byte_size);
        out.owns_data = true;

        // SNPE ITensor data is owned by the tensor; copy out.
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

// ---------------------------------------------------------------------------
// Private helpers (full)
// ---------------------------------------------------------------------------

utils::ErrorCode SnpeBackend::BuildTensorInfos() {
    input_info_.clear();
    output_info_.clear();

    // Inputs.
    for (const auto& name : impl_->input_names) {
        auto opt_shape = impl_->snpe->getInputDimensions(name.c_str());
        if (!opt_shape) return utils::ErrorCode::kInferFailed;

        utils::TensorInfo info;
        info.name = name;
        const size_t* dims = opt_shape->getDimensions();
        for (size_t r = 0; r < opt_shape->rank(); ++r) {
            info.shape.push_back(static_cast<int>(dims[r]));
        }

        // Attempt to detect dtype from buffer attributes.
        auto opt_attr = impl_->snpe->getInputOutputBufferAttributes(name.c_str());
        if (opt_attr && *opt_attr != nullptr) {
            auto* attr = *opt_attr;
            DlSystem::IOBufferDataType_t buf_dtype =
                attr->getBufferPrecision();
            info.dtype = SnpeDtypeToAtlas(buf_dtype);
        } else {
            // Fallback: assume float32.
            info.dtype = utils::DataType::kFloat32;
        }

        input_info_.push_back(std::move(info));
    }

    // Outputs.
    for (const auto& name : impl_->output_names) {
        auto opt_shape = impl_->snpe->getInputDimensions(name.c_str());
        if (!opt_shape) return utils::ErrorCode::kInferFailed;

        utils::TensorInfo info;
        info.name = name;
        const size_t* dims = opt_shape->getDimensions();
        for (size_t r = 0; r < opt_shape->rank(); ++r) {
            info.shape.push_back(static_cast<int>(dims[r]));
        }

        auto opt_attr = impl_->snpe->getInputOutputBufferAttributes(name.c_str());
        if (opt_attr && *opt_attr != nullptr) {
            auto* attr = *opt_attr;
            DlSystem::IOBufferDataType_t buf_dtype =
                attr->getBufferPrecision();
            info.dtype = SnpeDtypeToAtlas(buf_dtype);
        } else {
            info.dtype = utils::DataType::kFloat32;
        }

        output_info_.push_back(std::move(info));
    }

    return utils::ErrorCode::kOk;
}

#else

// ===================================================================
// === Stub implementation (non-target platforms) ===
// No SNPE SDK dependency; compiles cleanly on macOS / Linux x86_64.
// ===================================================================

SnpeBackend::SnpeBackend() = default;

SnpeBackend::~SnpeBackend() {
    Unload();
}

utils::ErrorCode SnpeBackend::Load(const std::string& model_path,
                                    const core::ModelConfig& config,
                                    IBackendContext* ctx) {
    (void)model_path;
    (void)config;
    (void)ctx;
    return utils::ErrorCode::kBackendNotFound;
}

utils::ErrorCode SnpeBackend::Infer(const std::vector<utils::Tensor>& inputs,
                                     std::vector<utils::Tensor>& outputs) {
    (void)inputs;
    (void)outputs;
    return utils::ErrorCode::kNotInitialized;
}

std::vector<utils::TensorInfo> SnpeBackend::GetInputInfo() const {
    return {};
}

std::vector<utils::TensorInfo> SnpeBackend::GetOutputInfo() const {
    return {};
}

void SnpeBackend::Unload() {
    active_ctx_ = nullptr;
    loaded_ = false;
}

bool SnpeBackend::IsLoaded() const { return false; }

#endif

}  // namespace backend
}  // namespace atlas

ATLAS_REGISTER_BACKEND("snpe", atlas::backend::SnpeBackend)