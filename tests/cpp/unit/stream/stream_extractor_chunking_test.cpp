// SEC*: ProtocolStreamExtractor arbitrary-position chunking tests.
//
// Verifies that the streaming parser correctly reassembles data regardless
// of where the input is split into chunks.

#include <gtest/gtest.h>

#include <string>
#include <vector>
#include <functional>

#include "stream/agenui_protocol_stream_extractor.h"

namespace {

using ::agenui::ProtocolStreamExtractor;
using ParseResult = ProtocolStreamExtractor::ParseResult;
using EventType = ProtocolStreamExtractor::EventType;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Feeds data to extractor split at the given position, returning all results.
std::vector<ParseResult> feedBinarySplit(const std::string& data, size_t splitPos) {
    ProtocolStreamExtractor x;
    std::vector<ParseResult> allResults;

    x.appendData(data.substr(0, splitPos));
    auto r1 = x.driveParser();
    allResults.insert(allResults.end(), r1.begin(), r1.end());

    x.appendData(data.substr(splitPos));
    auto r2 = x.driveParser();
    allResults.insert(allResults.end(), r2.begin(), r2.end());

    return allResults;
}

/// Feeds data one byte at a time, collecting all results.
std::vector<ParseResult> feedByteByByte(const std::string& data) {
    ProtocolStreamExtractor x;
    std::vector<ParseResult> allResults;

    for (size_t i = 0; i < data.size(); ++i) {
        x.appendData(std::string(1, data[i]));
        auto results = x.driveParser();
        allResults.insert(allResults.end(), results.begin(), results.end());
    }
    return allResults;
}

/// Feeds data in small fixed-size chunks.
std::vector<ParseResult> feedFixedChunks(const std::string& data, size_t chunkSize) {
    ProtocolStreamExtractor x;
    std::vector<ParseResult> allResults;

    for (size_t pos = 0; pos < data.size(); pos += chunkSize) {
        size_t len = std::min(chunkSize, data.size() - pos);
        x.appendData(data.substr(pos, len));
        auto results = x.driveParser();
        allResults.insert(allResults.end(), results.begin(), results.end());
    }
    return allResults;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// SEC001: Split in the middle of a JSON key.
TEST(StreamExtractorChunkingTest, SEC001_SplitInKey_CorrectlyReassembles) {
    std::string data = R"({"version":"v0.9","createSurface":{"surfaceId":"s1","catalogId":"c","theme":{},"sendDataModel":false,"animated":true}})";

    // Split inside "version" key
    size_t splitPos = 5; // {"ver | sion":"v0.9",...}
    auto results = feedBinarySplit(data, splitPos);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].type, ParseResult::Type::NormalEvent);
    EXPECT_EQ(results[0].eventType, EventType::CreateSurface);
}

// SEC002: Split in the middle of a JSON value.
TEST(StreamExtractorChunkingTest, SEC002_SplitInValue_SurfaceIdCorrect) {
    std::string data = R"({"version":"v0.9","createSurface":{"surfaceId":"test-surface-123","catalogId":"c","theme":{},"sendDataModel":false,"animated":true}})";

    // Split inside surfaceId value "test-sur | face-123"
    size_t surfaceIdPos = data.find("test-surface");
    ASSERT_NE(surfaceIdPos, std::string::npos);
    size_t splitPos = surfaceIdPos + 8; // "test-sur"

    auto results = feedBinarySplit(data, splitPos);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].eventType, EventType::CreateSurface);
    EXPECT_NE(results[0].eventJson.find("test-surface-123"), std::string::npos);
}

// SEC003: Split at opening brace.
TEST(StreamExtractorChunkingTest, SEC003_SplitAtOpenBrace_CorrectParsing) {
    std::string data = R"({"version":"v0.9","createSurface":{"surfaceId":"s1","catalogId":"c","theme":{},"sendDataModel":false,"animated":true}})";

    // Split right after opening brace
    auto results = feedBinarySplit(data, 1);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].eventType, EventType::CreateSurface);
}

// SEC004: Split in the middle of an escape sequence.
TEST(StreamExtractorChunkingTest, SEC004_SplitInEscapeSequence_CorrectReassembly) {
    // JSON with escaped characters: "content":"line1\nline2"
    std::string data = R"({"version":"v0.9","createSurface":{"surfaceId":"s1","catalogId":"c","theme":{},"sendDataModel":false,"animated":true}})";

    // For escape testing we use a value with backslash:
    std::string escData = R"({"version":"v0.9","updateDataModel":{"surfaceId":"s1","path":"/","value":"line1\nline2"}})";

    // Split right at the backslash before 'n'
    size_t bsPos = escData.find("\\n");
    ASSERT_NE(bsPos, std::string::npos);

    auto results = feedBinarySplit(escData, bsPos);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].eventType, EventType::UpdateDataModel);
    EXPECT_NE(results[0].eventJson.find("line1\\nline2"), std::string::npos);

    // Also split after the backslash (between \ and n)
    auto results2 = feedBinarySplit(escData, bsPos + 1);
    ASSERT_EQ(results2.size(), 1u);
    EXPECT_EQ(results2[0].eventType, EventType::UpdateDataModel);
}

// SEC005: updateComponents with multiple components, split between them.
TEST(StreamExtractorChunkingTest, SEC005_UpdateComponentsSplitBetween_AllExtracted) {
    std::string data = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"c1","component":"Button"},{"id":"c2","component":"Image"}]}})";

    // Split between the two components (after first '}')
    size_t firstCompEnd = data.find(R"({"id":"c2")");
    ASSERT_NE(firstCompEnd, std::string::npos);

    auto results = feedBinarySplit(data, firstCompEnd);

    // Should get ComponentUpdate results for each component
    int componentUpdates = 0;
    for (const auto& r : results) {
        if (r.type == ParseResult::Type::ComponentUpdate) {
            componentUpdates++;
        }
    }
    EXPECT_EQ(componentUpdates, 2);
}

// SEC006: Single byte at a time feed for updateComponents.
TEST(StreamExtractorChunkingTest, SEC006_OneByteChunks_UpdateComponents_AllExtracted) {
    std::string data = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"c1","component":"Button"},{"id":"c2","component":"Image"},{"id":"c3","component":"Text"}]}})";

    auto results = feedByteByByte(data);

    int componentUpdates = 0;
    for (const auto& r : results) {
        if (r.type == ParseResult::Type::ComponentUpdate) {
            componentUpdates++;
        }
    }
    EXPECT_EQ(componentUpdates, 3);
}

// SEC007: Exhaustive binary split - every position produces same result count.
TEST(StreamExtractorChunkingTest, SEC007_ExhaustiveBinarySplit_ConsistentResults) {
    std::string data = R"({"version":"v0.9","createSurface":{"surfaceId":"s1","catalogId":"c","theme":{},"sendDataModel":false,"animated":true}})";

    // Feed complete (baseline)
    ProtocolStreamExtractor baseline;
    baseline.appendData(data);
    auto baseResults = baseline.driveParser();
    ASSERT_EQ(baseResults.size(), 1u);

    // Split at every position
    for (size_t pos = 1; pos < data.size(); ++pos) {
        auto results = feedBinarySplit(data, pos);
        ASSERT_EQ(results.size(), 1u)
            << "Failed at split position " << pos
            << " (char='" << data[pos] << "')";
        EXPECT_EQ(results[0].eventType, EventType::CreateSurface)
            << "Wrong event type at split position " << pos;
    }
}

// SEC008: Multiple events concatenated, fed byte by byte.
TEST(StreamExtractorChunkingTest, SEC008_MultipleEventsByteByByte_AllExtracted) {
    std::string data =
        R"({"version":"v0.9","createSurface":{"surfaceId":"s1","catalogId":"c","theme":{},"sendDataModel":false,"animated":true}})"
        R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"c1","component":"Button"}]}})"
        R"({"version":"v0.9","deleteSurface":{"surfaceId":"s1"}})";

    auto results = feedByteByByte(data);

    // Count event types
    int creates = 0, updates = 0, deletes = 0;
    for (const auto& r : results) {
        if (r.type == ParseResult::Type::NormalEvent) {
            if (r.eventType == EventType::CreateSurface) creates++;
            if (r.eventType == EventType::DeleteSurface) deletes++;
        } else if (r.type == ParseResult::Type::ComponentUpdate) {
            updates++;
        }
    }
    EXPECT_EQ(creates, 1);
    EXPECT_EQ(updates, 1); // 1 component in the updateComponents
    EXPECT_EQ(deletes, 1);
}

// SEC009: Split precisely at delimiter characters (colon, comma, bracket).
TEST(StreamExtractorChunkingTest, SEC009_SplitAtDelimiters_CorrectParsing) {
    std::string data = R"({"version":"v0.9","createSurface":{"surfaceId":"s1","catalogId":"c","theme":{},"sendDataModel":false,"animated":true}})";

    // Find and split at various delimiters
    auto testSplitAt = [&](char delimiter, const std::string& desc) {
        size_t pos = data.find(delimiter);
        ASSERT_NE(pos, std::string::npos) << "Delimiter not found: " << desc;
        auto results = feedBinarySplit(data, pos);
        ASSERT_EQ(results.size(), 1u) << "Failed splitting at " << desc;
        EXPECT_EQ(results[0].eventType, EventType::CreateSurface)
            << "Wrong type at " << desc;
    };

    testSplitAt(':', "first colon");
    testSplitAt(',', "first comma");
    testSplitAt('{', "first open brace");

    // Split at closing braces
    size_t lastBrace = data.rfind('}');
    auto results = feedBinarySplit(data, lastBrace);
    ASSERT_EQ(results.size(), 1u);
}

// SEC010: Very small chunks (2-3 bytes) alternating.
TEST(StreamExtractorChunkingTest, SEC010_VerySmallChunks_CorrectResult) {
    std::string data = R"({"version":"v0.9","createSurface":{"surfaceId":"s1","catalogId":"c","theme":{},"sendDataModel":false,"animated":true}})";

    auto results2 = feedFixedChunks(data, 2);
    ASSERT_EQ(results2.size(), 1u);
    EXPECT_EQ(results2[0].eventType, EventType::CreateSurface);

    auto results3 = feedFixedChunks(data, 3);
    ASSERT_EQ(results3.size(), 1u);
    EXPECT_EQ(results3[0].eventType, EventType::CreateSurface);
}

// SEC011: updateComponents exhaustive binary split.
TEST(StreamExtractorChunkingTest, SEC011_UpdateComponentsExhaustiveSplit_AllComponentsExtracted) {
    std::string data = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"c1","component":"Button"},{"id":"c2","component":"Text"}]}})";

    // Baseline: full feed
    ProtocolStreamExtractor baseline;
    baseline.appendData(data);
    auto baseResults = baseline.driveParser();
    int baseComponentCount = 0;
    for (const auto& r : baseResults) {
        if (r.type == ParseResult::Type::ComponentUpdate) baseComponentCount++;
    }
    ASSERT_EQ(baseComponentCount, 2);

    // Exhaustive split
    for (size_t pos = 1; pos < data.size(); ++pos) {
        auto results = feedBinarySplit(data, pos);
        int componentCount = 0;
        for (const auto& r : results) {
            if (r.type == ParseResult::Type::ComponentUpdate) componentCount++;
        }
        EXPECT_EQ(componentCount, 2)
            << "Failed at split position " << pos
            << " (around: '" << data.substr(std::max((size_t)0, pos > 3 ? pos - 3 : 0), 6) << "')";
    }
}

// SEC012: Split in the middle of nested empty object {}.
TEST(StreamExtractorChunkingTest, SEC012_SplitInEmptyObject_Correct) {
    std::string data = R"({"version":"v0.9","createSurface":{"surfaceId":"s1","catalogId":"c","theme":{},"sendDataModel":false,"animated":true}})";

    // Find "theme":{} and split between { and }
    size_t themePos = data.find("\"theme\":{}");
    ASSERT_NE(themePos, std::string::npos);
    size_t emptyObjStart = data.find("{}", themePos);
    size_t splitPos = emptyObjStart + 1; // Between { and }

    auto results = feedBinarySplit(data, splitPos);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].eventType, EventType::CreateSurface);
}

}  // namespace
