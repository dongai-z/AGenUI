// CSP*: CompositeStreamPlugin unit tests.
//
// Tests the composite plugin that manages both Markdown and Text sub-plugins
// to verify correct delegation, mutual exclusivity, and state isolation.

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

struct CompositeTestFixture {
    ProtocolStreamExtractor extractor;
    MarkdownStreamPlugin markdownPlugin;
    TextStreamPlugin textPlugin;
    CompositeStreamPlugin compositePlugin;

    CompositeTestFixture() {
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

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// CSP001: Both Markdown and Text components in same updateComponents - both handled.
TEST(CompositeStreamPluginTest, CSP001_MarkdownAndText_BothProcessed) {
    CompositeTestFixture f;

    std::string data = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":{"path":"/mdPath"}},{"id":"t1","component":"Text","text":{"path":"/txtPath"}}]}})";

    f.feedAll(data);

    // Both plugins should have collected their binding paths
    EXPECT_TRUE(f.markdownPlugin.shouldStreamField("/mdPath"));
    EXPECT_TRUE(f.textPlugin.shouldStreamField("/txtPath"));
}

// CSP002: Markdown component streaming followed by Text component - correct delegation.
TEST(CompositeStreamPluginTest, CSP002_MarkdownThenText_CorrectDelegation) {
    CompositeTestFixture f;

    // Two complete components: Markdown first, Text second
    std::string data = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":"MD content"},{"id":"t1","component":"Text","text":"Text content"}]}})";

    auto results = f.feedAll(data);

    int componentUpdates = 0;
    bool foundMarkdown = false, foundText = false;
    for (const auto& r : results) {
        if (r.type == ParseResult::Type::ComponentUpdate) {
            componentUpdates++;
            auto json = nlohmann::json::parse(r.componentJson, nullptr, false);
            if (!json.is_discarded()) {
                if (json.contains("component")) {
                    if (json["component"] == "Markdown") foundMarkdown = true;
                    if (json["component"] == "Text") foundText = true;
                }
            }
        }
    }
    EXPECT_EQ(componentUpdates, 2);
    EXPECT_TRUE(foundMarkdown);
    EXPECT_TRUE(foundText);
}

// CSP003: shouldStreamField uses OR logic across sub-plugins.
TEST(CompositeStreamPluginTest, CSP003_ShouldStreamField_ORLogic) {
    CompositeTestFixture f;

    // Register Markdown binding path and Text binding path in one go
    std::string data = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":{"path":"/mdField"}},{"id":"t1","component":"Text","text":{"path":"/txtField"}}]}})";
    f.feedAll(data);

    // Composite should return true for either
    EXPECT_TRUE(f.compositePlugin.shouldStreamField("/mdField"));
    EXPECT_TRUE(f.compositePlugin.shouldStreamField("/txtField"));
    EXPECT_FALSE(f.compositePlugin.shouldStreamField("/unregistered"));
}

// CSP004: Reset clears all sub-plugins.
TEST(CompositeStreamPluginTest, CSP004_Reset_ClearsAllSubPlugins) {
    CompositeTestFixture f;

    std::string data = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":{"path":"/p1"}},{"id":"t1","component":"Text","text":{"path":"/p2"}}]}})";
    f.feedAll(data);

    f.compositePlugin.reset();
    EXPECT_FALSE(f.compositePlugin.isComponentStreaming());
    EXPECT_FALSE(f.compositePlugin.isDataModelStreaming());
    EXPECT_FALSE(f.markdownPlugin.isComponentStreaming());
    EXPECT_FALSE(f.textPlugin.isComponentStreaming());
}

// CSP005: Incomplete Markdown component - composite delegates to Markdown plugin.
TEST(CompositeStreamPluginTest, CSP005_IncompleteMarkdown_DelegatesToMarkdownPlugin) {
    CompositeTestFixture f;

    std::string chunk1 = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":"Streaming content)";

    f.extractor.appendData(chunk1);
    auto results = f.extractor.driveParser();

    EXPECT_TRUE(f.compositePlugin.isComponentStreaming());
    EXPECT_TRUE(f.markdownPlugin.isComponentStreaming());
    EXPECT_FALSE(f.textPlugin.isComponentStreaming());
}

// CSP006: Incomplete Text component - composite delegates to Text plugin.
TEST(CompositeStreamPluginTest, CSP006_IncompleteText_DelegatesToTextPlugin) {
    CompositeTestFixture f;

    std::string chunk1 = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"t1","component":"Text","text":"Streaming text)";

    f.extractor.appendData(chunk1);
    auto results = f.extractor.driveParser();

    EXPECT_TRUE(f.compositePlugin.isComponentStreaming());
    EXPECT_FALSE(f.markdownPlugin.isComponentStreaming());
    EXPECT_TRUE(f.textPlugin.isComponentStreaming());
}

// CSP007: Byte-by-byte with mixed components verifies no state leakage.
TEST(CompositeStreamPluginTest, CSP007_MixedComponentsByteByByte_NoStateLeak) {
    CompositeTestFixture f;

    std::string data = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":"MDText"},{"id":"t1","component":"Text","text":"TxtText"}]}})";

    auto results = f.feedByteByByte(data);
    EXPECT_FALSE(f.compositePlugin.isComponentStreaming());

    // Verify both components produced results
    int componentUpdates = 0;
    for (const auto& r : results) {
        if (r.type == ParseResult::Type::ComponentUpdate) {
            componentUpdates++;
        }
    }
    EXPECT_GE(componentUpdates, 2); // At least both components should be emitted
}

// CSP008: onComponentExtracted notifies all sub-plugins.
TEST(CompositeStreamPluginTest, CSP008_OnComponentExtracted_NotifiesAll) {
    CompositeTestFixture f;

    // A non-streaming component: both plugins' onComponentExtracted should be called
    std::string data = R"({"version":"v0.9","updateComponents":{"surfaceId":"s1","version":"1","components":[{"id":"md1","component":"Markdown","content":{"path":"/sharedPath"}}]}})";

    f.feedAll(data);

    // Only Markdown plugin should have registered this path (it matches Markdown)
    EXPECT_TRUE(f.markdownPlugin.shouldStreamField("/sharedPath"));
    // Text plugin should not register it (different component type)
    EXPECT_FALSE(f.textPlugin.shouldStreamField("/sharedPath"));
}

}  // namespace
