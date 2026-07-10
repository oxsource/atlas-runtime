// Copyright 2025 The Atlas Authors
//
// Unit tests for SnpeMemoryPool — header-only, no SNPE SDK dependency,
// compiles cleanly on any platform (macOS, Linux x86_64, WSL).
//
// Build:  bazel test //tests/backend/snpe:snpe_memory_pool_test

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "src/backend/snpe/snpe_memory_pool.h"

namespace atlas {
namespace backend {
namespace {

// ============================================================================
// Path A — Free-list reuse (Acquire / Release)
// ============================================================================

TEST(SnpeMemoryPoolTest, AcquireZeroSize) {
    SnpeMemoryPool pool;
    auto buf = pool.Acquire(0);
    EXPECT_EQ(buf.data, nullptr);
    EXPECT_EQ(buf.size, 0u);
}

TEST(SnpeMemoryPoolTest, AcquireReleaseReuse) {
    SnpeMemoryPool pool;

    auto buf_a = pool.Acquire(64);
    ASSERT_NE(buf_a.data, nullptr);
    EXPECT_GE(buf_a.size, 64u);

    void* ptr_a = buf_a.data;

    // Release and acquire again with the same size — pool reuses the memory.
    pool.Release(std::move(buf_a));

    auto buf_b = pool.Acquire(64);
    ASSERT_NE(buf_b.data, nullptr);
    EXPECT_GE(buf_b.size, 64u);
    EXPECT_EQ(buf_b.data, ptr_a);
}

TEST(SnpeMemoryPoolTest, AcquireBestFit) {
    SnpeMemoryPool pool;

    // Pre-allocate size=64 and size=256.
    pool.PreAllocate({64, 256});

    // Request size=128 → best fit is the 256 buffer.
    auto buf = pool.Acquire(128);
    ASSERT_NE(buf.data, nullptr);
    EXPECT_GE(buf.size, 256u);
}

TEST(SnpeMemoryPoolTest, PreAllocate) {
    SnpeMemoryPool pool;

    pool.PreAllocate({32, 64, 128});

    auto buf_32 = pool.Acquire(32);
    ASSERT_NE(buf_32.data, nullptr);
    EXPECT_GE(buf_32.size, 32u);

    auto buf_64 = pool.Acquire(64);
    ASSERT_NE(buf_64.data, nullptr);
    EXPECT_GE(buf_64.size, 64u);

    auto buf_128 = pool.Acquire(128);
    ASSERT_NE(buf_128.data, nullptr);
    EXPECT_GE(buf_128.size, 128u);
}

// ============================================================================
// Path B — Shared buffers (AcquireShared / ReleaseShared)
// ============================================================================

TEST(SnpeMemoryPoolTest, AcquireSharedFirstCaller) {
    SnpeMemoryPool pool;

    void* ptr = pool.AcquireShared("key1", 128);
    ASSERT_NE(ptr, nullptr);

    // First caller — refcount is 1.  No crash on release.
    pool.ReleaseShared("key1");
}

TEST(SnpeMemoryPoolTest, AcquireSharedSecondCaller) {
    SnpeMemoryPool pool;

    void* ptr_a = pool.AcquireShared("shared", 256);
    ASSERT_NE(ptr_a, nullptr);

    void* ptr_b = pool.AcquireShared("shared", 256);
    EXPECT_EQ(ptr_b, ptr_a);  // Same memory, refcount=2.

    pool.ReleaseShared("shared");  // refcount=1
    pool.ReleaseShared("shared");  // refcount=0 → moved to free list
}

TEST(SnpeMemoryPoolTest, ReleaseSharedDecRef) {
    SnpeMemoryPool pool;

    void* ptr_a = pool.AcquireShared("k", 128);
    ASSERT_NE(ptr_a, nullptr);

    void* ptr_b = pool.AcquireShared("k", 128);
    EXPECT_EQ(ptr_b, ptr_a);

    // Release once: still alive.
    pool.ReleaseShared("k");

    // Acquire again: should return the same pointer (still alive).
    void* ptr_c = pool.AcquireShared("k", 128);
    EXPECT_EQ(ptr_c, ptr_a);

    // Drain both references.
    pool.ReleaseShared("k");
    pool.ReleaseShared("k");
}

TEST(SnpeMemoryPoolTest, Clear) {
    SnpeMemoryPool pool;

    auto buf = pool.Acquire(256);
    ASSERT_NE(buf.data, nullptr);
    size_t bytes_before = pool.TotalAllocatedBytes();
    EXPECT_GT(bytes_before, 0u);

    pool.Clear();

    // After Clear, total_allocated is reset to 0.
    EXPECT_EQ(pool.TotalAllocatedBytes(), 0u);

    // Acquire after Clear should allocate fresh.
    auto buf2 = pool.Acquire(256);
    ASSERT_NE(buf2.data, nullptr);
    EXPECT_GE(pool.TotalAllocatedBytes(), 256u);
}

TEST(SnpeMemoryPoolTest, TotalAllocatedBytes) {
    SnpeMemoryPool pool;

    pool.AcquireShared("a", 512);
    pool.AcquireShared("b", 256);

    // Total should be at least 512 + 256 = 768.
    EXPECT_GE(pool.TotalAllocatedBytes(), 768u);

    pool.ReleaseShared("a");
    pool.ReleaseShared("b");
}

TEST(SnpeMemoryPoolTest, ThreadSafety) {
    SnpeMemoryPool pool;

    // AcquireShared the same key from 4 concurrent threads.
    std::vector<void*> results(4, nullptr);
    std::vector<std::thread> threads;

    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&pool, &results, i]() {
            results[i] = pool.AcquireShared("thread_safe_key", 1024);
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // All threads got the same pointer.
    void* ref = results[0];
    ASSERT_NE(ref, nullptr);
    for (int i = 1; i < 4; ++i) {
        EXPECT_EQ(results[i], ref);
    }

    // Now release all 4 refs.
    for (int i = 0; i < 4; ++i) {
        pool.ReleaseShared("thread_safe_key");
    }
}

}  // namespace
}  // namespace backend
}  // namespace atlas
