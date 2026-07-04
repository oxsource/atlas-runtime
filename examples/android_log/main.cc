// Copyright 2026 The Atlas Authors
// Minimal Android native example that only emits logs.

#include <cstdio>

#if defined(ATLAS_ANDROID_LOG_ENABLE_SNPE_VERSION)
#include "SNPE/SNPEFactory.hpp"
#endif

#define LOG_TAG "android_log"
#include "src/utils/logger.h"

int main(int argc, char* argv[]) {
    atlas::utils::Logger::SetLevel(atlas::utils::Logger::Level::Debug);
    ATLAS_LOGD("example started");

#if defined(ATLAS_ANDROID_LOG_ENABLE_SNPE_VERSION)
    const auto version = zdl::SNPE::SNPEFactory::getLibraryVersion();
    ATLAS_LOGD("SNPE version: %s", version.toString().c_str());
#endif

    ATLAS_LOGD("argc=%d", argc);
    for (int i = 0; i < argc; ++i) {
        ATLAS_LOGW("argv[%d]=%s", i, argv[i]);
    }
    ATLAS_LOGD("example finished");
    return 0;
}