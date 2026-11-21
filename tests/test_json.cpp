#include "TestHarness.hpp"

#include <sstream>
#include <string>

#include "TestData.hpp"
#include "ba/utils/Errors.hpp"
#include "ba/utils/JsonIO.hpp"

TEST_CASE(JsonReadsStreamWithinLimit) {
    std::istringstream input("{\"a\":1}");
    CHECK_EQ(std::string("{\"a\":1}"), ba::jsonio::ReadAll(input, 100));

    std::istringstream exact("abc");
    CHECK_EQ(std::string("abc"), ba::jsonio::ReadAll(exact, 3));

    std::istringstream tooLarge("abcdef");
    CHECK_THROWS_AS(ba::jsonio::ReadAll(tooLarge, 5), ba::AppError);

    std::istringstream empty("");
    CHECK_EQ(std::string(""), ba::jsonio::ReadAll(empty, 10));
}

TEST_CASE(JsonReadsFilesAndStripsBom) {
    const testdata::TempFile plain("json", "{\"ok\":true}");
    CHECK_EQ(std::string("{\"ok\":true}"), ba::jsonio::ReadFile(plain.String(), 100));

    const testdata::TempFile bom("json", "\xEF\xBB\xBF{\"ok\":true}");
    CHECK_EQ(std::string("{\"ok\":true}"), ba::jsonio::ReadFile(bom.String(), 100));

    CHECK_THROWS_AS(ba::jsonio::ReadFile(plain.String(), 2), ba::AppError);
    CHECK_THROWS_AS(ba::jsonio::ReadFile("no-such-file.json", 100), ba::AppError);
}

TEST_CASE(JsonParsesObjectsAndReportsErrors) {
    const auto object = ba::jsonio::ParseJson("{\"a\":1}", "test");
    CHECK(object.is_object());
    CHECK_EQ(1, object["a"].get<int>());

    const auto asObject = ba::jsonio::ParseObject("{\"a\":1}", "test");
    CHECK(asObject.is_object());

    CHECK_THROWS_AS(ba::jsonio::ParseJson("", "test"), ba::AppError);
    CHECK_THROWS_AS(ba::jsonio::ParseJson(" ", "test"), ba::AppError);
    CHECK_THROWS_AS(ba::jsonio::ParseJson("{oops", "test"), ba::AppError);
    CHECK_THROWS_AS(ba::jsonio::ParseObject("[]", "test"), ba::AppError);
    CHECK_THROWS_AS(ba::jsonio::ParseObject("42", "test"), ba::AppError);
    CHECK_THROWS_AS(ba::jsonio::ParseObject("null", "test"), ba::AppError);
}
