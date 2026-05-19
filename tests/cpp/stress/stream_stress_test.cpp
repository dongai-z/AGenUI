// SS*: in-process short stress tests.

#include <gtest/gtest.h>

#include "agenui_engine.h"
#include "support/fixture_loader.h"
#include "support/mock_message_listener.h"
#include "support/scoped_surface_manager.h"
#include "support/test_env.h"
#include "support/thread_sync_helper.h"

namespace {

// SS001
TEST(StreamStressTest, SS001_ManyEnvelopes_NoLeakViaASan) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm) << "engine returned a null SurfaceManager — likely a "
                       "prior test violated the singleton contract";
    ::agenui::testing::MockMessageListener listener;
    sm->addSurfaceEventListener(&listener);

    constexpr int kBatch = 200;
    for (int i = 0; i < kBatch; ++i) {
        std::string json = R"({"version":"v0.9","createSurface":{"surfaceId":"ss001-)"
                           + std::to_string(i)
                           + R"(","catalogId":"x","theme":{},"sendDataModel":false,"animated":true}})";
        sm->beginTextStream();
        sm->receiveTextChunk(json);
        sm->endTextStream();
    }
    EXPECT_TRUE(listener.waitFor(
        [&]() { return listener.createSurfaceCalls.size() >= kBatch; }, 10000));
    EXPECT_EQ(listener.createSurfaceCalls.size(),
              static_cast<size_t>(kBatch));

    sm->removeSurfaceEventListener(&listener);
}

// SS002: large payload split into many chunks.
TEST(StreamStressTest, SS002_LargePayload_OneByteChunks) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm) << "engine returned a null SurfaceManager — likely a "
                       "prior test violated the singleton contract";
    ::agenui::testing::MockMessageListener listener;
    sm->addSurfaceEventListener(&listener);

    auto json = ::agenui::testing::LoadFixtureOrEmpty(
        "protocol/create_surface.json");
    ASSERT_FALSE(json.empty());

    constexpr int kRounds = 30;
    for (int r = 0; r < kRounds; ++r) {
        sm->beginTextStream();
        for (char c : json) sm->receiveTextChunk(std::string(1, c));
        sm->endTextStream();
    }
    EXPECT_TRUE(listener.waitFor(
        [&]() { return !listener.createSurfaceCalls.empty(); }, 10000));

    sm->removeSurfaceEventListener(&listener);
}

}  // namespace
