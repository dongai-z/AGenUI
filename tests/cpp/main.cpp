// AGenUI gtest entry point.
//
// Registers the global TestEnvironment that initializes / tears down the
// engine once per test process. Individual tests use ScopedSurfaceManager
// for per-test isolation.

#include <gtest/gtest.h>

#include "support/test_env.h"

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new agenui::testing::AGenUIEngineEnvironment());
    return RUN_ALL_TESTS();
}
