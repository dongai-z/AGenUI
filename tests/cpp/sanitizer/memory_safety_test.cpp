// ASA*: memory safety tests. Run under ASan/LSan/UBSan.

#include <gtest/gtest.h>

#include "agenui_engine.h"
#include "agenui_engine_entry.h"
#include "support/scoped_surface_manager.h"
#include "support/test_env.h"
#include "support/thread_sync_helper.h"

namespace {

// ASA001: rapid SurfaceManager create / destroy.
TEST(MemorySafetyTest, ASA001_RapidSurfaceManagerCreateDestroy_NoLeak) {
    auto* engine = ::agenui::testing::GetEngine();
    ASSERT_NE(engine, nullptr);
    constexpr int kIters = 100;
    for (int i = 0; i < kIters; ++i) {
        auto* sm = engine->createSurfaceManager();
        ASSERT_NE(sm, nullptr);
        engine->destroySurfaceManager(sm);
    }
    ::agenui::testing::WaitForWorkerIdle();
}

// ASA002 (historical): "double destroy of engine then re-init" was removed.
// Reason: AGenUIEngine is a process-level singleton with init/destroy each
// allowed at most once per process. The global Environment owns that
// lifecycle (support/test_env.h). Tests must not destroy the engine
// mid-suite.

// ASA003: very long protocol payload.
TEST(MemorySafetyTest, ASA003_LongComponentValue_NoOverrun) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);
    std::string text(65536, 'x');
    std::string json = R"({"version":"v0.9","createSurface":{"surfaceId":"asa003","catalogId":")"
                       + text + R"(","theme":{},"sendDataModel":false,"animated":true}})";
    sm->beginTextStream();
    sm->receiveTextChunk(json);
    sm->endTextStream();
    ::agenui::testing::WaitForWorkerIdle();
}

// ASA004: rapid begin/end stream loop.
TEST(MemorySafetyTest, ASA004_RapidBeginEndStream_NoLeak) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);
    for (int i = 0; i < 1000; ++i) {
        sm->beginTextStream();
        sm->endTextStream();
    }
    ::agenui::testing::WaitForWorkerIdle();
}

}  // namespace
