#include "TestHarness.hpp"

#include <string>
#include <vector>

#include "ba/utils/RussianText.hpp"

TEST_CASE(TextNormalizeLowercasesAndCollapses) {
    CHECK_EQ(std::string("привет елка"), ba::text::Normalize("  ПРИВЕТ   Ёлка  "));
    CHECK_EQ(std::string("ростов-на-дону"), ba::text::Normalize("Ростов–на–Дону"));
    CHECK_EQ(std::string("15 000 руб"), ba::text::Normalize("15\u00A0000\tруб\n"));
    CHECK_EQ(std::string("думаю..."), ba::text::Normalize("Думаю…"));
}

TEST_CASE(TextTrimAndSplit) {
    CHECK_EQ(std::string("abc"), ba::text::Trim(" \t abc \r\n"));
    const auto words = ba::text::SplitWords("стол, кресло 2 и лампа");
    CHECK_EQ(std::size_t(5), words.size());
    CHECK_EQ(std::string("стол"), words[0]);
    CHECK_EQ(std::string("кресло"), words[1]);
    CHECK_EQ(std::string("2"), words[2]);
    CHECK_EQ(std::string("лампа"), words[4]);
}

TEST_CASE(TextContainsAny) {
    CHECK(ba::text::ContainsAny("нужен стол", {"шкаф", "стол"}));
    CHECK(!ba::text::ContainsAny("нужен стол", {"шкаф", "лампа"}));
    CHECK(!ba::text::ContainsAny("что угодно", {""}));
}

TEST_CASE(TextFormatsIntegersAndRubles) {
    CHECK_EQ(std::string("0"), ba::text::FormatInteger(0));
    CHECK_EQ(std::string("999"), ba::text::FormatInteger(999));
    CHECK_EQ(std::string("1 000"), ba::text::FormatInteger(1000));
    CHECK_EQ(std::string("15 000"), ba::text::FormatInteger(15000));
    CHECK_EQ(std::string("1 000 000"), ba::text::FormatInteger(1000000));
    CHECK_EQ(std::string("-1 500"), ba::text::FormatInteger(-1500));
    CHECK_EQ(std::string("15 000 ₽"), ba::text::FormatRub(15000));
}

TEST_CASE(TextJoinsLists) {
    CHECK_EQ(std::string("стол, кресло, лампа"),
             ba::text::JoinWithComma({"стол", "кресло", "лампа"}));
    CHECK_EQ(std::string("стол и кресло"), ba::text::JoinWithAnd({"стол", "кресло"}));
    CHECK_EQ(std::string("стол, кресло и лампа"),
             ba::text::JoinWithAnd({"стол", "кресло", "лампа"}));
    CHECK_EQ(std::string("стол"), ba::text::JoinWithAnd({"стол"}));
}

TEST_CASE(TextUtf8TruncationDoesNotBreakCharacters) {
    CHECK_EQ(std::size_t(6), ba::text::Utf8CodePointCount("привет"));
    CHECK_EQ(std::string("при"), ba::text::TruncateUtf8("привет", 3));
    CHECK_EQ(std::string("привет"), ba::text::TruncateUtf8("привет", 100));
    CHECK_EQ(std::string(""), ba::text::TruncateUtf8("привет", 0));
}

TEST_CASE(TextSanitizeRemovesControlCharacters) {
    const auto value = ba::text::SanitizeText("  стол\n\tс\u0001полкой  ", 100);
    CHECK_EQ(std::string("стол с полкой"), value);
    CHECK_EQ(std::string("сто"), ba::text::SanitizeText("стол", 3));
}
