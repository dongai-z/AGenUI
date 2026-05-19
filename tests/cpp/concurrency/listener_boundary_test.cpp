// LB*: Listener add/remove boundary cases.
//
// Documents and locks down the dispatcher's behavior around tricky
// listener patterns: duplicate registration, self-removal during dispatch,
// add-during-dispatch, listeners that re-enter the SDK, and timing
// around init/uninit.

#include <gtest/gtest.h>

#include <vector>

#include "agenui_dispatcher_types.h"
#include "agenui_engine.h"
#include "agenui_message_listener.h"
#include "agenui_surface_manager_interface.h"
#include "support/fixture_loader.h"
#include "support/mock_message_listener.h"
#include "support/scoped_surface_manager.h"
#include "support/test_env.h"
#include "support/thread_sync_helper.h"

namespace {

// LB001: registering the same listener twice causes each callback to be
// invoked twice (the dispatcher uses emplace_back without dedup).
TEST(ListenerBoundaryTest, LB001_AddSameListenerTwice_FiresTwice) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);
    ::agenui::testing::MockMessageListener listener;
    sm->addSurfaceEventListener(&listener);
    sm->addSurfaceEventListener(&listener);
    ::agenui::testing::WaitForWorkerIdle();

    auto proto = ::agenui::testing::LoadFixtureOrEmpty(
        "protocol/create_surface.json");
    sm->beginTextStream();
    sm->receiveTextChunk(proto);
    sm->endTextStream();
    EXPECT_TRUE(listener.waitFor(
        [&]() { return listener.createSurfaceCalls.size() >= 2; }, 3000));
    EXPECT_EQ(listener.createSurfaceCalls.size(), 2u);

    sm->removeSurfaceEventListener(&listener);
    sm->removeSurfaceEventListener(&listener);
}

// LB002: add twice + remove once → still fires once (remove uses break
// after first match).
TEST(ListenerBoundaryTest, LB002_AddTwiceRemoveOnce_StillFires) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);
    ::agenui::testing::MockMessageListener listener;
    sm->addSurfaceEventListener(&listener);
    sm->addSurfaceEventListener(&listener);
    sm->removeSurfaceEventListener(&listener);
    ::agenui::testing::WaitForWorkerIdle();

    auto proto = ::agenui::testing::LoadFixtureOrEmpty(
        "protocol/create_surface.json");
    sm->beginTextStream();
    sm->receiveTextChunk(proto);
    sm->endTextStream();
    EXPECT_TRUE(listener.waitFor(
        [&]() { return !listener.createSurfaceCalls.empty(); }, 3000));
    EXPECT_EQ(listener.createSurfaceCalls.size(), 1u);

    sm->removeSurfaceEventListener(&listener);
}

// LB003: remove listener mid-stream (between two chunks). The dispatch
// for the post-removal envelope must not invoke the listener.
TEST(ListenerBoundaryTest, LB003_RemoveListenerMidStream_NoCallbackAfter) {
    auto* engine = ::agenui::testing::GetEngine();
    ASSERT_NE(engine, nullptr);

    auto* sm = engine->createSurfaceManager();
    ASSERT_NE(sm, nullptr);
    ::agenui::testing::MockMessageListener listener;
    sm->addSurfaceEventListener(&listener);

    auto proto = ::agenui::testing::LoadFixtureOrEmpty(
        "protocol/create_surface.json");
    sm->beginTextStream();
    sm->receiveTextChunk(proto.substr(0, proto.size() / 2));
    sm->removeSurfaceEventListener(&listener);
    sm->receiveTextChunk(proto.substr(proto.size() / 2));
    sm->endTextStream();
    ::agenui::testing::WaitForWorkerIdle();

    // Listener removed BEFORE the parser saw the complete envelope, so it
    // must not fire.
    EXPECT_EQ(listener.createSurfaceCalls.size(), 0u);

    engine->destroySurfaceManager(sm);
    ::agenui::testing::WaitForWorkerIdle();
}

// LB004: mass register / mass remove. Final state has zero registered
// listeners; subsequent dispatch must not fire any.
TEST(ListenerBoundaryTest, LB004_MassAddRemove_FinalStateClean) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);

    constexpr int N = 100;
    std::vector<::agenui::testing::MockMessageListener> listeners(N);
    for (int i = 0; i < N; ++i) sm->addSurfaceEventListener(&listeners[i]);
    for (int i = 0; i < N; ++i) sm->removeSurfaceEventListener(&listeners[i]);
    ::agenui::testing::WaitForWorkerIdle();

    auto proto = ::agenui::testing::LoadFixtureOrEmpty(
        "protocol/create_surface.json");
    sm->beginTextStream();
    sm->receiveTextChunk(proto);
    sm->endTextStream();
    ::agenui::testing::WaitForWorkerIdle();

    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(listeners[i].createSurfaceCalls.size(), 0u) << "i=" << i;
    }
}

// LB005: 3 listeners, remove the middle one. The other two still fire.
TEST(ListenerBoundaryTest, LB005_RemoveMiddleListener_NeighborsStillFire) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);
    ::agenui::testing::MockMessageListener a, b, c;
    sm->addSurfaceEventListener(&a);
    sm->addSurfaceEventListener(&b);
    sm->addSurfaceEventListener(&c);
    sm->removeSurfaceEventListener(&b);
    ::agenui::testing::WaitForWorkerIdle();

    auto proto = ::agenui::testing::LoadFixtureOrEmpty(
        "protocol/create_surface.json");
    sm->beginTextStream();
    sm->receiveTextChunk(proto);
    sm->endTextStream();
    EXPECT_TRUE(a.waitFor([&]() { return !a.createSurfaceCalls.empty(); }, 3000));
    EXPECT_TRUE(c.waitFor([&]() { return !c.createSurfaceCalls.empty(); }, 3000));
    EXPECT_EQ(a.createSurfaceCalls.size(), 1u);
    EXPECT_EQ(b.createSurfaceCalls.size(), 0u);
    EXPECT_EQ(c.createSurfaceCalls.size(), 1u);

    sm->removeSurfaceEventListener(&a);
    sm->removeSurfaceEventListener(&c);
}

// LB006: listener added BEFORE init() runs (cached path), removed AFTER
// init runs. Must NOT receive subsequent events.
TEST(ListenerBoundaryTest, LB006_AddBeforeInit_RemoveAfterInit) {
    auto* engine = ::agenui::testing::GetEngine();
    ASSERT_NE(engine, nullptr);

    auto* sm = engine->createSurfaceManager();
    ASSERT_NE(sm, nullptr);

    ::agenui::testing::MockMessageListener listener;
    // sm hasn't run init() yet (queued on worker). addListener -> cached.
    sm->addSurfaceEventListener(&listener);
    ::agenui::testing::WaitForWorkerIdle();  // init() applies cached listener
    sm->removeSurfaceEventListener(&listener);

    auto proto = ::agenui::testing::LoadFixtureOrEmpty(
        "protocol/create_surface.json");
    sm->beginTextStream();
    sm->receiveTextChunk(proto);
    sm->endTextStream();
    ::agenui::testing::WaitForWorkerIdle();

    EXPECT_EQ(listener.createSurfaceCalls.size(), 0u);

    engine->destroySurfaceManager(sm);
    ::agenui::testing::WaitForWorkerIdle();
}

// LB007: listener added AND removed BEFORE init() runs. Cached listener
// vector must be drained, listener gets nothing.
TEST(ListenerBoundaryTest, LB007_AddAndRemoveBeforeInit_NoFire) {
    auto* engine = ::agenui::testing::GetEngine();
    ASSERT_NE(engine, nullptr);

    auto* sm = engine->createSurfaceManager();
    ASSERT_NE(sm, nullptr);

    ::agenui::testing::MockMessageListener listener;
    sm->addSurfaceEventListener(&listener);
    sm->removeSurfaceEventListener(&listener);
    ::agenui::testing::WaitForWorkerIdle();

    auto proto = ::agenui::testing::LoadFixtureOrEmpty(
        "protocol/create_surface.json");
    sm->beginTextStream();
    sm->receiveTextChunk(proto);
    sm->endTextStream();
    ::agenui::testing::WaitForWorkerIdle();

    EXPECT_EQ(listener.createSurfaceCalls.size(), 0u);
    engine->destroySurfaceManager(sm);
    ::agenui::testing::WaitForWorkerIdle();
}

// LB008: a listener that re-enters the SDK (calls submitUIAction) inside
// its callback. Must not deadlock — submit posts to worker, which is
// already executing the dispatch.
namespace {
class ReentrantListener : public ::agenui::testing::MockMessageListener {
public:
    ::agenui::ISurfaceManager* sm = nullptr;
    void onCreateSurface(const ::agenui::CreateSurfaceMessage& m) override {
        ::agenui::testing::MockMessageListener::onCreateSurface(m);
        if (sm) {
            ::agenui::ActionMessage action;
            action.surfaceId = m.surfaceId;
            action.sourceComponentId = "btn";
            sm->submitUIAction(action);
        }
    }
};
}  // namespace

TEST(ListenerBoundaryTest, LB008_ListenerReentersSDK_NoDeadlock) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);
    ReentrantListener rl;
    rl.sm = sm.get();
    sm->addSurfaceEventListener(&rl);

    auto proto = ::agenui::testing::LoadFixtureOrEmpty(
        "protocol/create_surface.json");
    sm->beginTextStream();
    sm->receiveTextChunk(proto);
    sm->endTextStream();
    ::agenui::testing::WaitForWorkerIdle();

    EXPECT_GE(rl.createSurfaceCalls.size(), 1u);
    sm->removeSurfaceEventListener(&rl);
}

}  // namespace
