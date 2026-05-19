// UA*: SubmitUIAction / SubmitUIDataModel tests.

#include <gtest/gtest.h>

#include "agenui_dispatcher_types.h"
#include "agenui_engine.h"
#include "agenui_engine_entry.h"
#include "agenui_surface_manager_interface.h"
#include "support/fixture_loader.h"
#include "support/mock_message_listener.h"
#include "support/scoped_surface_manager.h"
#include "support/test_env.h"
#include "support/thread_sync_helper.h"

namespace {

class UIActionTest : public ::testing::Test {
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
};

// UA001: surfaceId / componentId empty path triggers no callback (logged
// only). We verify safety here, not behavior on real components.
TEST_F(UIActionTest, UA001_SubmitUIAction_NoCrash) {
    ::agenui::ActionMessage msg;
    msg.surfaceId = "nonexistent";
    msg.sourceComponentId = "src1";
    msg.contextJson = "{}";
    EXPECT_NO_THROW(sm->submitUIAction(msg));
    ::agenui::testing::WaitForWorkerIdle();
}

// UA002
TEST_F(UIActionTest, UA002_SubmitUIAction_EmptyMessage_NoCrash) {
    ::agenui::ActionMessage msg;
    EXPECT_NO_THROW(sm->submitUIAction(msg));
    ::agenui::testing::WaitForWorkerIdle();
}

// UA003: SubmitUIDataModel against unknown surface is logged but doesn't
// crash.
TEST_F(UIActionTest, UA003_SubmitUIDataModel_NoCrash) {
    ::agenui::SyncUIToDataMessage msg;
    msg.surfaceId = "nonexistent";
    msg.componentId = "comp1";
    msg.change = R"({"value":1})";
    EXPECT_NO_THROW(sm->submitUIDataModel(msg));
    ::agenui::testing::WaitForWorkerIdle();
}

// UA004: action against an existing surface (created via protocol).
TEST_F(UIActionTest, UA004_SubmitUIAction_OnExistingSurface_NoCrash) {
    auto create = ::agenui::testing::LoadFixtureOrEmpty(
        "protocol/create_surface.json");
    ASSERT_FALSE(create.empty());
    sm->beginTextStream();
    sm->receiveTextChunk(create);
    sm->endTextStream();
    EXPECT_TRUE(listener.waitFor(
        [&]() { return !listener.createSurfaceCalls.empty(); }, 2000));

    ::agenui::ActionMessage msg;
    msg.surfaceId = "test-surface-1";
    msg.sourceComponentId = "non_existent_component";
    msg.contextJson = "{}";
    EXPECT_NO_THROW(sm->submitUIAction(msg));
    ::agenui::testing::WaitForWorkerIdle();
}

// UA005: submitUIDataModel ignores empty surfaceId without callback.
TEST_F(UIActionTest, UA005_SubmitUIDataModel_EmptySurfaceId_NoCallback) {
    ::agenui::SyncUIToDataMessage msg;
    msg.surfaceId = "";
    msg.componentId = "c";
    msg.change = "{}";
    EXPECT_NO_THROW(sm->submitUIDataModel(msg));
    ::agenui::testing::WaitForWorkerIdle();
    EXPECT_EQ(listener.totalCalls(), 0);
}

}  // namespace
