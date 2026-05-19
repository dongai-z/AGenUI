// TSA*: concurrent stress tests compiled into agenui_concurrency_tests.
//
// These tests pass under the plain build. They can also be run with
// ThreadSanitizer enabled (-DAGENUI_TESTS_ENABLE_TSAN=ON) for deeper
// race detection, though that configuration is not part of standard CI.

#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "agenui_engine.h"
#include "agenui_surface_manager_interface.h"
#include "support/mock_message_listener.h"
#include "support/scoped_surface_manager.h"
#include "support/test_env.h"
#include "support/thread_sync_helper.h"

namespace {

// TSA001: alternating add/remove listener while another thread streams.
TEST(ThreadSanitizerTest, TSA001_ListenerVector_RaceFree) {
    ::agenui::testing::ScopedSurfaceManager sm;
    ASSERT_TRUE(sm);

    std::atomic<bool> stop{false};
    ::agenui::testing::MockMessageListener listener;

    std::thread streamer([&]() {
        while (!stop.load()) {
            sm->beginTextStream();
            sm->receiveTextChunk("partial");
            sm->endTextStream();
        }
    });

    for (int i = 0; i < 200; ++i) {
        sm->addSurfaceEventListener(&listener);
        sm->removeSurfaceEventListener(&listener);
    }
    stop.store(true);
    streamer.join();
    ::agenui::testing::WaitForWorkerIdle();
}

// TSA002: serial create/destroy; validates that SurfaceManager
// init/uninit on the worker thread does not race with main-thread teardown.
TEST(ThreadSanitizerTest, TSA002_CreateDestroySurfaceManager_RaceFree) {
    auto* engine = ::agenui::testing::GetEngine();
    ASSERT_NE(engine, nullptr);
    for (int k = 0; k < 100; ++k) {
        auto* sm = engine->createSurfaceManager();
        if (sm) engine->destroySurfaceManager(sm);
    }
    ::agenui::testing::WaitForWorkerIdle();
}

}  // namespace
