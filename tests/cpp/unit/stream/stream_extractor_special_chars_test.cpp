// SSC*: ProtocolStreamExtractor special character tests.
//
// Verifies correct handling of Chinese characters, emoji, and other
// multi-byte UTF-8 sequences when split at arbitrary byte boundaries.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "stream/agenui_protocol_stream_extractor.h"

namespace {

using ::agenui::ProtocolStreamExtractor;
using ParseResult = ProtocolStreamExtractor::ParseResult;
using EventType = ProtocolStreamExtractor::EventType;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// SSC001: Chinese characters in value.
TEST(StreamExtractorSpecialCharsTest, SSC001_ChineseInValue_CorrectlyParsed) {
    std::string data = R"({"version":"v0.9","updateDataModel":{"surfaceId":"s1","path":"/","value":{"text":"\u4f60\u597d\u4e16\u754c"}}})";

    ProtocolStreamExtractor x;
    x.appendData(data);
    auto results = x.driveParser();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].eventType, EventType::UpdateDataModel);
    // Verify the Chinese text is in the output
    EXPECT_NE(results[0].eventJson.find("\\u4f60\\u597d\\u4e16\\u754c"), std::string::npos);
}

// SSC002: Chinese UTF-8 bytes split at multi-byte boundary.
// Chinese char "你" = 0xE4 0xBD 0xA0 (3 bytes)
TEST(StreamExtractorSpecialCharsTest, SSC002_ChineseUTF8SplitAtByteBoundary_Reassembles) {
    // Use raw UTF-8 Chinese characters directly in JSON value
    std::string data = "{\"version\":\"v0.9\",\"updateDataModel\":{\"surfaceId\":\"s1\",\"path\":\"/\",\"value\":{\"text\":\"\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c\"}}}";

    // Find the Chinese text start position
    size_t textPos = data.find("\xe4\xbd\xa0");
    ASSERT_NE(textPos, std::string::npos);

    // Split in the middle of the first Chinese character (after 1st byte of 3-byte sequence)
    auto results1 = feedBinarySplit(data, textPos + 1);
    ASSERT_EQ(results1.size(), 1u);
    EXPECT_EQ(results1[0].eventType, EventType::UpdateDataModel);
    EXPECT_NE(results1[0].eventJson.find("\xe4\xbd\xa0\xe5\xa5\xbd"), std::string::npos);

    // Split after 2nd byte of first character
    auto results2 = feedBinarySplit(data, textPos + 2);
    ASSERT_EQ(results2.size(), 1u);
    EXPECT_EQ(results2[0].eventType, EventType::UpdateDataModel);
}

// SSC003: Emoji in value.
// Emoji "🎉" = 0xF0 0x9F 0x8E 0x89 (4 bytes)
TEST(StreamExtractorSpecialCharsTest, SSC003_EmojiInValue_CorrectlyParsed) {
    std::string data = "{\"version\":\"v0.9\",\"updateDataModel\":{\"surfaceId\":\"s1\",\"path\":\"/\",\"value\":{\"text\":\"Hello \xf0\x9f\x8e\x89\xf0\x9f\x9a\x80\"}}}";

    ProtocolStreamExtractor x;
    x.appendData(data);
    auto results = x.driveParser();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].eventType, EventType::UpdateDataModel);
    // Verify emojis are preserved
    EXPECT_NE(results[0].eventJson.find("\xf0\x9f\x8e\x89"), std::string::npos);
    EXPECT_NE(results[0].eventJson.find("\xf0\x9f\x9a\x80"), std::string::npos);
}

// SSC004: Emoji 4-byte UTF-8 split at each byte boundary.
TEST(StreamExtractorSpecialCharsTest, SSC004_EmojiSplitAtEveryByte_Reassembles) {
    std::string data = "{\"version\":\"v0.9\",\"updateDataModel\":{\"surfaceId\":\"s1\",\"path\":\"/\",\"value\":{\"text\":\"A\xf0\x9f\x8e\x89Z\"}}}";

    size_t emojiPos = data.find("\xf0\x9f\x8e\x89");
    ASSERT_NE(emojiPos, std::string::npos);

    // Split at each byte within the emoji
    for (size_t offset = 1; offset <= 3; ++offset) {
        auto results = feedBinarySplit(data, emojiPos + offset);
        ASSERT_EQ(results.size(), 1u)
            << "Failed splitting emoji at byte offset " << offset;
        EXPECT_EQ(results[0].eventType, EventType::UpdateDataModel);
        EXPECT_NE(results[0].eventJson.find("\xf0\x9f\x8e\x89"), std::string::npos)
            << "Emoji corrupted at byte offset " << offset;
    }
}

// SSC005: Mixed Chinese + emoji + ASCII, fed byte by byte.
TEST(StreamExtractorSpecialCharsTest, SSC005_MixedUTF8ByteByByte_AllCorrect) {
    // "你好🌍world"
    std::string data = "{\"version\":\"v0.9\",\"updateDataModel\":{\"surfaceId\":\"s1\",\"path\":\"/\",\"value\":{\"text\":\"\xe4\xbd\xa0\xe5\xa5\xbd\xf0\x9f\x8c\x8dworld\"}}}";

    auto results = feedByteByByte(data);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].eventType, EventType::UpdateDataModel);
    // Verify all parts are present
    EXPECT_NE(results[0].eventJson.find("\xe4\xbd\xa0\xe5\xa5\xbd"), std::string::npos); // 你好
    EXPECT_NE(results[0].eventJson.find("\xf0\x9f\x8c\x8d"), std::string::npos);         // 🌍
    EXPECT_NE(results[0].eventJson.find("world"), std::string::npos);
}

// SSC006: JSON-escaped Unicode sequences split in the middle.
TEST(StreamExtractorSpecialCharsTest, SSC006_EscapedUnicodeSplit_PreservesSequence) {
    // "\\u4f60\\u597d" represents escaped unicode in JSON
    std::string data = R"({"version":"v0.9","updateDataModel":{"surfaceId":"s1","path":"/","value":{"text":"\u4f60\u597d"}}})";

    // Find the first \u sequence
    size_t uPos = data.find("\\u4f60");
    ASSERT_NE(uPos, std::string::npos);

    // Split in the middle of the escape sequence: \u4f | 60
    auto results1 = feedBinarySplit(data, uPos + 4);
    ASSERT_EQ(results1.size(), 1u);
    EXPECT_EQ(results1[0].eventType, EventType::UpdateDataModel);
    EXPECT_NE(results1[0].eventJson.find("\\u4f60"), std::string::npos);

    // Split between two escape sequences
    size_t u2Pos = data.find("\\u597d");
    auto results2 = feedBinarySplit(data, u2Pos);
    ASSERT_EQ(results2.size(), 1u);
    EXPECT_NE(results2[0].eventJson.find("\\u597d"), std::string::npos);
}

// SSC007: Long Chinese content (>1KB) fed byte by byte.
TEST(StreamExtractorSpecialCharsTest, SSC007_LongChineseByteByByte_Complete) {
    // Build a string with repeated Chinese characters (each 3 bytes, 400 chars = 1200 bytes)
    std::string chineseContent;
    for (int i = 0; i < 400; ++i) {
        chineseContent += "\xe4\xb8\xad"; // "中"
    }

    std::string data = "{\"version\":\"v0.9\",\"updateDataModel\":{\"surfaceId\":\"s1\",\"path\":\"/\",\"value\":{\"text\":\"" + chineseContent + "\"}}}";

    auto results = feedByteByByte(data);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].eventType, EventType::UpdateDataModel);
    // Verify content length is preserved
    EXPECT_NE(results[0].eventJson.find(chineseContent), std::string::npos);
}

// SSC008: Mixed CJK characters and special punctuation.
TEST(StreamExtractorSpecialCharsTest, SSC008_CJKMixedPunctuation_CorrectParsing) {
    // "中文「引号」日本語（括弧）한국어"
    std::string mixedContent = "\xe4\xb8\xad\xe6\x96\x87\xe3\x80\x8c\xe5\xbc\x95\xe5\x8f\xb7\xe3\x80\x8d\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
    std::string data = "{\"version\":\"v0.9\",\"updateDataModel\":{\"surfaceId\":\"s1\",\"path\":\"/\",\"value\":{\"text\":\"" + mixedContent + "\"}}}";

    // Exhaustive binary split
    for (size_t pos = 1; pos < data.size(); ++pos) {
        auto results = feedBinarySplit(data, pos);
        ASSERT_EQ(results.size(), 1u)
            << "Failed at split position " << pos;
        EXPECT_EQ(results[0].eventType, EventType::UpdateDataModel);
    }
}

// SSC009: Emoji skin tone modifier (multi-codepoint emoji, 8+ bytes).
TEST(StreamExtractorSpecialCharsTest, SSC009_MultiCodepointEmoji_CorrectParsing) {
    // 👨‍💻 = 👨 (4 bytes) + ZWJ (3 bytes) + 💻 (4 bytes) = 11 bytes total
    std::string emoji = "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x92\xbb";
    std::string data = "{\"version\":\"v0.9\",\"updateDataModel\":{\"surfaceId\":\"s1\",\"path\":\"/\",\"value\":{\"text\":\"" + emoji + "\"}}}";

    auto results = feedByteByByte(data);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_NE(results[0].eventJson.find(emoji), std::string::npos);
}

// SSC010: updateComponents with Chinese component content, byte-by-byte.
TEST(StreamExtractorSpecialCharsTest, SSC010_ChineseInComponentsByteByByte_Correct) {
    std::string data = "{\"version\":\"v0.9\",\"updateComponents\":{\"surfaceId\":\"s1\",\"version\":\"1\",\"components\":[{\"id\":\"c1\",\"component\":\"Text\",\"text\":\"\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c\"}]}}";

    auto results = feedByteByByte(data);

    int componentUpdates = 0;
    for (const auto& r : results) {
        if (r.type == ParseResult::Type::ComponentUpdate) {
            componentUpdates++;
            EXPECT_NE(r.componentJson.find("\xe4\xbd\xa0\xe5\xa5\xbd"), std::string::npos);
        }
    }
    EXPECT_EQ(componentUpdates, 1);
}

}  // namespace
