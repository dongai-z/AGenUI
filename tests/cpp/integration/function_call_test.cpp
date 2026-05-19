// FC*: registerFunction / unregisterFunction tests.

#include <gtest/gtest.h>

#include "agenui_engine.h"
#include "agenui_engine_entry.h"
#include "agenui_platform_function.h"
#include "support/mock_platform_function.h"
#include "support/test_env.h"

namespace {

class FunctionCallTest : public ::testing::Test {
protected:
    ::agenui::IAGenUIEngine* engine_ = nullptr;
    void SetUp() override {
        engine_ = ::agenui::testing::GetEngine();
        ASSERT_NE(engine_, nullptr);
    }
};

const char* const kValidConfig = R"({"name":"myTestFn","description":"x"})";

// FC001
TEST_F(FunctionCallTest, FC001_RegisterFunction_ValidConfig_Succeeds) {
    ::agenui::testing::MockPlatformFunction fn;
    EXPECT_TRUE(engine_->registerFunction(kValidConfig, &fn));
    EXPECT_TRUE(engine_->unregisterFunction("myTestFn"));
}

// FC002
TEST_F(FunctionCallTest, FC002_RegisterFunction_InvalidJsonConfig_Fails) {
    ::agenui::testing::MockPlatformFunction fn;
    EXPECT_FALSE(engine_->registerFunction("not-json{{", &fn));
}

// FC003
TEST_F(FunctionCallTest, FC003_RegisterFunction_NullFunction_Fails) {
    EXPECT_FALSE(engine_->registerFunction(kValidConfig, nullptr));
}

// FC004
TEST_F(FunctionCallTest, FC004_RegisterFunction_MissingName_Fails) {
    ::agenui::testing::MockPlatformFunction fn;
    EXPECT_FALSE(engine_->registerFunction(R"({"description":"no name"})", &fn));
}

// FC005
TEST_F(FunctionCallTest, FC005_UnregisterFunction_Registered_Succeeds) {
    ::agenui::testing::MockPlatformFunction fn;
    ASSERT_TRUE(engine_->registerFunction(R"({"name":"fc005"})", &fn));
    EXPECT_TRUE(engine_->unregisterFunction("fc005"));
}

// FC006
TEST_F(FunctionCallTest, FC006_UnregisterFunction_Unknown_Fails) {
    EXPECT_FALSE(engine_->unregisterFunction("definitely_not_registered_xyz"));
}

// FC007: register/unregister/register cycle is idempotent.
TEST_F(FunctionCallTest, FC007_RegisterUnregisterRegister_Idempotent) {
    ::agenui::testing::MockPlatformFunction fn;
    EXPECT_TRUE(engine_->registerFunction(R"({"name":"fc007a"})", &fn));
    EXPECT_TRUE(engine_->unregisterFunction("fc007a"));
    EXPECT_TRUE(engine_->registerFunction(R"({"name":"fc007a"})", &fn));
    EXPECT_TRUE(engine_->unregisterFunction("fc007a"));
}

// FC008: builtin functions are registered at engine init. As a smoke
// check, verify that user function registration does not interfere.
TEST_F(FunctionCallTest, FC008_BuiltinFunctions_Registered_Smoke) {
    // The contract is: builtins are registered when the first SurfaceManager
    // is created (initFunctionCalls in SurfaceCoordinator). We can't verify
    // each builtin from public API; this test simply ensures registering an
    // unrelated function does not interfere.
    ::agenui::testing::MockPlatformFunction fn;
    EXPECT_TRUE(engine_->registerFunction(R"({"name":"fc008_user"})", &fn));
    EXPECT_TRUE(engine_->unregisterFunction("fc008_user"));
}

}  // namespace
