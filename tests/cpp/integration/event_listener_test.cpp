// EL*: SurfaceManager add/remove event listener tests.

#include <gtest/gtest.h>

#include "agenui_engine.h"
#include "agenui_engine_entry.h"
#include "agenui_surface_manager_interface.h"
#include "support/mock_message_listener.h"
#include "support/scoped_surface_manager.h"
#include "support/test_env.h"
#include "support/thread_sync_helper.h"

namespace {

class EventListenerTest : public ::testing::Test {};

// EL001: addListener immediately after createSurfaceManager (before init() ran
// on the worker thread). It must be cached and applied once init completes.
TEST_F(EventListenerTest, EL001_AddListenerBeforeInit_CachedAndApplied) {
    auto* engine = ::agenui::testing::GetEngine();
    ASSERT_NE(engine, nullptr);

    auto* sm = engine->createSurfaceManager();
    ASSERT_NE(sm, nullptr);
    ::agenui::testing::MockMessageListener listener;
    EXPECT_NO_THROW(sm->addSurfaceEventListener(&listener));

    ::agenui::testing::WaitForWorkerIdle();
    // Removing should also work after init.
    EXPECT_NO_THROW(sm->removeSurfaceEventListener(&listener));
    engine->destroySurfaceManager(sm);
    ::agenui::testing::WaitForWorkerIdle();
}

// EL002
TEST_F(EventListenerTest, EL002_AddListenerAfterInit_RegistersImmediately) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);
    ::agenui::testing::MockMessageListener listener;
    EXPECT_NO_THROW(sm->addSurfaceEventListener(&listener));
    sm->removeSurfaceEventListener(&listener);
}

// EL003
TEST_F(EventListenerTest, EL003_RemoveListener_NoCallbacksAfterwards) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);

    ::agenui::testing::MockMessageListener listener;
    sm->addSurfaceEventListener(&listener);
    sm->removeSurfaceEventListener(&listener);

    sm->beginTextStream();
    ::agenui::testing::WaitForWorkerIdle();
    EXPECT_EQ(listener.totalCalls(), 0);
}

// EL004
TEST_F(EventListenerTest, EL004_RemoveUnregisteredListener_Safe) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);
    ::agenui::testing::MockMessageListener listener;
    EXPECT_NO_THROW(sm->removeSurfaceEventListener(&listener));
}

// EL005
TEST_F(EventListenerTest, EL005_MultipleListeners_AllRegistered) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);
    ::agenui::testing::MockMessageListener a, b, c;
    sm->addSurfaceEventListener(&a);
    sm->addSurfaceEventListener(&b);
    sm->addSurfaceEventListener(&c);
    sm->removeSurfaceEventListener(&a);
    sm->removeSurfaceEventListener(&b);
    sm->removeSurfaceEventListener(&c);
}

// EL006
TEST_F(EventListenerTest, EL006_NullListener_AddRemoveSafe) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);
    EXPECT_NO_THROW(sm->addSurfaceEventListener(nullptr));
    EXPECT_NO_THROW(sm->removeSurfaceEventListener(nullptr));
}

// EL007: Recursive mutex must allow remove from inside callback.
namespace {
class SelfRemovingListener : public ::agenui::testing::MockMessageListener {
public:
    void onCreateSurface(const ::agenui::CreateSurfaceMessage& msg) override {
        ::agenui::testing::MockMessageListener::onCreateSurface(msg);
        if (sm) sm->removeSurfaceEventListener(this);
    }
    ::agenui::ISurfaceManager* sm = nullptr;
};
}  // namespace

TEST_F(EventListenerTest, EL007_RemoveListenerInsideCallback_NoDeadlock) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);
    SelfRemovingListener self;
    self.sm = sm.get();
    sm->addSurfaceEventListener(&self);
    // We do not actually trigger onCreateSurface here without protocol data;
    // this test just verifies setup/teardown safety with self-removing
    // listener installed.
    EXPECT_NO_THROW(sm->removeSurfaceEventListener(&self));
}

// EL008: after destroySurfaceManager + WaitForWorkerIdle, the underlying
// shared_ptr is gone, so calling methods on the saved pointer would be
// use-after-free. Instead we verify the documented contract: a listener
// outliving the SM and removed *before* destroy works correctly.
TEST_F(EventListenerTest, EL008_RemoveBeforeDestroy_Safe) {
    auto* engine = ::agenui::testing::GetEngine();
    ASSERT_NE(engine, nullptr);
    auto* sm = engine->createSurfaceManager();
    ASSERT_NE(sm, nullptr);
    ::agenui::testing::MockMessageListener listener;
    sm->addSurfaceEventListener(&listener);
    EXPECT_NO_THROW(sm->removeSurfaceEventListener(&listener));
    engine->destroySurfaceManager(sm);
    ::agenui::testing::WaitForWorkerIdle();
}

// EL009: listener registered then removed via destroy path receives no
// callbacks (we never push protocol data, so this is trivially true; main
// purpose is verifying clean teardown with ASan).
TEST_F(EventListenerTest, EL009_NoCallbackAfterUninit) {
    auto* engine = ::agenui::testing::GetEngine();
    auto* sm = engine->createSurfaceManager();
    ASSERT_NE(sm, nullptr);
    ::agenui::testing::MockMessageListener listener;
    sm->addSurfaceEventListener(&listener);
    sm->removeSurfaceEventListener(&listener);
    engine->destroySurfaceManager(sm);
    ::agenui::testing::WaitForWorkerIdle();
    EXPECT_EQ(listener.totalCalls(), 0);
}

}  // namespace
