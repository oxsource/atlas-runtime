#pragma once

#include <memory>
#include <string>
#include <vector>

#include "src/backend/base/i_backend.h"
#include "src/core/manifest_config.h"
#include "src/utils/types.h"

namespace atlas {
namespace backend {

// One profiled timing event.
struct ProfileRecord {
    std::string model_id;
    std::string phase;
    std::string step;
    double      duration_ms;
    int64_t     timestamp_ms;
};

// Decorator that wraps an IBackend and records timing for Load / Infer / Unload.
//
// Profiling is enabled via the top-level "profile" section in the manifest.
// Timing data is output as CSV (to stdout or a file) for post-analysis.
//
// All pure-query methods (GetInputInfo, IsLoaded, Version, etc.) are forwarded
// directly to the inner backend without profiling overhead.
class ProfilingBackend : public IBackend {
 public:
    ProfilingBackend(std::unique_ptr<IBackend> inner,
                     core::ProfileConfig config,
                     std::string model_id);
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
    // Checks whether the given phase should be profiled based on modules config.
    bool ShouldProfile(const std::string& phase) const;

    // Records one timing entry.
    void Record(const std::string& phase, const std::string& step,
                double duration_ms, int64_t timestamp_ms);

    // Flushes all buffered records to the output (file or stdout).
    void Flush();

    std::unique_ptr<IBackend>      inner_;
    core::ProfileConfig            config_;
    std::string                    model_id_;
    std::vector<ProfileRecord>     records_;
    bool                           header_written_ = false;
};

}  // namespace backend
}  // namespace atlas
