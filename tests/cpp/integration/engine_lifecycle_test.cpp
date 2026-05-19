// E*: AGenUIEngine entry / lifecycle tests.
//
// Architectural contract:
//   AGenUIEngine is a process-level singleton that is init'd ONCE and
//   destroy'd ONCE for the entire process lifetime. The global test
//   Environment (support/test_env.h) takes care of both. Individual
//   tests must NOT call destroyAGenUIEngine() — doing so would tear
//   down the engine that subsequent tests rely on.
//
//   initAGenUIEngine() is documented as idempotent: repeated calls
//   return the same instance. Tests below exercise that contract plus
//   the engine-level configuration APIs.

#include <gtest/gtest.h>

#include "agenui_engine.h"
#include "agenui_engine_entry.h"
#include "support/test_env.h"

namespace {

class EngineLifecycleTest : public ::testing::Test {
    // No SetUp/TearDown: the global Environment owns the engine
    // lifecycle. Tests share a single, always-running engine.
};

// E001: init returns a valid (non-null) engine pointer. With the
// Environment already running, this is a re-entry of init and must
// return the existing instance.
TEST_F(EngineLifecycleTest, E001_InitEngine_ReturnsValidPointer) {
    auto* engine = ::agenui::initAGenUIEngine();
    EXPECT_NE(engine, nullptr);
    EXPECT_EQ(engine, ::agenui::getAGenUIEngine());
}

// E002: init is idempotent — repeated calls return the same instance.
TEST_F(EngineLifecycleTest, E002_InitEngine_RepeatedCall_Idempotent) {
    auto* a = ::agenui::initAGenUIEngine();
    auto* b = ::agenui::initAGenUIEngine();
    auto* c = ::agenui::initAGenUIEngine();
    EXPECT_NE(a, nullptr);
    EXPECT_EQ(a, b);
    EXPECT_EQ(b, c);
}

// E003: setDayNightMode tolerates valid and invalid inputs without throwing.
TEST_F(EngineLifecycleTest, E003_SetDayNightMode_AcceptsValidValues) {
    auto* engine = ::agenui::getAGenUIEngine();
    ASSERT_NE(engine, nullptr);
    EXPECT_NO_THROW(engine->setDayNightMode("light"));
    EXPECT_NO_THROW(engine->setDayNightMode("dark"));
    EXPECT_NO_THROW(engine->setDayNightMode("invalid_mode"));
    EXPECT_NO_THROW(engine->setDayNightMode(""));
}

// E004: loadThemeConfig accepts valid JSON.
TEST_F(EngineLifecycleTest, E004_LoadThemeConfig_ValidJson_Succeeds) {
    auto* engine = ::agenui::getAGenUIEngine();
    ASSERT_NE(engine, nullptr);
    std::string err;
    bool ok = engine->loadThemeConfig("{}", err);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(err.empty());
}

// E005: loadThemeConfig rejects malformed JSON and reports error.
TEST_F(EngineLifecycleTest, E005_LoadThemeConfig_InvalidJson_FailsWithMessage) {
    auto* engine = ::agenui::getAGenUIEngine();
    ASSERT_NE(engine, nullptr);
    std::string err;
    bool ok = engine->loadThemeConfig("not json{{", err);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(err.empty());
}

// E006: loadDesignTokenConfig requires a top-level `designTokens` object.
TEST_F(EngineLifecycleTest, E006_LoadDesignTokenConfig_ValidJson_Succeeds) {
    auto* engine = ::agenui::getAGenUIEngine();
    ASSERT_NE(engine, nullptr);
    std::string err;
    bool ok = engine->loadDesignTokenConfig(
        R"({"designTokens": {"primary": {"light": "#FF0000", "dark": "#00FF00"}}})",
        err);
    EXPECT_TRUE(ok) << err;
}

// E007: loadDesignTokenConfig rejects invalid JSON.
TEST_F(EngineLifecycleTest, E007_LoadDesignTokenConfig_InvalidJson_Fails) {
    auto* engine = ::agenui::getAGenUIEngine();
    ASSERT_NE(engine, nullptr);
    std::string err;
    bool ok = engine->loadDesignTokenConfig("##garbage", err);
    EXPECT_FALSE(ok);
}

// NOTE: The following historical cases are intentionally removed because
// they violate the "init once / destroy once per process" architectural
// contract:
//   - E003 GetEngine_BeforeInit_ReturnsNull
//   - E004 DestroyEngine_AfterInit_GetReturnsNull
//   - E005 DestroyEngine_NotInited_Safe
//   - E006 InitDestroyLoop_NoLeak
//   - E007 StartAfterStop_Recreates
//   - E012 DestroyEngine_GetMeasurement_NoCrash
// destroyAGenUIEngine() is the sole responsibility of the global test
// Environment (support/test_env.h::AGenUIEngineEnvironment::TearDown).

}  // namespace
