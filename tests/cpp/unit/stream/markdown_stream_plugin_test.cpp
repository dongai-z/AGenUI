// MSP*: MarkdownStreamPlugin unit tests.
//
// Tests the Markdown streaming plugin for inline content streaming and
// DataModel-bound content streaming, including incremental delta delivery.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "stream/agenui_protocol_stream_extractor.h"
#include "stream/agenui_markdown_stream_plugin.h"
#include "nlohmann/json.hpp"

namespace {

using ::agenui::ProtocolStreamExtractor;
using ::agenui::MarkdownStreamPlugin;
using ParseResult = ProtocolStreamExtractor::ParseResult;
using EventType = ProtocolStreamExtractor::EventType;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Creates an extractor with Markdown plugin attached.
struct MarkdownTestFixture {
    ProtocolStreamExtractor extractor;
    MarkdownStreamPlugin plugin;

    MarkdownTestFixture() {
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

/// Reconstructs final content from streaming results.
/// A `content` field means "full current value" and REPLACES accumulated text.
/// An `appendContent` field means "delta to add" and appends.
std::string collectFullContent(const std::vector<ParseResult>& results) {
    std::string fullContent;
    for (const auto& r : results) {
        if (r.type != ParseResult::Type::ComponentUpdate) continue;
        auto json = nlohmann::json::parse(r.componentJson, nullptr, false);
        if (json.is_discarded()) continue;
        if (json.contains("content")) {
            fullContent = json["content"].get<std::string>(); // Replace
        } else if (json.contains("appendContent")) {
            fullContent += json["appendContent"].get<std::string>();
        }
    }
    return fullContent;
}

// ---------------------------------------------------------------------------
// Tests: Inline Content Streaming
// ---------------------------------------------------------------------------

// MSP001: Complete Markdown component (content value fully closed) - no streaming needed.
TEST(MarkdownStreamPluginTest, MSP001_CompleteContent_NoStreaming) {
    MarkdownTestFixture f;
    std::string data = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":"Hello World"}]}})";

    auto results = f.feedAll(data);

    // Should produce a single ComponentUpdate with the full component
    int componentUpdates = 0;
    for (const auto& r : results) {
        if (r.type == ParseResult::Type::ComponentUpdate) {
            componentUpdates++;
            auto json = nlohmann::json::parse(r.componentJson, nullptr, false);
            ASSERT_FALSE(json.is_discarded());
            EXPECT_EQ(json["content"].get<std::string>(), "Hello World");
            EXPECT_FALSE(json.contains("appendContent"));
        }
    }
    EXPECT_EQ(componentUpdates, 1);
}

// MSP002: Markdown inline content not closed - plugin takes over.
TEST(MarkdownStreamPluginTest, MSP002_IncompleteContent_PluginTakesOver) {
    MarkdownTestFixture f;

    // First chunk: component with incomplete content
    std::string chunk1 = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":"Hello Wo)";

    f.extractor.appendData(chunk1);
    auto results1 = f.extractor.driveParser();

    // Should emit a partial component with content field
    ASSERT_FALSE(results1.empty());
    bool foundContent = false;
    for (const auto& r : results1) {
        if (r.type == ParseResult::Type::ComponentUpdate) {
            auto json = nlohmann::json::parse(r.componentJson, nullptr, false);
            if (!json.is_discarded() && json.contains("content")) {
                foundContent = true;
                // Content should contain initial partial text
                std::string content = json["content"].get<std::string>();
                EXPECT_FALSE(content.empty());
            }
        }
    }
    EXPECT_TRUE(foundContent) << "Expected initial content emission";
    EXPECT_TRUE(f.plugin.isComponentStreaming());
}

// MSP003: Incremental append delivers appendContent deltas.
TEST(MarkdownStreamPluginTest, MSP003_IncrementalAppend_EmitsAppendContent) {
    MarkdownTestFixture f;

    std::string chunk1 = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":"Hello)";
    std::string chunk2 = " World";
    std::string chunk3 = R"("}]}})";

    f.extractor.appendData(chunk1);
    auto results1 = f.extractor.driveParser();

    f.extractor.appendData(chunk2);
    auto results2 = f.extractor.driveParser();

    // Second batch should contain an appendContent delta
    bool foundAppend = false;
    for (const auto& r : results2) {
        if (r.type == ParseResult::Type::ComponentUpdate) {
            auto json = nlohmann::json::parse(r.componentJson, nullptr, false);
            if (!json.is_discarded() && json.contains("appendContent")) {
                foundAppend = true;
                std::string delta = json["appendContent"].get<std::string>();
                EXPECT_FALSE(delta.empty());
            }
        }
    }
    EXPECT_TRUE(foundAppend) << "Expected appendContent delta";

    // Close the component
    f.extractor.appendData(chunk3);
    auto results3 = f.extractor.driveParser();
    EXPECT_FALSE(f.plugin.isComponentStreaming());
}

// MSP004: Content closes - streaming ends, final delta emitted.
TEST(MarkdownStreamPluginTest, MSP004_ContentCloses_StreamingEnds) {
    MarkdownTestFixture f;

    std::string fullData = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":"ABCDEF"}]}})";

    // Split so content value is incomplete in first chunk
    size_t contentStart = fullData.find("\"ABCDEF\"");
    ASSERT_NE(contentStart, std::string::npos);
    size_t splitPos = contentStart + 4; // "ABC

    auto results = f.feedChunked(fullData, {splitPos});

    // Verify streaming ended
    EXPECT_FALSE(f.plugin.isComponentStreaming());

    // Verify full content reconstruction
    std::string full = collectFullContent(results);
    EXPECT_EQ(full, "ABCDEF");
}

// MSP005: Escape characters at chunk boundary.
TEST(MarkdownStreamPluginTest, MSP005_EscapeAtBoundary_NoTruncation) {
    MarkdownTestFixture f;

    // Content with escape: "line1\nline2\ttab"
    std::string fullData = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":"line1\nline2\ttab"}]}})";

    // Split right at the backslash before 'n'
    size_t bsPos = fullData.find("line1\\n");
    ASSERT_NE(bsPos, std::string::npos);
    size_t splitPos = bsPos + 5; // After "line1" before "\"

    auto results = f.feedChunked(fullData, {splitPos});
    EXPECT_FALSE(f.plugin.isComponentStreaming());

    // nlohmann::json unescapes \n to real newline, \t to real tab
    std::string full = collectFullContent(results);
    EXPECT_EQ(full, "line1\nline2\ttab");
}

// MSP006: Chinese content incremental streaming.
TEST(MarkdownStreamPluginTest, MSP006_ChineseContent_IncrementalCorrect) {
    MarkdownTestFixture f;

    // "# 标题\n正文内容" — the \n is a JSON escape in the source, parsed as newline
    std::string chineseContent = "# \xe6\xa0\x87\xe9\xa2\x98\\n\xe6\xad\xa3\xe6\x96\x87\xe5\x86\x85\xe5\xae\xb9";
    std::string fullData = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"s1\",\"version\":\"1\",\"components\":[{\"id\":\"md1\",\"component\":\"Markdown\",\"content\":\"" + chineseContent + "\"}]}}";

    // Split in the middle of the Chinese text
    size_t contentQuotePos = fullData.find("\"# ");
    ASSERT_NE(contentQuotePos, std::string::npos);
    size_t splitPos = contentQuotePos + 6; // In the middle of Chinese chars

    auto results = f.feedChunked(fullData, {splitPos});
    EXPECT_FALSE(f.plugin.isComponentStreaming());

    // nlohmann::json will unescape \\n to real newline
    std::string full = collectFullContent(results);
    std::string expected = "# \xe6\xa0\x87\xe9\xa2\x98\n\xe6\xad\xa3\xe6\x96\x87\xe5\x86\x85\xe5\xae\xb9";
    EXPECT_EQ(full, expected);
}

// MSP007: Emoji content streaming - verify no crash and content eventually produced.
TEST(MarkdownStreamPluginTest, MSP007_EmojiContent_NoCrash) {
    MarkdownTestFixture f;

    std::string emojiContent = "Hello \xf0\x9f\x8e\x89 world";
    std::string fullData = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"s1\",\"version\":\"1\",\"components\":[{\"id\":\"md1\",\"component\":\"Markdown\",\"content\":\"" + emojiContent + "\"}]}}";

    // Split in the middle of the emoji
    size_t emojiPos = fullData.find("\xf0\x9f\x8e\x89");
    ASSERT_NE(emojiPos, std::string::npos);
    size_t splitPos = emojiPos + 2; // Middle of 4-byte emoji

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

// MSP008: Data-binding component - collectBindingPath called.
TEST(MarkdownStreamPluginTest, MSP008_DataBinding_CollectsBindingPath) {
    MarkdownTestFixture f;

    // Complete Markdown component with data binding
    std::string data = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":{"path":"/info"}}]}})";

    auto results = f.feedAll(data);

    // After processing, the binding path should be registered
    EXPECT_TRUE(f.plugin.shouldStreamField("/info"));
    EXPECT_FALSE(f.plugin.shouldStreamField("/other"));
}

// MSP009: DataModel streaming with nested object - extractor navigates to leaf string
// and uses shouldStreamField to initiate field-level incremental streaming.
TEST(MarkdownStreamPluginTest, MSP009_DataModelNestedObject_ExtractorFieldStreaming) {
    MarkdownTestFixture f;

    // Register binding path via a complete Markdown component with data binding
    std::string compData = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":{"path":"/route/info"}}]}})";
    f.feedAll(compData);

    // Verify path registered
    EXPECT_TRUE(f.plugin.shouldStreamField("/route/info"));

    // Send incomplete updateDataModel with nested object value.
    // When value is an object, the extractor handles DM streaming itself
    // (navigates to the leaf string and uses shouldStreamField for field-level streaming).
    ProtocolStreamExtractor extractor2;
    extractor2.setPlugin(&f.plugin);

    std::string dmChunk1 = R"({"version":"v0.9","updateDataModel":{"surfaceId":"s1","path":"/","value":{"route":{"info":"Hello streaming)";

    extractor2.appendData(dmChunk1);
    auto results1 = extractor2.driveParser();

    // The extractor should emit an updateDataModel event for the leaf field /route/info
    bool foundUDM = false;
    for (const auto& r : results1) {
        if (r.type == ParseResult::Type::NormalEvent &&
            r.eventType == EventType::UpdateDataModel) {
            foundUDM = true;
            // Verify the event targets the correct path
            auto json = nlohmann::json::parse(r.eventJson, nullptr, false);
            if (!json.is_discarded() && json.contains("updateDataModel")) {
                auto& udm = json["updateDataModel"];
                EXPECT_EQ(udm["path"].get<std::string>(), "/route/info");
                EXPECT_EQ(udm["surfaceId"].get<std::string>(), "s1");
            }
        }
    }
    EXPECT_TRUE(foundUDM) << "Expected updateDataModel event for leaf field /route/info";
}

// MSP010: DataModel incremental append emits appendDataModel.
TEST(MarkdownStreamPluginTest, MSP010_DataModelIncrement_EmitsAppendDataModel) {
    MarkdownTestFixture f;

    // Register binding path
    std::string compData = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":{"path":"/text"}}]}})";
    f.feedAll(compData);
    EXPECT_TRUE(f.plugin.shouldStreamField("/text"));

    // Use new extractor to avoid reset clearing binding paths
    ProtocolStreamExtractor extractor2;
    extractor2.setPlugin(&f.plugin);

    // Incomplete datamodel
    std::string dmChunk1 = R"({"version":"v0.9","updateDataModel":{"surfaceId":"s1","path":"/","value":{"text":"Hello)";
    extractor2.appendData(dmChunk1);
    extractor2.driveParser();

    // Append more data
    std::string dmChunk2 = " World";
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

// MSP011: Nested object binding path navigation.
TEST(MarkdownStreamPluginTest, MSP011_NestedBindingPath_CorrectNavigation) {
    MarkdownTestFixture f;

    // Register deep binding path (no reset - preserves binding paths)
    std::string compData = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":{"path":"/a/b/c"}}]}})";
    f.feedAll(compData);

    EXPECT_TRUE(f.plugin.shouldStreamField("/a/b/c"));
    EXPECT_FALSE(f.plugin.shouldStreamField("/a/b"));
    EXPECT_FALSE(f.plugin.shouldStreamField("/a/b/c/d"));
}

// MSP012: Multiple binding paths.
TEST(MarkdownStreamPluginTest, MSP012_MultipleBindingPaths_AllRegistered) {
    MarkdownTestFixture f;

    std::string data = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":{"path":"/path1"}},{"id":"md2","component":"Markdown","content":{"path":"/path2"}}]}})";
    f.feedAll(data);

    EXPECT_TRUE(f.plugin.shouldStreamField("/path1"));
    EXPECT_TRUE(f.plugin.shouldStreamField("/path2"));
    EXPECT_FALSE(f.plugin.shouldStreamField("/path3"));
}

// MSP014: shouldStreamField correctness.
TEST(MarkdownStreamPluginTest, MSP014_ShouldStreamField_CorrectBehavior) {
    MarkdownTestFixture f;

    // No binding paths registered yet
    EXPECT_FALSE(f.plugin.shouldStreamField("/anything"));

    // Register a path
    std::string data = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":{"path":"/my/path"}}]}})";
    f.feedAll(data);

    EXPECT_TRUE(f.plugin.shouldStreamField("/my/path"));
    EXPECT_FALSE(f.plugin.shouldStreamField("/my"));
    EXPECT_FALSE(f.plugin.shouldStreamField("/my/path/deeper"));
}

// MSP015: Reset clears all state.
TEST(MarkdownStreamPluginTest, MSP015_Reset_ClearsAllState) {
    MarkdownTestFixture f;

    // Register path and enter streaming
    std::string data = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":{"path":"/info"}}]}})";
    f.feedAll(data);
    EXPECT_TRUE(f.plugin.shouldStreamField("/info"));

    f.plugin.reset();

    EXPECT_FALSE(f.plugin.isComponentStreaming());
    EXPECT_FALSE(f.plugin.isDataModelStreaming());
    // Note: binding paths may or may not be cleared by reset() depending on implementation
}

// MSP016: Content with escaped quotes, byte by byte.
TEST(MarkdownStreamPluginTest, MSP016_EscapedQuotes_ByteByByte) {
    MarkdownTestFixture f;

    // "say \"hello\" end"
    std::string fullData = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":"say \"hello\" end"}]}})";

    auto results = f.feedByteByByte(fullData);
    EXPECT_FALSE(f.plugin.isComponentStreaming());

    // nlohmann::json unescapes \" to " when parsing
    std::string full = collectFullContent(results);
    EXPECT_EQ(full, "say \"hello\" end");
}

// MSP017: Non-Markdown component is not handled by plugin.
TEST(MarkdownStreamPluginTest, MSP017_NonMarkdownComponent_NotHandled) {
    MarkdownTestFixture f;

    std::string data = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"btn1","component":"Button","title":"Click"}]}})";

    auto results = f.feedAll(data);

    // Should produce normal ComponentUpdate without plugin interference
    int componentUpdates = 0;
    for (const auto& r : results) {
        if (r.type == ParseResult::Type::ComponentUpdate) {
            componentUpdates++;
            auto json = nlohmann::json::parse(r.componentJson, nullptr, false);
            ASSERT_FALSE(json.is_discarded());
            EXPECT_EQ(json["component"].get<std::string>(), "Button");
        }
    }
    EXPECT_EQ(componentUpdates, 1);
}

}  // namespace
