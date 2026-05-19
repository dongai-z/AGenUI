// ST*: streaming pipeline tests.
//
// We push protocol data through receiveTextChunk and verify that the
// listener receives the corresponding callbacks.

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

#include "agenui_engine.h"
#include "agenui_engine_entry.h"
#include "agenui_surface_manager_interface.h"
#include "support/fixture_loader.h"
#include "support/mock_message_listener.h"
#include "support/scoped_surface_manager.h"
#include "support/test_env.h"
#include "support/thread_sync_helper.h"

namespace {

class StreamingPipelineTest : public ::testing::Test {
protected:
    ::agenui::testing::ScopedSurfaceManager sm;
    ::agenui::testing::MockMessageListener listener;

    void SetUp() override {
        ASSERT_TRUE(sm);
        sm->addSurfaceEventListener(&listener);
    }

    void TearDown() override {
        if (sm) sm->removeSurfaceEventListener(&listener);
    }

    void Drain(int timeoutMs = 1000) {
        ::agenui::testing::WaitForWorkerIdle(timeoutMs);
    }
};

// ST001
TEST_F(StreamingPipelineTest, ST001_BeginEndOnly_NoEventsDispatched) {
    sm->beginTextStream();
    sm->endTextStream();
    Drain();
    EXPECT_EQ(listener.totalCalls(), 0);
}

// ST002
TEST_F(StreamingPipelineTest, ST002_FullCreateSurfaceJson_DispatchesOnCreateSurface) {
    auto json = ::agenui::testing::LoadFixtureOrEmpty("protocol/create_surface.json");
    ASSERT_FALSE(json.empty()) << "fixture missing";
    sm->beginTextStream();
    sm->receiveTextChunk(json);
    sm->endTextStream();

    EXPECT_TRUE(listener.waitFor(
        [&]() { return !listener.createSurfaceCalls.empty(); }, 2000));
    EXPECT_EQ(listener.createSurfaceCalls.size(), 1u);
    if (!listener.createSurfaceCalls.empty()) {
        EXPECT_EQ(listener.createSurfaceCalls.front().surfaceId, "test-surface-1");
    }
}

// ST003: receiveTextChunk without begin still works (compatibility mode).
TEST_F(StreamingPipelineTest, ST003_ReceiveWithoutBegin_StillParses) {
    auto json = ::agenui::testing::LoadFixtureOrEmpty("protocol/create_surface.json");
    ASSERT_FALSE(json.empty());
    sm->receiveTextChunk(json);
    sm->endTextStream();
    EXPECT_TRUE(listener.waitFor(
        [&]() { return !listener.createSurfaceCalls.empty(); }, 2000));
}

// ST004: chunked send.
TEST_F(StreamingPipelineTest, ST004_ChunkedJson_ReassembledAndDispatched) {
    auto json = ::agenui::testing::LoadFixtureOrEmpty("protocol/create_surface.json");
    ASSERT_GT(json.size(), 20u);
    sm->beginTextStream();
    size_t mid = json.size() / 2;
    sm->receiveTextChunk(json.substr(0, mid));
    sm->receiveTextChunk(json.substr(mid));
    sm->endTextStream();
    EXPECT_TRUE(listener.waitFor(
        [&]() { return !listener.createSurfaceCalls.empty(); }, 2000));
}

// ST005: 1 byte at a time.
TEST_F(StreamingPipelineTest, ST005_OneByteChunks_StillParses) {
    auto json = ::agenui::testing::LoadFixtureOrEmpty("protocol/create_surface.json");
    ASSERT_FALSE(json.empty());
    sm->beginTextStream();
    for (char c : json) {
        sm->receiveTextChunk(std::string(1, c));
    }
    sm->endTextStream();
    EXPECT_TRUE(listener.waitFor(
        [&]() { return !listener.createSurfaceCalls.empty(); }, 5000));
}

// ST007: begin in the middle resets buffer.
TEST_F(StreamingPipelineTest, ST007_BeginAgain_ResetsBuffer) {
    auto full = ::agenui::testing::LoadFixtureOrEmpty("protocol/create_surface.json");
    ASSERT_FALSE(full.empty());

    sm->beginTextStream();
    sm->receiveTextChunk(full.substr(0, full.size() / 2));
    // Interrupt.
    sm->beginTextStream();
    sm->receiveTextChunk(full);
    sm->endTextStream();
    EXPECT_TRUE(listener.waitFor(
        [&]() { return !listener.createSurfaceCalls.empty(); }, 2000));
}

// ST008: end after garbage.
TEST_F(StreamingPipelineTest, ST008_EndAfterGarbage_DoesNotCrash) {
    sm->beginTextStream();
    sm->receiveTextChunk("garbage that is not a valid json envelope");
    sm->endTextStream();
    Drain();
    SUCCEED();
}

// ST009: updateComponents protocol applied after createSurface produces
// a `componentsAdd` callback (the diff operation for new components).
TEST_F(StreamingPipelineTest, ST009_UpdateComponentsProtocol_DispatchesComponentsAdd) {
    auto create = ::agenui::testing::LoadFixtureOrEmpty(
        "protocol/create_surface.json");
    auto update = ::agenui::testing::LoadFixtureOrEmpty(
        "protocol/update_components.json");
    ASSERT_FALSE(create.empty());
    ASSERT_FALSE(update.empty());

    sm->beginTextStream();
    sm->receiveTextChunk(create);
    sm->endTextStream();
    EXPECT_TRUE(listener.waitFor(
        [&]() { return !listener.createSurfaceCalls.empty(); }, 2000));

    sm->beginTextStream();
    sm->receiveTextChunk(update);
    sm->endTextStream();
    EXPECT_TRUE(listener.waitFor(
        [&]() { return !listener.componentsAddCalls.empty(); }, 2000));
    if (!listener.componentsAddCalls.empty()) {
        EXPECT_EQ(listener.componentsAddCalls.front().surfaceId,
                  "test-surface-1");
    }
}

// ST010
TEST_F(StreamingPipelineTest, ST010_DeleteSurfaceProtocol_DispatchesOnDeleteSurface) {
    auto json = ::agenui::testing::LoadFixtureOrEmpty(
        "protocol/delete_surface.json");
    ASSERT_FALSE(json.empty());
    sm->beginTextStream();
    sm->receiveTextChunk(json);
    sm->endTextStream();
    EXPECT_TRUE(listener.waitFor(
        [&]() { return !listener.deleteSurfaceCalls.empty(); }, 2000));
}

// ST013
TEST_F(StreamingPipelineTest, ST013_MalformedThenValid_RecoversAndDispatches) {
    sm->beginTextStream();
    sm->receiveTextChunk("##malformed##");
    sm->endTextStream();
    Drain();
    listener.clear();

    auto good = ::agenui::testing::LoadFixtureOrEmpty(
        "protocol/create_surface.json");
    sm->beginTextStream();
    sm->receiveTextChunk(good);
    sm->endTextStream();
    EXPECT_TRUE(listener.waitFor(
        [&]() { return !listener.createSurfaceCalls.empty(); }, 2000));
}

// ST014
TEST_F(StreamingPipelineTest, ST014_EmptyChunk_Ignored) {
    sm->beginTextStream();
    sm->receiveTextChunk("");
    sm->endTextStream();
    Drain();
    EXPECT_EQ(listener.totalCalls(), 0);
}

// ST015: two SMs in parallel, each gets its own data.
TEST_F(StreamingPipelineTest, ST015_MultipleSurfaceManagers_Isolated) {
    auto* engine = ::agenui::testing::GetEngine();
    auto* other = engine->createSurfaceManager();
    ASSERT_NE(other, nullptr);

    ::agenui::testing::MockMessageListener otherListener;
    other->addSurfaceEventListener(&otherListener);

    auto json = ::agenui::testing::LoadFixtureOrEmpty(
        "protocol/create_surface.json");
    sm->beginTextStream();
    sm->receiveTextChunk(json);
    sm->endTextStream();

    other->beginTextStream();
    other->receiveTextChunk(json);
    other->endTextStream();

    EXPECT_TRUE(listener.waitFor(
        [&]() { return !listener.createSurfaceCalls.empty(); }, 2000));
    EXPECT_TRUE(otherListener.waitFor(
        [&]() { return !otherListener.createSurfaceCalls.empty(); }, 2000));

    other->removeSurfaceEventListener(&otherListener);
    engine->destroySurfaceManager(other);
    ::agenui::testing::WaitForWorkerIdle();
}

// ST006: many createSurface envelopes with distinct surfaceIds dispatched.
TEST_F(StreamingPipelineTest, ST006_LargePayload_DispatchesAllEvents) {
    constexpr int kBatch = 5;
    for (int i = 0; i < kBatch; ++i) {
        std::string json = R"({"version":"v0.9","createSurface":{"surfaceId":")"
                           + std::string("payload-") + std::to_string(i)
                           + R"(","catalogId":"x","theme":{},"sendDataModel":false,"animated":true}})";
        sm->beginTextStream();
        sm->receiveTextChunk(json);
        sm->endTextStream();
    }
    EXPECT_TRUE(listener.waitFor(
        [&]() { return listener.createSurfaceCalls.size() >= kBatch; }, 5000));
    EXPECT_EQ(listener.createSurfaceCalls.size(), static_cast<size_t>(kBatch));
}

// ST011/ST012: updateDataModel and componentsAdd/Remove/Update need
// surfaces created first; these are exercised by render_callback_test and
// theme_designtoken_test where surfaces are pre-created. Skipping here to
// keep the test suite focused.

}  // namespace
