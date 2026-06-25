#include "src/backend/cpu/cpu_backend.h"

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "src/backend/base/backend_factory.h"
#include "src/utils/types.h"

namespace atlas {
namespace backend {

namespace {

constexpr int     kDefaultNumThreads  = 1;
constexpr char    kConfigNumThreads[] = "num_threads";
constexpr char    kOrtLoggerName[]    = "atlas_cpu";

// Returns the byte size of one ORT element (same dtype set as atlas).
size_t OrtElementByteSize(ONNXTensorElementDataType t) {
    switch (t) {
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:   return 4;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16: return 2;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:    return 1;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:   return 1;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:   return 4;
        default:                                    return 0;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

CpuBackend::CpuBackend() = default;
CpuBackend::~CpuBackend() { Unload(); }

// ---------------------------------------------------------------------------
// IBackend interface
// ---------------------------------------------------------------------------

utils::ErrorCode CpuBackend::Load(const std::string& model_path,
                                   const core::ModelConfig& config) {
    Unload();

    // Resolve num_threads from per-model config, fall back to default.
    int num_threads = kDefaultNumThreads;
    auto it = config.config.find(kConfigNumThreads);
    if (it != config.config.end()) {
        try {
            num_threads = std::stoi(it->second);
        } catch (...) {
            num_threads = kDefaultNumThreads;
        }
    }

    try {
        env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING,
                                           kOrtLoggerName);

        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(num_threads);
        opts.SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

        session_ = std::make_unique<Ort::Session>(*env_,
                                                   model_path.c_str(),
                                                   opts);
    } catch (const Ort::Exception& e) {
        env_.reset();
        session_.reset();
        return utils::ErrorCode::kInvalidArgument;
    }

    auto ret = BuildTensorInfos();
    if (ret != utils::ErrorCode::kOk) {
        Unload();
        return ret;
    }

    loaded_ = true;
    return utils::ErrorCode::kOk;
}

utils::ErrorCode CpuBackend::Infer(const std::vector<utils::Tensor>& inputs,
                                    std::vector<utils::Tensor>& outputs) {
    if (!loaded_) return utils::ErrorCode::kNotInitialized;

    // Wrap atlas Tensors as Ort::Value (zero-copy).
    auto mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,
                                               OrtMemTypeDefault);

    std::vector<Ort::Value> ort_inputs;
    ort_inputs.reserve(inputs.size());

    for (size_t i = 0; i < inputs.size(); ++i) {
        const utils::Tensor& t = inputs[i];
        std::vector<int64_t> shape;
        shape.reserve(t.info.shape.size());
        for (int d : t.info.shape) shape.push_back(static_cast<int64_t>(d));

        // CreateTensor is templated; dispatch on dtype.
        switch (t.info.dtype) {
            case utils::DataType::kFloat32:
                ort_inputs.push_back(Ort::Value::CreateTensor<float>(
                    mem_info,
                    static_cast<float*>(t.data),
                    t.byte_size / sizeof(float),
                    shape.data(), shape.size()));
                break;
            case utils::DataType::kUInt8:
                ort_inputs.push_back(Ort::Value::CreateTensor<uint8_t>(
                    mem_info,
                    static_cast<uint8_t*>(t.data),
                    t.byte_size,
                    shape.data(), shape.size()));
                break;
            case utils::DataType::kInt8:
                ort_inputs.push_back(Ort::Value::CreateTensor<int8_t>(
                    mem_info,
                    static_cast<int8_t*>(t.data),
                    t.byte_size,
                    shape.data(), shape.size()));
                break;
            case utils::DataType::kInt32:
                ort_inputs.push_back(Ort::Value::CreateTensor<int32_t>(
                    mem_info,
                    static_cast<int32_t*>(t.data),
                    t.byte_size / sizeof(int32_t),
                    shape.data(), shape.size()));
                break;
            default:
                return utils::ErrorCode::kInvalidArgument;
        }
    }

    // Build raw name pointer arrays required by ORT C API.
    std::vector<const char*> in_names;
    in_names.reserve(input_names_.size());
    for (const auto& n : input_names_) in_names.push_back(n.c_str());

    std::vector<const char*> out_names;
    out_names.reserve(output_names_.size());
    for (const auto& n : output_names_) out_names.push_back(n.c_str());

    // Run inference.
    std::vector<Ort::Value> ort_outputs;
    try {
        ort_outputs = session_->Run(Ort::RunOptions{nullptr},
                                    in_names.data(),
                                    ort_inputs.data(),
                                    in_names.size(),
                                    out_names.data(),
                                    out_names.size());
    } catch (const Ort::Exception&) {
        return utils::ErrorCode::kInferFailed;
    }

    // Copy ORT outputs into caller-owned atlas Tensors.
    outputs.clear();
    outputs.reserve(ort_outputs.size());

    for (size_t i = 0; i < ort_outputs.size(); ++i) {
        auto type_shape = ort_outputs[i].GetTensorTypeAndShapeInfo();
        auto ort_shape  = type_shape.GetShape();
        auto ort_dtype  = type_shape.GetElementType();

        utils::Tensor out;
        out.info       = output_info_[i];
        out.byte_size  = type_shape.GetElementCount() *
                         OrtElementByteSize(ort_dtype);
        out.data       = malloc(out.byte_size);
        out.owns_data  = true;
        std::memcpy(out.data,
                    ort_outputs[i].GetTensorRawData(),
                    out.byte_size);
        outputs.push_back(std::move(out));
    }

    return utils::ErrorCode::kOk;
}

std::vector<utils::TensorInfo> CpuBackend::GetInputInfo() const {
    return input_info_;
}

std::vector<utils::TensorInfo> CpuBackend::GetOutputInfo() const {
    return output_info_;
}

void CpuBackend::Unload() {
    session_.reset();
    env_.reset();
    input_names_.clear();
    output_names_.clear();
    input_info_.clear();
    output_info_.clear();
    loaded_ = false;
}

bool CpuBackend::IsLoaded() const { return loaded_; }

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

utils::DataType CpuBackend::OrtDtypeToAtlas(
    ONNXTensorElementDataType ort_type) {
    switch (ort_type) {
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:   return utils::DataType::kFloat32;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16: return utils::DataType::kFloat16;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:    return utils::DataType::kInt8;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:   return utils::DataType::kUInt8;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:   return utils::DataType::kInt32;
        default:                                    return utils::DataType::kUnknown;
    }
}

utils::ErrorCode CpuBackend::BuildTensorInfos() {
    const size_t num_inputs  = session_->GetInputCount();
    const size_t num_outputs = session_->GetOutputCount();

    input_names_.reserve(num_inputs);
    input_info_.reserve(num_inputs);
    for (size_t i = 0; i < num_inputs; ++i) {
        auto name_ptr = session_->GetInputNameAllocated(i, allocator_);
        input_names_.emplace_back(name_ptr.get());

        auto type_info   = session_->GetInputTypeInfo(i);
        auto shape_info  = type_info.GetTensorTypeAndShapeInfo();
        auto ort_shape   = shape_info.GetShape();
        auto ort_dtype   = shape_info.GetElementType();

        utils::TensorInfo info;
        info.name  = input_names_.back();
        info.dtype = OrtDtypeToAtlas(ort_dtype);
        for (int64_t d : ort_shape) info.shape.push_back(static_cast<int>(d));
        input_info_.push_back(std::move(info));
    }

    output_names_.reserve(num_outputs);
    output_info_.reserve(num_outputs);
    for (size_t i = 0; i < num_outputs; ++i) {
        auto name_ptr = session_->GetOutputNameAllocated(i, allocator_);
        output_names_.emplace_back(name_ptr.get());

        auto type_info   = session_->GetOutputTypeInfo(i);
        auto shape_info  = type_info.GetTensorTypeAndShapeInfo();
        auto ort_shape   = shape_info.GetShape();
        auto ort_dtype   = shape_info.GetElementType();

        utils::TensorInfo info;
        info.name  = output_names_.back();
        info.dtype = OrtDtypeToAtlas(ort_dtype);
        for (int64_t d : ort_shape) info.shape.push_back(static_cast<int>(d));
        output_info_.push_back(std::move(info));
    }

    return utils::ErrorCode::kOk;
}

}  // namespace backend
}  // namespace atlas

ATLAS_REGISTER_BACKEND("cpu", atlas::backend::CpuBackend)
