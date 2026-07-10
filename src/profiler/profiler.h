#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace atlas {
namespace core {
struct ProfileConfig;
}  // namespace core

namespace backend {

// ── Profile phase and step name constants ────────────────────
//
// |phase| corresponds to the "modules" field in manifest profile config
// and identifies which subsystem is being profiled (load / infer / unload).
// |step| identifies the sub-operation within that phase.

constexpr const char* kProfilePhaseLoad   = "load";
constexpr const char* kProfilePhaseInfer  = "infer";
constexpr const char* kProfilePhaseUnload = "unload";

constexpr const char* kProfileStepTotal          = "total";
constexpr const char* kProfileStepInputPipeline  = "input_pipeline";
constexpr const char* kProfileStepForward        = "forward";
constexpr const char* kProfileStepOutputPipeline = "output_pipeline";

// One profiled timing event.
struct ProfileRecord {
    std::string model_id;
    std::string phase;
    std::string step;
    double      duration_ms;
    int64_t     timestamp_ms;
};

// ── Profiler ─────────────────────────────────────────────────
//
// Unified profiler for all models in the same process.
//
// All Profiler instances SHARE a single FILE* handle via static members.
// The first constructed Profiler opens the output file (profile_NNN.csv
// under output_path directory, auto-incrementing NNN per run); subsequent
// Profilers reuse the same handle.  The file is closed when the last
// Profiler is destroyed.
//
// config_.output_path is treated as a DIRECTORY (not a file path).
// Empty output_path → stdout.
class Profiler {
 public:
    Profiler(const core::ProfileConfig& config, const std::string& model_id);
    ~Profiler();

    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    // Returns true if |phase| should be profiled according to config.
    bool ShouldProfile(const std::string& phase) const;

    // Writes one CSV line immediately (with fflush).
    void Record(const std::string& phase, const std::string& step,
                double duration_ms);

    // Buffers one record for later batch Flush().
    void BufferRecord(const std::string& phase, const std::string& step,
                      double duration_ms);

    // Writes all buffered records to the output file.
    void Flush();

    // ── Time utilities ────────────────────────────────────────
    static int64_t NowMs();
    static double  NowSteadyMs();

 private:
    // Generates "profile_NNN.csv" under |dir|, scanning for existing files.
    static std::string DetermineOutputPath(const std::string& dir);

    void WriteLine(const std::string& model_id, const std::string& phase,
                   const std::string& step, double duration_ms,
                   int64_t timestamp_ms);

    // ── Shared state (across all Profiler instances) ──────────
    static FILE*  s_shared_fp_;
    static int    s_instance_count_;
    static bool   s_header_written_;

    const core::ProfileConfig*  config_;
    std::string                 model_id_;
    FILE*                       fp_ = nullptr;  // == s_shared_fp_
    std::vector<ProfileRecord>  records_;
};

}  // namespace backend
}  // namespace atlas
