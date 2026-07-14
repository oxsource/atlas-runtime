// Copyright 2026 The Atlas Authors
// Minimal Android native example that only emits logs.

#include <string>

#include "src/backend/snpe/snpe_backend_context.h"
#include "src/backend/snpe/snpe_backend.h"

#define LOG_TAG "android_log"
#include "src/utils/logger.h"

int main(int argc, char* argv[]) {
    atlas::utils::Logger::SetLevel(atlas::utils::Logger::Level::Debug);
    ATLAS_LOGD("example started");

    atlas::backend::snpe::SnpeBackendContext backend_context;
    atlas::backend::snpe::SnpeBackend backend;
    const std::string backend_version =
        std::string(backend_context.BackendType()) + "-" + backend.Version();
    ATLAS_LOGD("snpe backend version: %s", backend_version.c_str());

    ATLAS_LOGD("argc=%d", argc);
    for (int i = 0; i < argc; ++i) {
        ATLAS_LOGW("argv[%d]=%s", i, argv[i]);
    }
    ATLAS_LOGD("example finished");
    return 0;
}