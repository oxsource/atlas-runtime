// Copyright 2026 The Atlas Authors
// Minimal Android native example that only emits logs.

#include <cstdio>
#include <string>

#define LOG_TAG "android_log"
#include "src/utils/logger.h"

int main(int argc, char* argv[]) {
    atlas::utils::Logger::SetLevel(atlas::utils::Logger::Level::Debug);

    ATLAS_LOGD("example started");
    ATLAS_LOGD("argc=%d", argc);
    for (int i = 0; i < argc; ++i) {
        ATLAS_LOGW("argv[%d]=%s", i, argv[i]);
    }
    ATLAS_LOGD("example finished");

    std::printf("example finished\n");
    return 0;
}