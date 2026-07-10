#pragma once

#include <memory>

#include "src/backend/base/i_backend.h"
#include "src/profiler/profiler.h"

namespace atlas {
namespace backend {

// ── ProfilingBackend ─────────────────────────────────────────
//
// Decorator that wraps an IBackend and records timing for Load / Infer / Unload
// via the Profiler owned by ModelEntry.
class ProfilingBackend : public IBackend {
 public:
    ProfilingBackend(std::unique_ptr<IBackend> inner, Profiler* profiler);
    ~ProfilingBackend() override;

    utils::ErrorCode Load(const std::string& model_path,
                          const core::ModelConfig& config,
                          IBackendContext* ctx) override;
    utils::ErrorCode Infer(const std::vector<utils::Tensor>& inputs,
                           std::vector<utils::Tensor>& outputs) override;
    void Unload() override;
    std::vector<utils::TensorInfo> GetInputInfo() const override;
    std::vector<utils::TensorInfo> GetOutputInfo() const override;
    utils::Span<void> GetInputBuffer(size_t index) const override;
    utils::Span<void> GetOutputBuffer(size_t index) const override;
    utils::ErrorCode SetInputBuffer(size_t index, void* external_mem,
                                     size_t byte_size) override;
    bool IsLoaded() const override;
    std::string Version() const override;

 private:
    bool ShouldProfile(const std::string& phase) const;

    std::unique_ptr<IBackend> inner_;
    Profiler*                 profiler_;   // non-owning
};

}  // namespace backend
}  // namespace atlas
