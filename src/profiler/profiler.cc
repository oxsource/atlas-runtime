#include "src/profiler/profiler.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "src/profiler/profile_config.h"

#define LOG_TAG "Atlas::Profiler"
#include "src/utils/logger.h"

namespace atlas {
namespace backend {

namespace {

// Counter file stored inside the output directory to persist the run counter
// across process restarts.  Each run atomically reads and increments it.
constexpr const char* kCounterFile = "_counter";

// Lock file for atomic counter read-increment-store on systems that don't
// have an atomic filesystem rename.  We use a simple rename-based lock.
constexpr const char* kCounterLock = "_counter.lock";

// Reads the current counter value from the counter file.
// Returns 0 if the file does not exist or is unreadable.
int ReadCounter(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (f == nullptr) return 0;
    char buf[32] = {};
    if (std::fgets(buf, sizeof(buf), f) == nullptr) {
        std::fclose(f);
        return 0;
    }
    std::fclose(f);
    char* end = nullptr;
    const long val = std::strtol(buf, &end, 10);
    if (end == nullptr || *end == '\0' || *end == '\n') {
        return (val >= 0 && val <= INT_MAX) ? static_cast<int>(val) : 0;
    }
    return 0;
}

// Writes |counter| to the counter file.
void WriteCounter(const std::string& path, int counter) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (f == nullptr) {
        ATLAS_LOGE("Profiler: failed to write counter to %s", path.c_str());
        return;
    }
    std::fprintf(f, "%d\n", counter);
    std::fclose(f);
}

// Atomically read, increment, and return the next counter value.
// Uses a lock file + rename to ensure atomicity across processes.
int NextCounter(const std::string& dir) {
    const std::string counter_path = dir + "/" + kCounterFile;
    const std::string lock_path    = dir + "/" + kCounterLock;

    // Spin until we acquire the lock.
    // A simple loop is fine since the critical section is < 1 ms.
    for (int retry = 0; retry < 100; ++retry) {
        // Try to create the lock file exclusively.
        FILE* lock = std::fopen(lock_path.c_str(), "wx");
        if (lock != nullptr) {
            std::fclose(lock);

            // Lock acquired.
            const int cur = ReadCounter(counter_path);
            const int next = cur + 1;
            WriteCounter(counter_path, next);
            std::remove(lock_path.c_str());
            return next;
        }
        // Lock held by another process — brief backoff.
        ::usleep(1000);  // 1 ms
    }

    // Fallback: just scan the directory.
    ATLAS_LOGW("Profiler: failed to acquire lock after 100 retries, falling back");
    return 0;
}

}  // namespace

// ── Static shared state ──────────────────────────────────────
FILE*                       Profiler::s_shared_fp_      = nullptr;
int                         Profiler::s_instance_count_ = 0;
bool                        Profiler::s_header_written_ = false;
std::vector<ProfileRecord>  Profiler::s_records_;
std::mutex                  Profiler::s_mutex_;
int                         Profiler::s_max_records_    = 100;

Profiler::Profiler(const core::ProfileConfig& config,
                   const std::string& model_id)
    : config_(&config), model_id_(model_id) {
    if (!config_->enabled) return;

    std::lock_guard<std::mutex> lock(s_mutex_);

    if (s_instance_count_ == 0) {
        // First Profiler: open the file and write the CSV header.
        if (config_->output_path.empty()) {
            s_shared_fp_ = stdout;
        } else {
            ::mkdir(config_->output_path.c_str(), 0755);
            const int counter = NextCounter(config_->output_path);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s/profile_%03d.csv",
                          config_->output_path.c_str(), counter);
            s_shared_fp_ = std::fopen(buf, "w");
            if (s_shared_fp_ == nullptr) {
                ATLAS_LOGE("Profiler: failed to open %s", buf);
                s_shared_fp_ = stdout;
            } else {
                ATLAS_LOGD("Profiler: writing to %s", buf);
            }
        }
        // Write CSV header once.
        if (s_shared_fp_ != nullptr) {
            std::fputs("model_id,phase,step,duration_ms,timestamp_ms\n",
                       s_shared_fp_);
        }
        s_header_written_ = true;
        s_max_records_ = config_->max_records;
    }

    fp_ = s_shared_fp_;
    s_instance_count_++;
}

Profiler::~Profiler() {
    if (!config_->enabled) return;

    std::lock_guard<std::mutex> lock(s_mutex_);
    s_instance_count_--;
    if (s_instance_count_ == 0) {
        // Last Profiler: flush remaining records and close.
        FlushAll();
        if (s_shared_fp_ != nullptr && s_shared_fp_ != stdout) {
            std::fclose(s_shared_fp_);
        }
        s_shared_fp_      = nullptr;
        s_header_written_ = false;
        s_records_.clear();
    }
}

bool Profiler::ShouldProfile(const std::string& phase) const {
    return config_->enabled &&
           core::ProfileModulesContain(config_->modules, phase);
}

void Profiler::Push(const std::string& phase, const std::string& step,
                     double duration_ms) {
    if (fp_ == nullptr) return;

    std::lock_guard<std::mutex> lock(s_mutex_);
    s_records_.push_back({model_id_, phase, step, duration_ms, NowMs()});

    // Auto-flush if max_records reached (0 means no limit).
    if (s_max_records_ > 0 &&
        static_cast<int>(s_records_.size()) >= s_max_records_) {
        FlushAll();
    }
}

void Profiler::FlushAll() {
    if (s_shared_fp_ == nullptr || s_records_.empty()) return;

    // Flush must be called with s_mutex_ already held.
    for (const auto& r : s_records_) {
        std::fprintf(s_shared_fp_, "%s,%s,%s,%.3f,%lld\n",
                     r.model_id.c_str(), r.phase.c_str(), r.step.c_str(),
                     r.duration_ms, static_cast<long long>(r.timestamp_ms));
    }
    std::fflush(s_shared_fp_);
    s_records_.clear();
}

// static
int64_t Profiler::NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// static
double Profiler::NowSteadyMs() {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace backend
}  // namespace atlas