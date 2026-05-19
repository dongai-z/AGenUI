// RAII helper that creates a SurfaceManager on construction and destroys it
// on scope exit, waiting for the worker thread to drain so subsequent tests
// see a clean state.

#pragma once

#include "agenui_engine.h"
#include "agenui_engine_entry.h"
#include "agenui_surface_manager_interface.h"
#include "support/test_env.h"
#include "support/thread_sync_helper.h"

namespace agenui {
namespace testing {

class ScopedSurfaceManager {
public:
    ScopedSurfaceManager() {
        auto* engine = GetEngine();
        if (engine) {
            sm_ = engine->createSurfaceManager();
            // SurfaceManager::init() runs on worker thread; wait for it.
            WaitForWorkerIdle();
        }
    }

    ~ScopedSurfaceManager() {
        if (sm_) {
            auto* engine = GetEngine();
            if (engine) {
                engine->destroySurfaceManager(sm_);
                // Wait for uninit() to finish on the worker thread before we
                // tear down the test stack.
                WaitForWorkerIdle();
            }
        }
    }

    ScopedSurfaceManager(const ScopedSurfaceManager&) = delete;
    ScopedSurfaceManager& operator=(const ScopedSurfaceManager&) = delete;

    ::agenui::ISurfaceManager* get() const { return sm_; }
    ::agenui::ISurfaceManager* operator->() const { return sm_; }
    operator bool() const { return sm_ != nullptr; }

private:
    ::agenui::ISurfaceManager* sm_ = nullptr;
};

}  // namespace testing
}  // namespace agenui
