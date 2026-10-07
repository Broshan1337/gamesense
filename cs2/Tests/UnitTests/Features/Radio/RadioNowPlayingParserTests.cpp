#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <Features/Radio/RadioNowPlayingParser.h>

#include <string>
#include <string_view>

namespace
{

// "\x1F" adjacent to a letter is a broken hex escape in C++ string literals (A-F are hex
// digits), so every test record is built with the char value instead.
constexpr char kSep = '\x1F';

std::string mprisLine(std::string_view player, std::string_view title, std::string_view artist, std::string_view status)
{
    std::string line{player};
    line += kSep;
    line += title;
    line += kSep;
    line += artist;
    line += kSep;
    line += status;
    return line;
}

class RadioNowPlayingParserTest : public testing::Test {
protected:
    char buffer[160]{};
};

// --- ICY StreamTitle extraction (host probe writes the raw metadata block shape) ---

TEST_F(RadioNowPlayingParserTest, ExtractsTitleFromRealIcyBlock) {
    constexpr std::string_view block = "StreamTitle='Reptilia - The Strokes';StreamUrl='https://listenapi.planetradio.co.uk/api9.2/eventdata/418406589';";
    EXPECT_TRUE(RadioNowPlayingParser::extractStreamTitle(block.data(), buffer, sizeof(buffer)));
    EXPECT_STREQ(buffer, "Reptilia - The Strokes");
}

TEST_F(RadioNowPlayingParserTest, ExtractsTitleAfterOtherFields) {
    constexpr std::string_view block = "StreamUrl='https://example.com/1';StreamTitle='Song';";
    EXPECT_TRUE(RadioNowPlayingParser::extractStreamTitle(block.data(), buffer, sizeof(buffer)));
    EXPECT_STREQ(buffer, "Song");
}

TEST_F(RadioNowPlayingParserTest, ExtractsBareStreamTitleBlock) {
    constexpr std::string_view block = "StreamTitle='X';";
    EXPECT_TRUE(RadioNowPlayingParser::extractStreamTitle(block.data(), buffer, sizeof(buffer)));
    EXPECT_STREQ(buffer, "X");
}

TEST_F(RadioNowPlayingParserTest, ReturnsFalseWhenBlockHasNoStreamTitle) {
    constexpr std::string_view block = "StreamUrl='https://example.com/1';";
    EXPECT_FALSE(RadioNowPlayingParser::extractStreamTitle(block.data(), buffer, sizeof(buffer)));
}

TEST_F(RadioNowPlayingParserTest, ReturnsFalseForEmptyTitle) {
    constexpr std::string_view block = "StreamTitle='';";
    EXPECT_FALSE(RadioNowPlayingParser::extractStreamTitle(block.data(), buffer, sizeof(buffer)));
}

TEST_F(RadioNowPlayingParserTest, ReturnsFalseForUnterminatedTitle) {
    constexpr std::string_view block = "StreamTitle='Song";
    EXPECT_FALSE(RadioNowPlayingParser::extractStreamTitle(block.data(), buffer, sizeof(buffer)));
}

TEST_F(RadioNowPlayingParserTest, TruncatesLongTitlesToCapacity) {
    constexpr std::string_view block = "StreamTitle='ABCDEFGHIJKLMNOPQRSTUV';";
    // capacity 8 -> 7 chars + null terminator
    EXPECT_TRUE(RadioNowPlayingParser::extractStreamTitle(block.data(), buffer, 8));
    EXPECT_STREQ(buffer, "ABCDEFG");
}

TEST_F(RadioNowPlayingParserTest, CopiesUtf8BytesUntouched) {
    constexpr std::string_view block = "StreamTitle='\xC3\x85" "lesund - \xC3\x85" "ge';StreamUrl='x';";
    EXPECT_TRUE(RadioNowPlayingParser::extractStreamTitle(block.data(), buffer, sizeof(buffer)));
    EXPECT_STREQ(buffer, "\xC3\x85" "lesund - \xC3\x85" "ge");
}

// --- playerctl record parsing (0x1f-separated: player, title, artist, status) ---

TEST_F(RadioNowPlayingParserTest, ParsesPlayingRecord) {
    const std::string line = mprisLine("Spotify", "Bohemian Rhapsody", "Queen", "Playing");
    MprisNowPlaying out{};
    EXPECT_TRUE(RadioNowPlayingParser::parseMprisLine(line.data(), out));
    EXPECT_STREQ(out.player, "Spotify");
    EXPECT_STREQ(out.title, "Bohemian Rhapsody");
    EXPECT_STREQ(out.artist, "Queen");
    EXPECT_FALSE(out.paused);
}

TEST_F(RadioNowPlayingParserTest, MarksPausedRecord) {
    const std::string line = mprisLine("Spotify", "Song", "Artist", "Paused");
    MprisNowPlaying out{};
    EXPECT_TRUE(RadioNowPlayingParser::parseMprisLine(line.data(), out));
    EXPECT_TRUE(out.paused);
}

TEST_F(RadioNowPlayingParserTest, AllowsEmptyArtist) {
    const std::string line = mprisLine("Firefox", "Some Video Title", "", "Playing");
    MprisNowPlaying out{};
    EXPECT_TRUE(RadioNowPlayingParser::parseMprisLine(line.data(), out));
    EXPECT_STREQ(out.player, "Firefox");
    EXPECT_STREQ(out.title, "Some Video Title");
    EXPECT_STREQ(out.artist, "");
}

TEST_F(RadioNowPlayingParserTest, ReturnsFalseWhenPlayerIsEmpty) {
    const std::string line = mprisLine("", "Song", "Artist", "Playing");
    MprisNowPlaying out{};
    EXPECT_FALSE(RadioNowPlayingParser::parseMprisLine(line.data(), out));
}

TEST_F(RadioNowPlayingParserTest, ReturnsFalseWhenTitleIsEmpty) {
    const std::string line = mprisLine("Spotify", "", "Artist", "Playing");
    MprisNowPlaying out{};
    EXPECT_FALSE(RadioNowPlayingParser::parseMprisLine(line.data(), out));
}

TEST_F(RadioNowPlayingParserTest, ReturnsFalseForTooFewFields) {
    const std::string line = std::string{"Spotify"} + kSep + "Song";
    MprisNowPlaying out{};
    EXPECT_FALSE(RadioNowPlayingParser::parseMprisLine(line.data(), out));
}

TEST_F(RadioNowPlayingParserTest, TruncatesLongTitleToBufferCapacity) {
    const std::string longTitle(200, 'x');
    const std::string line = mprisLine("Spotify", longTitle, "Artist", "Playing");
    MprisNowPlaying out{};
    EXPECT_TRUE(RadioNowPlayingParser::parseMprisLine(line.data(), out));
    EXPECT_EQ(std::string{out.title}, longTitle.substr(0, sizeof(out.title) - 1));
}

}
