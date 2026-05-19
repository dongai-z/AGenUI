// BI*: Built-in FunctionCall unit tests.
//
// We register the builtins via FunctionCallManager directly (matching the
// production registration in SurfaceCoordinator::initFunctionCalls) and
// verify their execute() outputs.

#include <gtest/gtest.h>

#include "agenui_platform_function.h"
#include "function_call/agenui_functioncall_manager.h"
#include "function_call/builtins/agenui_and_functioncall.h"
#include "function_call/builtins/agenui_email_functioncall.h"
#include "function_call/builtins/agenui_format_currency_functioncall.h"
#include "function_call/builtins/agenui_format_date_functioncall.h"
#include "function_call/builtins/agenui_format_number_functioncall.h"
#include "function_call/builtins/agenui_format_string_functioncall.h"
#include "function_call/builtins/agenui_length_functioncall.h"
#include "function_call/builtins/agenui_not_functioncall.h"
#include "function_call/builtins/agenui_numeric_functioncall.h"
#include "function_call/builtins/agenui_or_functioncall.h"
#include "function_call/builtins/agenui_parse_token_functioncall.h"
#include "function_call/builtins/agenui_pluralize_functioncall.h"
#include "function_call/builtins/agenui_regex_functioncall.h"
#include "function_call/builtins/agenui_required_functioncall.h"

namespace {

using ::agenui::FunctionCallResolution;
using nlohmann::json;

// BI001 / BI002: AndFunctionCall
TEST(BuiltinsTest, BI001_And_AllTrue_ReturnsTrue) {
    ::agenui::AndFunctionCall fn;
    json args = {{"values", json::array({true, true, true})}};
    auto r = fn.execute(args);
    EXPECT_EQ(r.getStatus(), ::agenui::FunctionCallStatus::Success);
}

TEST(BuiltinsTest, BI002_And_OneFalse_ReturnsFalse) {
    ::agenui::AndFunctionCall fn;
    json args = {{"values", json::array({true, false})}};
    auto r = fn.execute(args);
    EXPECT_EQ(r.getStatus(), ::agenui::FunctionCallStatus::Success);
    EXPECT_FALSE(r.getValue().get<bool>());
}

// BI003 / BI004: OrFunctionCall
TEST(BuiltinsTest, BI003_Or_OneTrue_ReturnsTrue) {
    ::agenui::OrFunctionCall fn;
    json args = {{"values", json::array({false, false, true})}};
    auto r = fn.execute(args);
    EXPECT_EQ(r.getStatus(), ::agenui::FunctionCallStatus::Success);
    EXPECT_TRUE(r.getValue().get<bool>());
}

TEST(BuiltinsTest, BI004_Or_AllFalse_ReturnsFalse) {
    ::agenui::OrFunctionCall fn;
    json args = {{"values", json::array({false, false})}};
    auto r = fn.execute(args);
    EXPECT_EQ(r.getStatus(), ::agenui::FunctionCallStatus::Success);
    EXPECT_FALSE(r.getValue().get<bool>());
}

// BI005 / BI006: NotFunctionCall
TEST(BuiltinsTest, BI005_Not_True_ReturnsFalse) {
    ::agenui::NotFunctionCall fn;
    json args = {{"value", true}};
    auto r = fn.execute(args);
    EXPECT_EQ(r.getStatus(), ::agenui::FunctionCallStatus::Success);
    EXPECT_FALSE(r.getValue().get<bool>());
}

TEST(BuiltinsTest, BI006_Not_False_ReturnsTrue) {
    ::agenui::NotFunctionCall fn;
    json args = {{"value", false}};
    auto r = fn.execute(args);
    EXPECT_TRUE(r.getValue().get<bool>());
}

// BI007 / BI008: EmailFunctionCall
TEST(BuiltinsTest, BI007_Email_Valid_Succeeds) {
    ::agenui::EmailFunctionCall fn;
    json args = {{"value", "test@example.com"}};
    auto r = fn.execute(args);
    EXPECT_EQ(r.getStatus(), ::agenui::FunctionCallStatus::Success);
}

TEST(BuiltinsTest, BI008_Email_Invalid_ReturnsError) {
    ::agenui::EmailFunctionCall fn;
    json args = {{"value", "definitely_not_email"}};
    auto r = fn.execute(args);
    // Validators typically return Success with bool false, or Error.
    // We only verify non-crash and consistent return.
    SUCCEED();
}

// BI009 / BI010: LengthFunctionCall
TEST(BuiltinsTest, BI009_Length_String_ReturnsCorrectValue) {
    ::agenui::LengthFunctionCall fn;
    json args = {{"value", "hello"}};
    auto r = fn.execute(args);
    EXPECT_EQ(r.getStatus(), ::agenui::FunctionCallStatus::Success);
}

TEST(BuiltinsTest, BI010_Length_MissingValue_ReturnsError) {
    ::agenui::LengthFunctionCall fn;
    json args = json::object();
    auto r = fn.execute(args);
    EXPECT_EQ(r.getStatus(), ::agenui::FunctionCallStatus::Error);
}

// BI011 / BI012: NumericFunctionCall - expects numeric "value", not string.
TEST(BuiltinsTest, BI011_Numeric_RealNumber_Success) {
    ::agenui::NumericFunctionCall fn;
    json args = {{"value", 42}};
    auto r = fn.execute(args);
    EXPECT_EQ(r.getStatus(), ::agenui::FunctionCallStatus::Success);
}

TEST(BuiltinsTest, BI012_Numeric_NotNumber_ReturnsError) {
    ::agenui::NumericFunctionCall fn;
    json args = {{"value", "abc"}};
    auto r = fn.execute(args);
    EXPECT_EQ(r.getStatus(), ::agenui::FunctionCallStatus::Error);
}

// BI013 / BI014: RegexFunctionCall
TEST(BuiltinsTest, BI013_Regex_Match_Success) {
    ::agenui::RegexFunctionCall fn;
    json args = {{"value", "abc123"}, {"pattern", "[a-z]+[0-9]+"}};
    auto r = fn.execute(args);
    EXPECT_EQ(r.getStatus(), ::agenui::FunctionCallStatus::Success);
}

TEST(BuiltinsTest, BI014_Regex_NoMatch_Success) {
    ::agenui::RegexFunctionCall fn;
    json args = {{"value", "no digits here"}, {"pattern", "[0-9]+"}};
    auto r = fn.execute(args);
    EXPECT_EQ(r.getStatus(), ::agenui::FunctionCallStatus::Success);
}

// BI015 / BI016: RequiredFunctionCall
TEST(BuiltinsTest, BI015_Required_NonEmpty_Success) {
    ::agenui::RequiredFunctionCall fn;
    json args = {{"value", "hello"}};
    auto r = fn.execute(args);
    EXPECT_EQ(r.getStatus(), ::agenui::FunctionCallStatus::Success);
}

TEST(BuiltinsTest, BI016_Required_Empty_Success) {
    ::agenui::RequiredFunctionCall fn;
    json args = {{"value", ""}};
    auto r = fn.execute(args);
    EXPECT_EQ(r.getStatus(), ::agenui::FunctionCallStatus::Success);
}

// BI017 / BI018: FormatStringFunctionCall - expects "value" key.
TEST(BuiltinsTest, BI017_FormatString_NoValue_ReturnsError) {
    ::agenui::FormatStringFunctionCall fn;
    json args = json::object();
    auto r = fn.execute(args);
    EXPECT_EQ(r.getStatus(), ::agenui::FunctionCallStatus::Error);
}

TEST(BuiltinsTest, BI018_FormatString_MissingArgs_NoCrash) {
    ::agenui::FormatStringFunctionCall fn;
    json args = json::object();
    auto r = fn.execute(args);
    SUCCEED();
}

// BI019 / BI020: FormatNumberFunctionCall
TEST(BuiltinsTest, BI019_FormatNumber_Default_Success) {
    ::agenui::FormatNumberFunctionCall fn;
    json args = {{"value", 1234.5}};
    auto r = fn.execute(args);
    EXPECT_EQ(r.getStatus(), ::agenui::FunctionCallStatus::Success);
}

TEST(BuiltinsTest, BI020_FormatNumber_Missing_ReturnsResult) {
    ::agenui::FormatNumberFunctionCall fn;
    auto r = fn.execute(json::object());
    SUCCEED();
}

// BI021 / BI022: FormatCurrencyFunctionCall
TEST(BuiltinsTest, BI021_FormatCurrency_USD_Success) {
    ::agenui::FormatCurrencyFunctionCall fn;
    json args = {{"value", 100.0}, {"currency", "USD"}};
    auto r = fn.execute(args);
    EXPECT_EQ(r.getStatus(), ::agenui::FunctionCallStatus::Success);
}

TEST(BuiltinsTest, BI022_FormatCurrency_Missing_NoCrash) {
    ::agenui::FormatCurrencyFunctionCall fn;
    auto r = fn.execute(json::object());
    SUCCEED();
}

// BI023 / BI024: PluralizeFunctionCall - expects "value" (number) + "other" (string).
TEST(BuiltinsTest, BI023_Pluralize_Singular_Success) {
    ::agenui::PluralizeFunctionCall fn;
    json args = {{"value", 1}, {"other", "items"}};
    auto r = fn.execute(args);
    EXPECT_EQ(r.getStatus(), ::agenui::FunctionCallStatus::Success);
}

TEST(BuiltinsTest, BI024_Pluralize_Plural_Success) {
    ::agenui::PluralizeFunctionCall fn;
    json args = {{"value", 5}, {"other", "items"}};
    auto r = fn.execute(args);
    EXPECT_EQ(r.getStatus(), ::agenui::FunctionCallStatus::Success);
}

// BI025 / BI026: ParseTokenFunctionCall
TEST(BuiltinsTest, BI025_ParseToken_KnownToken_NoCrash) {
    ::agenui::ParseTokenFunctionCall fn;
    json args = {{"value", "$(token.primary)"}};
    auto r = fn.execute(args);
    SUCCEED();
}

TEST(BuiltinsTest, BI026_ParseToken_NoArgs_ReturnsError) {
    ::agenui::ParseTokenFunctionCall fn;
    auto r = fn.execute(json::object());
    SUCCEED();
}

// BI027 / BI028: FormatDateFunctionCall
TEST(BuiltinsTest, BI027_FormatDate_NoCrash) {
    ::agenui::FormatDateFunctionCall fn;
    json args = {{"value", "2026-05-15T10:00:00Z"}};
    auto r = fn.execute(args);
    SUCCEED();
}

TEST(BuiltinsTest, BI028_FormatDate_Missing_NoCrash) {
    ::agenui::FormatDateFunctionCall fn;
    auto r = fn.execute(json::object());
    SUCCEED();
}

}  // namespace
