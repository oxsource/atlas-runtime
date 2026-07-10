#include "src/backend/base/profiling_backend.h"

#include <string>
#include <vector>

#define LOG_TAG "Atlas::ProfilingBE"
#include "src/utils/logger.h"

namespace atlas {
namespace backend {

ProfilingBackend::ProfilingBackend(std::unique_ptr<IBackend> inner,
                                   Profiler* profiler)
    : inner_(std::move(inner)), profiler_(profiler) {
    ATLAS_LOGD("ProfilingBackend created");
}

ProfilingBackend::~ProfilingBackend() {
    if (profiler_) profiler_->Flush();
}

utils::ErrorCode ProfilingBackend::Load(const std::string& model_path,
                                         const core::ModelConfig& config,
                                         IBackendContext* ctx) {
    const double t0 = Profiler::NowSteadyMs();
    utils::ErrorCode ret = inner_->Load(model_path, config, ctx);
    const double elapsed = Profiler::NowSteadyMs() - t0;

    if (ShouldProfile(kProfilePhaseLoad)) {
        profiler_->BufferRecord(kProfilePhaseLoad, kProfileStepTotal, elapsed);
    }

    return ret;
}

utils::ErrorCode ProfilingBackend::Infer(
    const std::vector<utils::Tensor>& inputs,
    std::vector<utils::Tensor>& outputs) {
    const double t0 = Profiler::NowSteadyMs();
    utils::ErrorCode ret = inner_->Infer(inputs, outputs);
    const double elapsed = Profiler::NowSteadyMs() - t0;

    if (ShouldProfile(kProfilePhaseInfer)) {
        profiler_->BufferRecord(kProfilePhaseInfer, kProfileStepForward, elapsed);
    }

    return ret;
}

void ProfilingBackend::Unload() {
    const double t0 = Profiler::NowSteadyMs();
    inner_->Unload();
    const double elapsed = Profiler::NowSteadyMs() - t0;

    if (ShouldProfile(kProfilePhaseUnload)) {
        profiler_->BufferRecord(kProfilePhaseUnload, kProfileStepTotal, elapsed);
    }

    if (profiler_) profiler_->Flush();
}

std::vector<utils::TensorInfo> ProfilingBackend::GetInputInfo() const {
    return inner_->GetInputInfo();
}

std::vector<utils::TensorInfo> ProfilingBackend::GetOutputInfo() const {
    return inner_->GetOutputInfo();
}

utils::Span<void> ProfilingBackend::GetInputBuffer(size_t index) const {
    return inner_->GetInputBuffer(index);
}

utils::Span<void> ProfilingBackend::GetOutputBuffer(size_t index) const {
    return inner_->GetOutputBuffer(index);
}

utils::ErrorCode ProfilingBackend::SetInputBuffer(size_t index,
                                                   void* external_mem,
                                                   size_t byte_size) {
    return inner_->SetInputBuffer(index, external_mem, byte_size);
}

bool ProfilingBackend::IsLoaded() const { return inner_->IsLoaded(); }

std::string ProfilingBackend::Version() const { return inner_->Version(); }

bool ProfilingBackend::ShouldProfile(const std::string& phase) const {
    return profiler_ && profiler_->ShouldProfile(phase);
}

}  // namespace backend
}  // namespace atlas
