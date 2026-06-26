#pragma once

// Symbol export / import macros for shared library ABI control.
//
// When building libatlas as a shared library, ATLAS_SHARED_LIBRARY is defined
// by the build system (Bazel defines / CMake add_compile_definitions), causing
// all ATLAS_API-decorated symbols to be exported with default visibility.
//
// Consumers of the shared library include this header without defining
// ATLAS_SHARED_LIBRARY, so ATLAS_API resolves to an import hint on Windows
// or a no-op on Linux/macOS (where import is implicit).
//
// All translation units are compiled with -fvisibility=hidden by default;
// only symbols marked ATLAS_API are exported.

#if defined(_WIN32)
  #if defined(ATLAS_SHARED_LIBRARY)
    #define ATLAS_API __declspec(dllexport)
  #else
    #define ATLAS_API __declspec(dllimport)
  #endif
#else
  #if defined(ATLAS_SHARED_LIBRARY)
    #define ATLAS_API __attribute__((visibility("default")))
  #else
    #define ATLAS_API
  #endif
#endif
