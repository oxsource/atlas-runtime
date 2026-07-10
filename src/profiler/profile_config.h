#pragma once

#include <string>

namespace atlas {
namespace core {

// Profiling configuration parsed from the top-level "profile" section of the
// manifest.  All models share the same profiling settings.
//
// |output_path| is treated as a DIRECTORY.  The Profiler creates files named
// {model_id}_NNN.csv inside it, with NNN auto-incremented per program run.
// Empty → stdout.
struct ProfileConfig {
    bool        enabled     = false;
    std::string output_path;           // Output directory (not file path). Empty → stdout.
    std::string modules     = "load,infer";  // Comma-separated: load,infer,unload,all
};

inline bool ProfileModulesContain(const std::string& modules,
                                   const std::string& phase) {
    return modules == "all" || modules.find(phase) != std::string::npos;
}

}  // namespace core
}  // namespace atlas
