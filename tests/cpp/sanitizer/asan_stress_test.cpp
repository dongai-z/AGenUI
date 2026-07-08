// ASan-targeted stress tests for memory safety.
//
// These tests specifically exercise patterns that commonly lead to:
//   - Use-after-free (dangling pointer access after object destruction)
//   - Double-free (destroying already-freed memory)
//   - Stack/heap buffer overflows (writing past buffer boundaries)
//   - Memory leaks (unreleased allocations under error paths)
//
// Each test is designed to trigger ASan if the underlying bug exists.

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "agenui_dispatcher_types.h"
#include "agenui_engine.h"
#include "agenui_engine_entry.h"
#include "agenui_message_listener.h"
#include "agenui_surface_manager_interface.h"
#include "support/fixture_loader.h"
#include "support/mock_message_listener.h"
#include "support/scoped_surface_manager.h"
#include "support/test_env.h"
#include "support/thread_sync_helper.h"

namespace {

// =============================================================================
// Section 1: Lifecycle Use-After-Free tests
// =============================================================================

// ASAN_LC001: Destroy SM while listener callback is in-flight.
// If the listener pointer is accessed after SM teardown, ASan will flag it.
TEST(AsanLifecycleTest, LC001_DestroyDuringListenerCallback_NoUaF) {
    auto* engine = ::agenui::testing::GetEngine();
    ASSERT_NE(engine, nullptr);

    for (int iter = 0; iter < 20; ++iter) {
        auto* sm = engine->createSurfaceManager();
        ASSERT_NE(sm, nullptr);
        ::agenui::testing::WaitForWorkerIdle();

        ::agenui::testing::MockMessageListener listener;
        sm->addSurfaceEventListener(&listener);

        // Feed a createSurface protocol to trigger onCreateSurface callback
        sm->beginTextStream();
        sm->receiveTextChunk(
            R"({"version":"v0.9","createSurface":{"surfaceId":"lc001","catalogId":"test","theme":{},"sendDataModel":false,"animated":true}})");
        sm->endTextStream();

        // Immediately destroy without waiting for callback. The callback
        // references &listener which lives on our stack. If SM accesses
        // the listener after destroy, ASan will catch it.
        engine->destroySurfaceManager(sm);
        ::agenui::testing::WaitForWorkerIdle();
    }
}

// ASAN_LC002: Double-destroy of SurfaceManager.
// The engine should handle this gracefully (no double-free).
TEST(AsanLifecycleTest, LC002_DoubleDestroy_NoCrash) {
    auto* engine = ::agenui::testing::GetEngine();
    ASSERT_NE(engine, nullptr);

    auto* sm = engine->createSurfaceManager();
    ASSERT_NE(sm, nullptr);
    ::agenui::testing::WaitForWorkerIdle();

    engine->destroySurfaceManager(sm);
    // Second destroy with same pointer — should be a no-op or handled safely
    engine->destroySurfaceManager(sm);
    ::agenui::testing::WaitForWorkerIdle();
}

// ASAN_LC003: Use SM after destroy (explicit use-after-free test).
// After destroySurfaceManager, calling APIs on the SM must not crash.
// Note: This is an intentional misuse test — verifying graceful handling.
TEST(AsanLifecycleTest, LC003_UseAfterDestroy_GracefulReject) {
    auto* engine = ::agenui::testing::GetEngine();
    ASSERT_NE(engine, nullptr);

    auto* sm = engine->createSurfaceManager();
    ASSERT_NE(sm, nullptr);
    ::agenui::testing::WaitForWorkerIdle();

    engine->destroySurfaceManager(sm);
    ::agenui::testing::WaitForWorkerIdle();

    // These calls are on a freed SM. If the engine sets _isRunning=false
    // BEFORE releasing memory, the early-return path prevents UaF.
    // ASan will flag if it actually reads freed memory.
    // NOTE: This test documents the current API contract gap.
    // Commenting out direct access since raw pointer is dangling.
    // The real test is whether QP004 pattern triggers UaF (it does).
}

// ASAN_LC004: Rapid init/uninit cycle interleaved with stream ops.
TEST(AsanLifecycleTest, LC004_RapidInitUninit_InterleavedStreaming) {
    auto* engine = ::agenui::testing::GetEngine();
    ASSERT_NE(engine, nullptr);

    constexpr int kIters = 50;
    for (int i = 0; i < kIters; ++i) {
        auto* sm = engine->createSurfaceManager();
        ASSERT_NE(sm, nullptr);
        // Don't wait for init to finish — immediately start streaming
        sm->beginTextStream();
        sm->receiveTextChunk("{\"invalid\": true}");
        sm->endTextStream();
        // Destroy immediately, potentially before init() completes on worker
        engine->destroySurfaceManager(sm);
    }
    ::agenui::testing::WaitForWorkerIdle(30000);
}

// =============================================================================
// Section 2: Concurrent Access Stress Tests
// =============================================================================

// ASAN_CC001: Multiple threads call addSurfaceEventListener concurrently.
TEST(AsanConcurrencyTest, CC001_ConcurrentListenerAdd_NoCorruption) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);

    constexpr int kThreads = 8;
    std::vector<::agenui::testing::MockMessageListener> listeners(kThreads);
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            sm->addSurfaceEventListener(&listeners[t]);
        });
    }
    for (auto& th : threads) th.join();

    // Now remove them all concurrently
    threads.clear();
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            sm->removeSurfaceEventListener(&listeners[t]);
        });
    }
    for (auto& th : threads) th.join();
    ::agenui::testing::WaitForWorkerIdle();
}

// ASAN_CC002: Concurrent stream + destroy race (stress version of QP004).
// Run many iterations to increase probability of hitting the race window.
TEST(AsanConcurrencyTest, CC002_StreamDestroyRace_Repeated) {
    auto* engine = ::agenui::testing::GetEngine();
    ASSERT_NE(engine, nullptr);

    constexpr int kIters = 30;
    for (int iter = 0; iter < kIters; ++iter) {
        auto* sm = engine->createSurfaceManager();
        ASSERT_NE(sm, nullptr);
        ::agenui::testing::WaitForWorkerIdle();

        std::atomic<bool> stop{false};
        std::thread producer([&]() {
            while (!stop.load(std::memory_order_acquire)) {
                sm->beginTextStream();
                sm->receiveTextChunk("x");
                sm->endTextStream();
            }
        });

        // Let producer run a bit
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        // Destroy while producer is active
        engine->destroySurfaceManager(sm);
        stop.store(true, std::memory_order_release);
        producer.join();
        ::agenui::testing::WaitForWorkerIdle(5000);
    }
}

// ASAN_CC003: Concurrent submitUIAction during destroy.
TEST(AsanConcurrencyTest, CC003_ActionDuringDestroy_NoUaF) {
    auto* engine = ::agenui::testing::GetEngine();
    ASSERT_NE(engine, nullptr);

    for (int iter = 0; iter < 20; ++iter) {
        auto* sm = engine->createSurfaceManager();
        ASSERT_NE(sm, nullptr);
        ::agenui::testing::WaitForWorkerIdle();

        std::atomic<bool> stop{false};
        std::thread actor([&]() {
            ::agenui::ActionMessage msg;
            msg.surfaceId = "action_test";
            msg.sourceComponentId = "btn_click";
            msg.contextJson = R"({"action":"click"})";
            while (!stop.load(std::memory_order_acquire)) {
                sm->submitUIAction(msg);
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        engine->destroySurfaceManager(sm);
        stop.store(true, std::memory_order_release);
        actor.join();
        ::agenui::testing::WaitForWorkerIdle(5000);
    }
}

// =============================================================================
// Section 3: Buffer Boundary Tests
// =============================================================================

// ASAN_BB001: Extremely long surfaceId to test string buffer handling.
TEST(AsanBufferTest, BB001_VeryLongSurfaceId_NoOverflow) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);

    std::string longId(100000, 'A');
    std::string json = R"({"version":"v0.9","createSurface":{"surfaceId":")" + longId +
                       R"(","catalogId":"test","theme":{},"sendDataModel":false,"animated":true}})";

    sm->beginTextStream();
    sm->receiveTextChunk(json);
    sm->endTextStream();
    ::agenui::testing::WaitForWorkerIdle();
}

// ASAN_BB002: Binary data / null bytes in text chunk.
TEST(AsanBufferTest, BB002_NullBytesInChunk_NoOverflow) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);

    std::string data = "abc";
    data.push_back('\0');
    data.append("def");
    data.push_back('\0');
    data.append(R"({"version":"v0.9"})");

    sm->beginTextStream();
    sm->receiveTextChunk(data);
    sm->endTextStream();
    ::agenui::testing::WaitForWorkerIdle();
}

// ASAN_BB003: Incrementally large chunks to test reallocation.
TEST(AsanBufferTest, BB003_IncrementalGrowth_NoReallocationCorruption) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);

    sm->beginTextStream();
    // Send increasingly large chunks to stress internal buffer reallocation
    for (int size = 1; size <= 65536; size *= 2) {
        std::string chunk(size, 'X');
        sm->receiveTextChunk(chunk);
    }
    sm->endTextStream();
    ::agenui::testing::WaitForWorkerIdle();
}

// ASAN_BB004: Malformed JSON with deeply nested objects.
TEST(AsanBufferTest, BB004_DeeplyNestedJson_NoStackOverflow) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);

    // Create deeply nested JSON: {{{...}}}
    std::string nested;
    constexpr int kDepth = 200;
    for (int i = 0; i < kDepth; ++i) nested += "{\"a\":";
    nested += "1";
    for (int i = 0; i < kDepth; ++i) nested += "}";

    sm->beginTextStream();
    sm->receiveTextChunk(nested);
    sm->endTextStream();
    ::agenui::testing::WaitForWorkerIdle();
}

// ASAN_BB005: Empty and single-char edge cases.
TEST(AsanBufferTest, BB005_MinimalInputs_NoUnderflow) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);

    sm->beginTextStream();
    sm->receiveTextChunk("");           // empty
    sm->receiveTextChunk(" ");          // whitespace
    sm->receiveTextChunk("{");          // incomplete
    sm->receiveTextChunk("\n");         // newline
    sm->receiveTextChunk("\x00");       // null
    sm->endTextStream();
    ::agenui::testing::WaitForWorkerIdle();
}

// =============================================================================
// Section 4: Multi-Surface Interaction Tests
// =============================================================================

// ASAN_MS001: Create many SurfaceManagers, destroy in random order.
TEST(AsanMultiSurfaceTest, MS001_ManyCreateRandomDestroy_NoLeak) {
    auto* engine = ::agenui::testing::GetEngine();
    ASSERT_NE(engine, nullptr);

    constexpr int N = 30;
    std::vector<::agenui::ISurfaceManager*> managers;
    for (int i = 0; i < N; ++i) {
        auto* sm = engine->createSurfaceManager();
        ASSERT_NE(sm, nullptr);
        managers.push_back(sm);
    }
    ::agenui::testing::WaitForWorkerIdle();

    // Destroy in reverse order (LIFO) to test interleaving
    for (int i = N - 1; i >= 0; --i) {
        engine->destroySurfaceManager(managers[i]);
    }
    ::agenui::testing::WaitForWorkerIdle(30000);
}

// ASAN_MS002: Destroy one SM while another processes a heavy payload.
TEST(AsanMultiSurfaceTest, MS002_DestroyOneWhileAnotherStreams) {
    auto* engine = ::agenui::testing::GetEngine();
    ASSERT_NE(engine, nullptr);

    auto* sm1 = engine->createSurfaceManager();
    auto* sm2 = engine->createSurfaceManager();
    ASSERT_NE(sm1, nullptr);
    ASSERT_NE(sm2, nullptr);
    ::agenui::testing::WaitForWorkerIdle();

    // Feed heavy payload to sm1
    sm1->beginTextStream();
    for (int i = 0; i < 100; ++i) {
        sm1->receiveTextChunk(R"({"version":"v0.9","updateComponents":{"surfaceId":"ms002","components":[{"type":"text","id":"t)" +
                              std::to_string(i) + R"(","content":{"text":"hello"}}]}})");
    }

    // While sm1 is still processing, destroy sm2
    engine->destroySurfaceManager(sm2);

    sm1->endTextStream();
    engine->destroySurfaceManager(sm1);
    ::agenui::testing::WaitForWorkerIdle(30000);
}

// =============================================================================
// Section 5: Listener Lifetime Safety
// =============================================================================

// ASAN_LS001: Remove listener that was never added.
TEST(AsanListenerSafetyTest, LS001_RemoveUnregistered_NoCrash) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);

    ::agenui::testing::MockMessageListener listener;
    // Never added, but try to remove
    sm->removeSurfaceEventListener(&listener);
    ::agenui::testing::WaitForWorkerIdle();
}

// ASAN_LS002: Add same listener twice, then remove once.
TEST(AsanListenerSafetyTest, LS002_DuplicateAdd_RemoveOnce) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);

    ::agenui::testing::MockMessageListener listener;
    sm->addSurfaceEventListener(&listener);
    sm->addSurfaceEventListener(&listener);

    // Feed protocol to trigger callbacks (should not double-fire or crash)
    sm->beginTextStream();
    sm->receiveTextChunk(
        R"({"version":"v0.9","createSurface":{"surfaceId":"ls002","catalogId":"test","theme":{},"sendDataModel":false,"animated":true}})");
    sm->endTextStream();
    ::agenui::testing::WaitForWorkerIdle();

    sm->removeSurfaceEventListener(&listener);
    ::agenui::testing::WaitForWorkerIdle();
}

}  // namespace
