// Global gtest Environment.
//
// Initializes the AGenUI engine once per test process and tears it down at
// the end. Each individual test should create its own SurfaceManager via
// ScopedSurfaceManager so that contexts do not leak between tests.

#pragma once

#include <gtest/gtest.h>

#include "agenui_engine.h"
#include "agenui_engine_entry.h"

namespace agenui {
namespace testing {

class AGenUIEngineEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        engine_ = ::agenui::initAGenUIEngine();
        ASSERT_NE(engine_, nullptr) << "initAGenUIEngine() returned nullptr";
    }

    void TearDown() override {
        ::agenui::destroyAGenUIEngine();
        engine_ = nullptr;
    }

    static ::agenui::IAGenUIEngine* engine() {
        return ::agenui::getAGenUIEngine();
    }

private:
    ::agenui::IAGenUIEngine* engine_ = nullptr;
};

inline ::agenui::IAGenUIEngine* GetEngine() {
    return ::agenui::getAGenUIEngine();
}

}  // namespace testing
}  // namespace agenui
