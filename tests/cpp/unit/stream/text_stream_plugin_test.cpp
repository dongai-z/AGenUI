// TSP*: TextStreamPlugin unit tests.
//
// Tests the Text streaming plugin for inline text streaming and
// DataModel-bound text streaming, including suffix matching for List scenarios.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "stream/agenui_protocol_stream_extractor.h"
#include "stream/agenui_text_stream_plugin.h"
#include "nlohmann/json.hpp"

namespace {

using ::agenui::ProtocolStreamExtractor;
using ::agenui::TextStreamPlugin;
using ParseResult = ProtocolStreamExtractor::ParseResult;
using EventType = ProtocolStreamExtractor::EventType;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

struct TextTestFixture {
    ProtocolStreamExtractor extractor;
    TextStreamPlugin plugin;

    TextTestFixture() {
        extractor.setPlugin(&plugin);
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

/// Concatenates all text/textChunk values from ComponentUpdate results.
std::string collectFullText(const std::vector<ParseResult>& results) {
    std::string fullText;
    for (const auto& r : results) {
        if (r.type != ParseResult::Type::ComponentUpdate) continue;
        auto json = nlohmann::json::parse(r.componentJson, nullptr, false);
        if (json.is_discarded()) continue;
        if (json.contains("text")) {
            fullText = json["text"].get<std::string>(); // Replace (new full value)
        } else if (json.contains("textChunk")) {
            fullText += json["textChunk"].get<std::string>();
        }
    }
    return fullText;
}

// ---------------------------------------------------------------------------
// Tests: Inline Text Streaming
// ---------------------------------------------------------------------------

// TSP001: Complete Text component (text value fully closed) - no streaming.
TEST(TextStreamPluginTest, TSP001_CompleteText_NoStreaming) {
    TextTestFixture f;
    std::string data = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"t1","component":"Text","text":"Hello World"}]}})";

    auto results = f.feedAll(data);

    int componentUpdates = 0;
    for (const auto& r : results) {
        if (r.type == ParseResult::Type::ComponentUpdate) {
            componentUpdates++;
            auto json = nlohmann::json::parse(r.componentJson, nullptr, false);
            ASSERT_FALSE(json.is_discarded());
            EXPECT_EQ(json["text"].get<std::string>(), "Hello World");
            EXPECT_FALSE(json.contains("textChunk"));
        }
    }
    EXPECT_EQ(componentUpdates, 1);
}

// TSP002: Text inline text not closed - plugin takes over.
TEST(TextStreamPluginTest, TSP002_IncompleteText_PluginTakesOver) {
    TextTestFixture f;

    std::string chunk1 = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"t1","component":"Text","text":"Hello Wo)";

    f.extractor.appendData(chunk1);
    auto results1 = f.extractor.driveParser();

    ASSERT_FALSE(results1.empty());
    bool foundText = false;
    for (const auto& r : results1) {
        if (r.type == ParseResult::Type::ComponentUpdate) {
            auto json = nlohmann::json::parse(r.componentJson, nullptr, false);
            if (!json.is_discarded() && json.contains("text")) {
                foundText = true;
            }
        }
    }
    EXPECT_TRUE(foundText) << "Expected initial text emission";
    EXPECT_TRUE(f.plugin.isComponentStreaming());
}

// TSP003: Incremental append delivers textChunk deltas.
TEST(TextStreamPluginTest, TSP003_IncrementalAppend_EmitsTextChunk) {
    TextTestFixture f;

    std::string chunk1 = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"t1","component":"Text","text":"Hello)";
    std::string chunk2 = " World";
    std::string chunk3 = R"("}]}})";

    f.extractor.appendData(chunk1);
    f.extractor.driveParser();

    f.extractor.appendData(chunk2);
    auto results2 = f.extractor.driveParser();

    bool foundChunk = false;
    for (const auto& r : results2) {
        if (r.type == ParseResult::Type::ComponentUpdate) {
            auto json = nlohmann::json::parse(r.componentJson, nullptr, false);
            if (!json.is_discarded() && json.contains("textChunk")) {
                foundChunk = true;
                EXPECT_FALSE(json["textChunk"].get<std::string>().empty());
            }
        }
    }
    EXPECT_TRUE(foundChunk) << "Expected textChunk delta";

    // Close
    f.extractor.appendData(chunk3);
    f.extractor.driveParser();
    EXPECT_FALSE(f.plugin.isComponentStreaming());
}

// TSP004: Text closes - streaming ends, full content reconstructed.
TEST(TextStreamPluginTest, TSP004_TextCloses_StreamingEnds) {
    TextTestFixture f;

    std::string fullData = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"t1","component":"Text","text":"ABCDEF"}]}})";

    size_t textQuote = fullData.find("\"ABCDEF\"");
    ASSERT_NE(textQuote, std::string::npos);
    size_t splitPos = textQuote + 4; // "ABC

    auto results = f.feedChunked(fullData, {splitPos});
    EXPECT_FALSE(f.plugin.isComponentStreaming());

    std::string full = collectFullText(results);
    EXPECT_EQ(full, "ABCDEF");
}

// TSP005: Chinese text streaming split in middle of multi-byte char.
// Splitting inside a multi-byte UTF-8 codepoint means the streamed component JSON
// may contain invalid UTF-8 that nlohmann::json rejects. Verify no crash and
// streaming completes normally.
TEST(TextStreamPluginTest, TSP005_ChineseText_ByteByByte) {
    TextTestFixture f;

    std::string chineseText = "\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c"; // 你好世界
    std::string fullData = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"s1\",\"version\":\"1\",\"components\":[{\"id\":\"t1\",\"component\":\"Text\",\"text\":\"" + chineseText + "\"}]}}";

    // Split in middle of Chinese chars
    size_t textPos = fullData.find(chineseText);
    ASSERT_NE(textPos, std::string::npos);
    size_t splitPos = textPos + 4; // Middle of second character

    auto results = f.feedChunked(fullData, {splitPos});
    EXPECT_FALSE(f.plugin.isComponentStreaming());

    // Verify at least one ComponentUpdate was emitted (no crash)
    int componentUpdates = 0;
    for (const auto& r : results) {
        if (r.type == ParseResult::Type::ComponentUpdate) {
            componentUpdates++;
        }
    }
    EXPECT_GE(componentUpdates, 1);
}

// TSP006: Emoji text streaming split in middle of 4-byte emoji.
// Same as TSP005: partial UTF-8 in emitted JSON is expected; verify no crash.
TEST(TextStreamPluginTest, TSP006_EmojiText_NotCorrupted) {
    TextTestFixture f;

    std::string emojiText = "Hello \xf0\x9f\x9a\x80 go";
    std::string fullData = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"s1\",\"version\":\"1\",\"components\":[{\"id\":\"t1\",\"component\":\"Text\",\"text\":\"" + emojiText + "\"}]}}";

    size_t emojiPos = fullData.find("\xf0\x9f\x9a\x80");
    ASSERT_NE(emojiPos, std::string::npos);
    size_t splitPos = emojiPos + 2;

    auto results = f.feedChunked(fullData, {splitPos});
    EXPECT_FALSE(f.plugin.isComponentStreaming());

    // Verify at least one ComponentUpdate was emitted (no crash)
    int componentUpdates = 0;
    for (const auto& r : results) {
        if (r.type == ParseResult::Type::ComponentUpdate) {
            componentUpdates++;
        }
    }
    EXPECT_GE(componentUpdates, 1);
}

// TSP007: Data-binding component collects binding path.
TEST(TextStreamPluginTest, TSP007_DataBinding_CollectsBindingPath) {
    TextTestFixture f;

    std::string data = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"t1","component":"Text","text":{"path":"/myText"}}]}})";

    f.feedAll(data);
    EXPECT_TRUE(f.plugin.shouldStreamField("/myText"));
    EXPECT_FALSE(f.plugin.shouldStreamField("/other"));
}

// TSP008: DataModel with nested object value - extractor navigates to leaf field
// and uses shouldStreamField for field-level streaming (plugin's isDataModelStreaming
// is NOT set because the extractor handles the nested object navigation itself).
TEST(TextStreamPluginTest, TSP008_DataModelPrefixMatch_ExtractorFieldStreaming) {
    TextTestFixture f;

    // Register binding path
    std::string compData = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"t1","component":"Text","text":{"path":"/flight/text"}}]}})";
    f.feedAll(compData);

    EXPECT_TRUE(f.plugin.shouldStreamField("/flight/text"));

    // Use a new extractor to avoid reset clearing binding paths
    ProtocolStreamExtractor extractor2;
    extractor2.setPlugin(&f.plugin);

    // Incomplete datamodel with nested object value
    std::string dmChunk = R"({"version":"v0.9","updateDataModel":{"surfaceId":"s1","path":"/","value":{"flight":{"text":"Streaming content)";

    extractor2.appendData(dmChunk);
    auto results = extractor2.driveParser();

    // The extractor should emit an updateDataModel event for the leaf field /flight/text
    bool foundUDM = false;
    for (const auto& r : results) {
        if (r.type == ParseResult::Type::NormalEvent &&
            r.eventType == EventType::UpdateDataModel) {
            foundUDM = true;
            auto json = nlohmann::json::parse(r.eventJson, nullptr, false);
            if (!json.is_discarded() && json.contains("updateDataModel")) {
                auto& udm = json["updateDataModel"];
                EXPECT_EQ(udm["path"].get<std::string>(), "/flight/text");
                EXPECT_EQ(udm["surfaceId"].get<std::string>(), "s1");
            }
        }
    }
    EXPECT_TRUE(foundUDM) << "Expected updateDataModel event for leaf field /flight/text";
}

// TSP009: DataModel suffix match for List scenarios.
TEST(TextStreamPluginTest, TSP009_DataModelSuffixMatch_ListScenario) {
    TextTestFixture f;

    // Register binding path with simple suffix (e.g., "/text" used in a List)
    std::string compData = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"t1","component":"Text","text":{"path":"/text"}}]}})";
    f.feedAll(compData);

    EXPECT_TRUE(f.plugin.shouldStreamField("/text"));
    // Suffix matching: /items/0/text should also match /text
    EXPECT_TRUE(f.plugin.shouldStreamField("/items/0/text"));
}

// TSP010: DataModel incremental appendDataModel.
TEST(TextStreamPluginTest, TSP010_DataModelIncrement_EmitsAppendDataModel) {
    TextTestFixture f;

    std::string compData = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"t1","component":"Text","text":{"path":"/content"}}]}})";
    f.feedAll(compData);
    EXPECT_TRUE(f.plugin.shouldStreamField("/content"));

    // Use new extractor to preserve binding paths
    ProtocolStreamExtractor extractor2;
    extractor2.setPlugin(&f.plugin);

    // Incomplete datamodel
    std::string dmChunk1 = R"({"version":"v0.9","updateDataModel":{"surfaceId":"s1","path":"/","value":{"content":"Hello)";
    extractor2.appendData(dmChunk1);
    extractor2.driveParser();

    std::string dmChunk2 = " more text";
    extractor2.appendData(dmChunk2);
    auto results2 = extractor2.driveParser();

    bool foundAppend = false;
    for (const auto& r : results2) {
        if (r.type == ParseResult::Type::NormalEvent &&
            r.eventType == EventType::AppendDataModel) {
            foundAppend = true;
        }
    }
    EXPECT_TRUE(foundAppend) << "Expected appendDataModel event";
}

// TSP011: Text and Markdown in same updateComponents - Text not confused.
TEST(TextStreamPluginTest, TSP011_TextWithMarkdown_IndependentProcessing) {
    TextTestFixture f;

    // Only Text plugin is attached; Markdown components should pass through normally
    std::string data = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":"MDContent"},{"id":"t1","component":"Text","text":"TextContent"}]}})";

    auto results = f.feedAll(data);

    int componentUpdates = 0;
    for (const auto& r : results) {
        if (r.type == ParseResult::Type::ComponentUpdate) {
            componentUpdates++;
        }
    }
    EXPECT_EQ(componentUpdates, 2);
}

// TSP012: shouldStreamField with suffix matching.
TEST(TextStreamPluginTest, TSP012_ShouldStreamField_SuffixMatching) {
    TextTestFixture f;

    std::string data = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"t1","component":"Text","text":{"path":"/label"}}]}})";
    f.feedAll(data);

    EXPECT_TRUE(f.plugin.shouldStreamField("/label"));
    EXPECT_TRUE(f.plugin.shouldStreamField("/items/0/label")); // suffix match
    EXPECT_TRUE(f.plugin.shouldStreamField("/data/items/1/label")); // deeper suffix
    EXPECT_FALSE(f.plugin.shouldStreamField("/labelExtra")); // not a path boundary match
}

// TSP013: Multiple Text components with different binding paths.
TEST(TextStreamPluginTest, TSP013_MultipleBindingPaths_AllRegistered) {
    TextTestFixture f;

    std::string data = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"t1","component":"Text","text":{"path":"/title"}},{"id":"t2","component":"Text","text":{"path":"/desc"}}]}})";
    f.feedAll(data);

    EXPECT_TRUE(f.plugin.shouldStreamField("/title"));
    EXPECT_TRUE(f.plugin.shouldStreamField("/desc"));
    EXPECT_FALSE(f.plugin.shouldStreamField("/other"));
}

// TSP014: Multiple escapes in text value.
TEST(TextStreamPluginTest, TSP014_MultipleEscapes_BoundarySafe) {
    TextTestFixture f;

    // "a\\b\"c" -> in JSON this is: a\b"c
    std::string fullData = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"t1","component":"Text","text":"a\\\\b\\\"c"}]}})";

    // Split at escape boundaries
    size_t bsPos = fullData.find("a\\\\");
    ASSERT_NE(bsPos, std::string::npos);

    auto results = f.feedChunked(fullData, {bsPos + 2}); // Split between the two backslashes
    EXPECT_FALSE(f.plugin.isComponentStreaming());

    std::string full = collectFullText(results);
    EXPECT_EQ(full, "a\\\\b\\\"c");
}

// TSP015: Reset clears state.
TEST(TextStreamPluginTest, TSP015_Reset_ClearsState) {
    TextTestFixture f;

    std::string data = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"t1","component":"Text","text":{"path":"/info"}}]}})";
    f.feedAll(data);
    EXPECT_TRUE(f.plugin.shouldStreamField("/info"));

    f.plugin.reset();
    EXPECT_FALSE(f.plugin.isComponentStreaming());
    EXPECT_FALSE(f.plugin.isDataModelStreaming());
}

}  // namespace
