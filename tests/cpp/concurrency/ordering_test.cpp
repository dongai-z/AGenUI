// OR*: ordering tests for the worker thread queue.

#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "agenui_engine.h"
#include "agenui_surface_manager_interface.h"
#include "support/fixture_loader.h"
#include "support/mock_message_listener.h"
#include "support/scoped_surface_manager.h"
#include "support/test_env.h"
#include "support/thread_sync_helper.h"

namespace {

// OR001: begin → chunk → end is processed in order; the resulting
// onCreateSurface callback observes the full envelope.
TEST(OrderingTest, OR001_BeginChunkEnd_OrderPreserved) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ::agenui::testing::MockMessageListener listener;
    sm->addSurfaceEventListener(&listener);

    auto json = ::agenui::testing::LoadFixtureOrEmpty(
        "protocol/create_surface.json");
    ASSERT_FALSE(json.empty());
    sm->beginTextStream();
    sm->receiveTextChunk(json);
    sm->endTextStream();
    EXPECT_TRUE(listener.waitFor(
        [&]() { return !listener.createSurfaceCalls.empty(); }, 2000));

    sm->removeSurfaceEventListener(&listener);
}

// OR002: many small chunks are reassembled in order.
TEST(OrderingTest, OR002_ManySmallChunks_FIFOPreserved) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ::agenui::testing::MockMessageListener listener;
    sm->addSurfaceEventListener(&listener);

    auto json = ::agenui::testing::LoadFixtureOrEmpty(
        "protocol/create_surface.json");
    ASSERT_FALSE(json.empty());
    sm->beginTextStream();
    for (size_t i = 0; i < json.size(); i += 4) {
        sm->receiveTextChunk(json.substr(i, 4));
    }
    sm->endTextStream();
    EXPECT_TRUE(listener.waitFor(
        [&]() { return !listener.createSurfaceCalls.empty(); }, 5000));

    sm->removeSurfaceEventListener(&listener);
}

}  // namespace
