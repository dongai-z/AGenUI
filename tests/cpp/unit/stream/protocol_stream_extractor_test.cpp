// PSE*: ProtocolStreamExtractor unit tests.

#include <gtest/gtest.h>

#include "stream/agenui_protocol_stream_extractor.h"

namespace {

using ::agenui::ProtocolStreamExtractor;

// PSE001
TEST(ProtocolStreamExtractorTest, PSE001_SingleEnvelope_FullJson) {
    ProtocolStreamExtractor x;
    x.appendData(R"({"version":"v0.9","createSurface":{"surfaceId":"a"}})");
    auto results = x.driveParser();
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].type,
              ProtocolStreamExtractor::ParseResult::Type::NormalEvent);
    EXPECT_EQ(results[0].eventType,
              ProtocolStreamExtractor::EventType::CreateSurface);
}

// PSE002
TEST(ProtocolStreamExtractorTest, PSE002_ChunkedEnvelope_RecombineAndParse) {
    ProtocolStreamExtractor x;
    std::string full =
        R"({"version":"v0.9","createSurface":{"surfaceId":"abc"}})";
    x.appendData(full.substr(0, full.size() / 2));
    auto first = x.driveParser();
    EXPECT_TRUE(first.empty());
    x.appendData(full.substr(full.size() / 2));
    auto second = x.driveParser();
    ASSERT_FALSE(second.empty());
    EXPECT_EQ(second[0].eventType,
              ProtocolStreamExtractor::EventType::CreateSurface);
}

// PSE003
TEST(ProtocolStreamExtractorTest, PSE003_TwoEnvelopes_BothExtracted) {
    ProtocolStreamExtractor x;
    x.appendData(
        R"({"version":"v0.9","createSurface":{"surfaceId":"s1"}})"
        R"({"version":"v0.9","createSurface":{"surfaceId":"s2"}})");
    auto results = x.driveParser();
    EXPECT_GE(results.size(), 2u);
}

// PSE004
TEST(ProtocolStreamExtractorTest, PSE004_EmptyBuffer_NoOutput) {
    ProtocolStreamExtractor x;
    auto results = x.driveParser();
    EXPECT_TRUE(results.empty());
    EXPECT_FALSE(x.hasUnprocessedData());
}

// PSE005
TEST(ProtocolStreamExtractorTest, PSE005_Reset_ClearsBuffer) {
    ProtocolStreamExtractor x;
    x.appendData("partial-{");
    x.reset();
    EXPECT_FALSE(x.hasUnprocessedData());
}

// PSE006: extractFirstCompleteJson static helper.
TEST(ProtocolStreamExtractorTest, PSE006_ExtractFirstCompleteJson_Works) {
    std::string buffer = R"({"a":1}{"b":2})";
    std::string out;
    size_t end = 0;
    EXPECT_TRUE(ProtocolStreamExtractor::extractFirstCompleteJson(
        buffer, out, end));
    EXPECT_EQ(out, "{\"a\":1}");
    EXPECT_EQ(end, 7u);
}

// PSE007
TEST(ProtocolStreamExtractorTest, PSE007_DetectEventType_ForCommonEnvelopes) {
    ProtocolStreamExtractor x;
    EXPECT_EQ(
        x.detectEventType(R"({"createSurface":{"surfaceId":"s"}})"),
        ProtocolStreamExtractor::EventType::CreateSurface);
    EXPECT_EQ(
        x.detectEventType(R"({"deleteSurface":{"surfaceId":"s"}})"),
        ProtocolStreamExtractor::EventType::DeleteSurface);
    EXPECT_EQ(
        x.detectEventType(R"({"updateComponents":{}})"),
        ProtocolStreamExtractor::EventType::UpdateComponents);
}

}  // namespace
