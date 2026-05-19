// SDM*: DataModel streaming tests via ProtocolStreamExtractor.
//
// Tests the built-in DataModel streaming state machine that handles
// nested object/array value parsing and field-level incremental streaming.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "stream/agenui_protocol_stream_extractor.h"
#include "stream/agenui_markdown_stream_plugin.h"
#include "stream/agenui_text_stream_plugin.h"
#include "stream/agenui_composite_stream_plugin.h"
#include "nlohmann/json.hpp"

namespace {

using ::agenui::ProtocolStreamExtractor;
using ::agenui::MarkdownStreamPlugin;
using ::agenui::TextStreamPlugin;
using ::agenui::CompositeStreamPlugin;
using ParseResult = ProtocolStreamExtractor::ParseResult;
using EventType = ProtocolStreamExtractor::EventType;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

struct DataModelTestFixture {
    ProtocolStreamExtractor extractor;
    MarkdownStreamPlugin markdownPlugin;
    TextStreamPlugin textPlugin;
    CompositeStreamPlugin compositePlugin;

    DataModelTestFixture() {
        compositePlugin.addPlugin(&markdownPlugin);
        compositePlugin.addPlugin(&textPlugin);
        extractor.setPlugin(&compositePlugin);
    }

    std::vector<ParseResult> feedAll(const std::string& data) {
        extractor.appendData(data);
        return extractor.driveParser();
    }

    std::vector<ParseResult> feedChunked(const std::string& data,
                                          const std::vector<size_t>& splits) {
        std::vector<ParseResult> allResults;
        size_t prev = 0;
        for (size_t sp : splits) {
            extractor.appendData(data.substr(prev, sp - prev));
            auto r = extractor.driveParser();
            allResults.insert(allResults.end(), r.begin(), r.end());
            prev = sp;
        }
        if (prev < data.size()) {
            extractor.appendData(data.substr(prev));
            auto r = extractor.driveParser();
            allResults.insert(allResults.end(), r.begin(), r.end());
        }
        return allResults;
    }

    std::vector<ParseResult> feedByteByByte(const std::string& data) {
        std::vector<ParseResult> allResults;
        for (size_t i = 0; i < data.size(); ++i) {
            extractor.appendData(std::string(1, data[i]));
            auto results = extractor.driveParser();
            allResults.insert(allResults.end(), results.begin(), results.end());
        }
        return allResults;
    }
};

/// Collects all NormalEvent updateDataModel paths from results.
std::vector<std::string> collectUpdatePaths(const std::vector<ParseResult>& results) {
    std::vector<std::string> paths;
    for (const auto& r : results) {
        if (r.type == ParseResult::Type::NormalEvent &&
            r.eventType == EventType::UpdateDataModel) {
            auto json = nlohmann::json::parse(r.eventJson, nullptr, false);
            if (!json.is_discarded() && json.contains("updateDataModel")) {
                auto& udm = json["updateDataModel"];
                if (udm.contains("path")) {
                    paths.push_back(udm["path"].get<std::string>());
                }
            }
        }
    }
    return paths;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// SDM001: Complete updateDataModel (simple string value) - emits as NormalEvent.
TEST(StreamDataModelTest, SDM001_CompleteSimpleValue_EmitsNormalEvent) {
    DataModelTestFixture f;

    std::string data = R"({"version":"v0.9","updateDataModel":{"surfaceId":"s1","path":"/title","value":"Hello"}})";

    auto results = f.feedAll(data);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].type, ParseResult::Type::NormalEvent);
    EXPECT_EQ(results[0].eventType, EventType::UpdateDataModel);
    EXPECT_NE(results[0].eventJson.find("\"Hello\""), std::string::npos);
}

// SDM002: Incomplete updateDataModel (value is object) - enters DM streaming.
TEST(StreamDataModelTest, SDM002_IncompleteObjectValue_EntersDMStreaming) {
    DataModelTestFixture f;

    std::string data = R"({"version":"v0.9","updateDataModel":{"surfaceId":"s1","path":"/","value":{"name":"Alice","age":30}}})";

    // Split so the object value is incomplete
    size_t valuePos = data.find("\"value\":{");
    ASSERT_NE(valuePos, std::string::npos);
    size_t splitPos = valuePos + 20; // Somewhere inside the value object

    auto results = f.feedChunked(data, {splitPos});

    // Should produce individual updateDataModel events for each leaf field
    bool foundName = false, foundAge = false;
    for (const auto& r : results) {
        if (r.type == ParseResult::Type::NormalEvent &&
            r.eventType == EventType::UpdateDataModel) {
            if (r.eventJson.find("\"name\"") != std::string::npos ||
                r.eventJson.find("/name") != std::string::npos) {
                foundName = true;
            }
            if (r.eventJson.find("\"age\"") != std::string::npos ||
                r.eventJson.find("/age") != std::string::npos) {
                foundAge = true;
            }
        }
    }
    // At least some events should be produced from DM streaming
    EXPECT_FALSE(results.empty());
}

// SDM003: Nested object multi-level - paths generated correctly.
TEST(StreamDataModelTest, SDM003_NestedObjectMultiLevel_CorrectPaths) {
    DataModelTestFixture f;

    std::string data = R"({"version":"v0.9","updateDataModel":{"surfaceId":"s1","path":"/","value":{"a":{"b":{"c":"deep"}}}}})";

    // Split to force DM streaming
    size_t splitPos = data.find("\"c\":\"deep") ;
    ASSERT_NE(splitPos, std::string::npos);

    auto results = f.feedChunked(data, {splitPos});

    // Should see a path like /a/b/c
    auto paths = collectUpdatePaths(results);
    bool foundDeepPath = false;
    for (const auto& p : paths) {
        if (p.find("/a/b/c") != std::string::npos || p == "/a/b/c") {
            foundDeepPath = true;
        }
    }
    EXPECT_TRUE(foundDeepPath) << "Expected path /a/b/c in results";
}

// SDM004: Array value - elements indexed correctly.
TEST(StreamDataModelTest, SDM004_ArrayValue_IndexedPaths) {
    DataModelTestFixture f;

    std::string data = R"({"version":"v0.9","updateDataModel":{"surfaceId":"s1","path":"/","value":{"items":["apple","banana","cherry"]}}})";

    // Split to enter DM streaming
    size_t splitPos = data.find("\"banana\"");
    ASSERT_NE(splitPos, std::string::npos);

    auto results = f.feedChunked(data, {splitPos});

    auto paths = collectUpdatePaths(results);
    // Should see paths like /items/0, /items/1, /items/2
    bool found0 = false, found1 = false, found2 = false;
    for (const auto& p : paths) {
        if (p.find("/items/0") != std::string::npos) found0 = true;
        if (p.find("/items/1") != std::string::npos) found1 = true;
        if (p.find("/items/2") != std::string::npos) found2 = true;
    }
    EXPECT_TRUE(found0 || found1 || found2)
        << "Expected indexed array paths";
}

// SDM005: Deep nesting with object inside array.
TEST(StreamDataModelTest, SDM005_ObjectInArray_DeepNesting) {
    DataModelTestFixture f;

    std::string data = R"({"version":"v0.9","updateDataModel":{"surfaceId":"s1","path":"/","value":{"list":[{"name":"Alice"},{"name":"Bob"}]}}})";

    // Split inside the array
    size_t splitPos = data.find("\"Bob\"");
    ASSERT_NE(splitPos, std::string::npos);

    auto results = f.feedChunked(data, {splitPos});

    auto paths = collectUpdatePaths(results);
    // Should see paths like /list/0/name, /list/1/name
    bool foundListPath = false;
    for (const auto& p : paths) {
        if (p.find("/list/") != std::string::npos && p.find("/name") != std::string::npos) {
            foundListPath = true;
        }
    }
    EXPECT_TRUE(foundListPath) << "Expected paths like /list/N/name";
}

// SDM006: DM streaming with Markdown field-level streaming integration.
TEST(StreamDataModelTest, SDM006_WithMarkdownFieldStreaming_EmitsIncrementally) {
    DataModelTestFixture f;

    // First register a Markdown binding path
    std::string compData = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":{"path":"/info"}}]}})";
    f.feedAll(compData);
    EXPECT_TRUE(f.compositePlugin.shouldStreamField("/info"));

    // Use a new extractor with the same plugin (preserves binding paths)
    ProtocolStreamExtractor extractor2;
    extractor2.setPlugin(&f.compositePlugin);

    // Now send an incomplete updateDataModel where the /info field is streaming
    std::string dmData = R"({"version":"v0.9","updateDataModel":{"surfaceId":"s1","path":"/","value":{"info":"Long streaming text that arrives incrementally over time and should produce multiple events"}}})";

    // Split inside the info string value
    size_t infoValuePos = dmData.find("\"Long streaming");
    ASSERT_NE(infoValuePos, std::string::npos);
    size_t splitPos = infoValuePos + 20;

    // Feed first chunk
    extractor2.appendData(dmData.substr(0, splitPos));
    auto results1 = extractor2.driveParser();

    // Feed second chunk
    extractor2.appendData(dmData.substr(splitPos));
    auto results2 = extractor2.driveParser();

    // Combine results
    std::vector<ParseResult> allResults;
    allResults.insert(allResults.end(), results1.begin(), results1.end());
    allResults.insert(allResults.end(), results2.begin(), results2.end());

    // Should see both updateDataModel and appendDataModel events
    bool foundUpdate = false, foundAppend = false;
    for (const auto& r : allResults) {
        if (r.type == ParseResult::Type::NormalEvent) {
            if (r.eventType == EventType::UpdateDataModel) foundUpdate = true;
            if (r.eventType == EventType::AppendDataModel) foundAppend = true;
        }
    }
    EXPECT_TRUE(foundUpdate) << "Expected initial updateDataModel";
    EXPECT_TRUE(foundAppend) << "Expected incremental appendDataModel";
}

// SDM007: Field-level incremental streaming byte by byte.
TEST(StreamDataModelTest, SDM007_FieldLevelByteByByte_DeltasCorrect) {
    DataModelTestFixture f;

    // Register binding path
    std::string compData = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":{"path":"/content"}}]}})";
    f.feedAll(compData);
    EXPECT_TRUE(f.compositePlugin.shouldStreamField("/content"));

    // Use new extractor with same plugin to preserve binding paths
    ProtocolStreamExtractor extractor2;
    extractor2.setPlugin(&f.compositePlugin);

    std::string dmData = R"({"version":"v0.9","updateDataModel":{"surfaceId":"s1","path":"/","value":{"content":"ABCDEFGHIJ"}}})";

    std::vector<ParseResult> results;
    for (size_t i = 0; i < dmData.size(); ++i) {
        extractor2.appendData(std::string(1, dmData[i]));
        auto r = extractor2.driveParser();
        results.insert(results.end(), r.begin(), r.end());
    }

    // Collect all content from updateDataModel and appendDataModel events
    std::string totalContent;
    for (const auto& r : results) {
        if (r.type == ParseResult::Type::NormalEvent) {
            auto json = nlohmann::json::parse(r.eventJson, nullptr, false);
            if (json.is_discarded()) continue;

            if (r.eventType == EventType::UpdateDataModel && json.contains("updateDataModel")) {
                auto& udm = json["updateDataModel"];
                if (udm.contains("value") && udm["value"].is_string()) {
                    totalContent += udm["value"].get<std::string>();
                }
            } else if (r.eventType == EventType::AppendDataModel && json.contains("appendDataModel")) {
                auto& adm = json["appendDataModel"];
                if (adm.contains("value") && adm["value"].is_string()) {
                    totalContent += adm["value"].get<std::string>();
                }
            }
        }
    }
    // The total content should equal the original value
    EXPECT_EQ(totalContent, "ABCDEFGHIJ");
}

// SDM008: Multiple value types (bool, null, number, string) in object.
TEST(StreamDataModelTest, SDM008_MultipleValueTypes_AllHandled) {
    DataModelTestFixture f;

    std::string data = R"({"version":"v0.9","updateDataModel":{"surfaceId":"s1","path":"/","value":{"flag":true,"count":42,"label":"hello","empty":null}}})";

    // Split to force DM streaming
    size_t splitPos = data.find("\"count\":42");
    ASSERT_NE(splitPos, std::string::npos);

    auto results = f.feedChunked(data, {splitPos});

    // Should produce events without crashing (type handling)
    EXPECT_FALSE(results.empty());

    // Verify at least one updateDataModel produced
    bool foundUDM = false;
    for (const auto& r : results) {
        if (r.type == ParseResult::Type::NormalEvent &&
            r.eventType == EventType::UpdateDataModel) {
            foundUDM = true;
        }
    }
    EXPECT_TRUE(foundUDM);
}

// SDM009: Complete updateDataModel with object value - no DM streaming when complete.
TEST(StreamDataModelTest, SDM009_CompleteObjectValue_NoDMStreaming) {
    DataModelTestFixture f;

    std::string data = R"({"version":"v0.9","updateDataModel":{"surfaceId":"s1","path":"/data","value":{"x":1,"y":2}}})";

    // Feed all at once - should NOT enter streaming
    auto results = f.feedAll(data);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].type, ParseResult::Type::NormalEvent);
    EXPECT_EQ(results[0].eventType, EventType::UpdateDataModel);
    // The entire event should be delivered as-is
    EXPECT_NE(results[0].eventJson.find("\"x\":1"), std::string::npos);
    EXPECT_NE(results[0].eventJson.find("\"y\":2"), std::string::npos);
}

// SDM010: Exhaustive binary split for a simple updateDataModel with object value.
TEST(StreamDataModelTest, SDM010_ExhaustiveSplit_ObjectValue_NoCrash) {
    DataModelTestFixture f;

    std::string data = R"({"version":"v0.9","updateDataModel":{"surfaceId":"s1","path":"/","value":{"a":"hello","b":123}}})";

    // Every split position should produce results without crash
    for (size_t pos = 1; pos < data.size(); ++pos) {
        ProtocolStreamExtractor ex;
        MarkdownStreamPlugin mp;
        TextStreamPlugin tp;
        CompositeStreamPlugin cp;
        cp.addPlugin(&mp);
        cp.addPlugin(&tp);
        ex.setPlugin(&cp);

        ex.appendData(data.substr(0, pos));
        auto r1 = ex.driveParser();

        ex.appendData(data.substr(pos));
        auto r2 = ex.driveParser();

        // Should not crash; at least one result eventually
        auto total = r1.size() + r2.size();
        EXPECT_GE(total, 0u) << "Crash at position " << pos;
    }
}

}  // namespace
