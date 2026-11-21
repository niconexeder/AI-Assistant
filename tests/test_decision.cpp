#include "TestHarness.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "TestData.hpp"
#include "ba/core/BundleSearch.hpp"
#include "ba/core/DecisionEngine.hpp"
#include "ba/models/Response.hpp"

namespace {

struct Fixture {
    Fixture()
        : CatalogData(
              {testdata::MakeListing("t-80", ba::Category::Table, 3500, "taganrog", 80,
                                     std::nullopt, std::nullopt, "СЕКРЕТНЫЙ СТОЛ",
                                     "Не показывать это описание"),
               testdata::MakeListing("t-100", ba::Category::Table, 4000, "taganrog", 100),
               testdata::MakeListing("t-null", ba::Category::Table, 3000),
               testdata::MakeListing("c-null", ba::Category::Chair, 1000),
               testdata::MakeListing("c-adjust", ba::Category::Chair, 2000, "taganrog",
                                     std::nullopt, true),
               testdata::MakeListing("l-null", ba::Category::Lamp, 500),
               testdata::MakeListing("l-4000", ba::Category::Lamp, 1200, "taganrog",
                                     std::nullopt, std::nullopt, 4000),
               testdata::MakeListing("a-c", ba::Category::Chair, 100, "azov",
                                     std::nullopt, true)}),
          Categories(testdata::Categories()),
          Search(CatalogData, Categories),
          Cities(testdata::Cities()),
          Decision(Search, Cities, 3) {}

    ba::Catalog CatalogData;
    ba::CategoryDirectory Categories;
    ba::BundleSearch Search;
    ba::CityDirectory Cities;
    ba::DecisionEngine Decision;
};

ba::Requirements BaseRequirements() {
    auto requirements = testdata::FullRequirements();
    requirements.BudgetRub = 10000;
    ba::NumericConstraint width;
    width.Max = ba::NumericBound{100, true};
    requirements.TableWidth = width;
    return requirements;
}

const ba::OkPayload* AsOk(const ba::Response& response) {
    return std::get_if<ba::OkPayload>(&response.GetPayload());
}

const ba::ClarifyPayload* AsClarify(const ba::Response& response) {
    return std::get_if<ba::ClarifyPayload>(&response.GetPayload());
}

const ba::NoMatchPayload* AsNoMatch(const ba::Response& response) {
    return std::get_if<ba::NoMatchPayload>(&response.GetPayload());
}

} // namespace

TEST_CASE(DecisionAsksForAllMissingCoreConditions) {
    Fixture fixture;
    const auto response = fixture.Decision.Decide(ba::Requirements{});
    const auto* clarify = AsClarify(response);
    CHECK(clarify != nullptr);
    CHECK_EQ(std::size_t(3), clarify->Questions.size());
    CHECK_EQ(std::string("city_id"), clarify->Questions[0].Field);
    CHECK_EQ(std::string("categories"), clarify->Questions[1].Field);
    CHECK_EQ(std::string("budget_rub"), clarify->Questions[2].Field);
    CHECK(!clarify->Questions[0].Text.empty());
}

TEST_CASE(DecisionAsksOnlyForBudgetWhenEverythingElseIsKnown) {
    Fixture fixture;
    auto requirements = BaseRequirements();
    requirements.BudgetRub.reset();
    const auto response = fixture.Decision.Decide(requirements);
    const auto* clarify = AsClarify(response);
    CHECK(clarify != nullptr);
    CHECK_EQ(std::size_t(1), clarify->Questions.size());
    CHECK_EQ(std::string("budget_rub"), clarify->Questions[0].Field);
}

TEST_CASE(DecisionExplainsInvalidCityAndCategories) {
    Fixture fixture;
    auto requirements = BaseRequirements();
    requirements.CityId.reset();
    requirements.CityInvalid = true;
    requirements.Categories.clear();
    requirements.CategoriesInvalid = true;
    const auto response = fixture.Decision.Decide(requirements);
    const auto* clarify = AsClarify(response);
    CHECK(clarify != nullptr);
    CHECK_EQ(std::size_t(2), clarify->Questions.size());
    CHECK_EQ(std::string("city_id"), clarify->Questions[0].Field);
    CHECK(clarify->Questions[0].Text.find("Таганрог") != std::string::npos);
    CHECK_EQ(std::string("categories"), clarify->Questions[1].Field);
    CHECK(clarify->Questions[1].Text.find("только стол, кресло и лампу") !=
          std::string::npos);
}

TEST_CASE(DecisionAsksForUnambiguousBudget) {
    Fixture fixture;
    auto requirements = BaseRequirements();
    requirements.BudgetRub.reset();
    requirements.BudgetAmbiguous = true;
    const auto response = fixture.Decision.Decide(requirements);
    const auto* clarify = AsClarify(response);
    CHECK(clarify != nullptr);
    CHECK_EQ(std::size_t(1), clarify->Questions.size());
    CHECK_EQ(std::string("budget_rub"), clarify->Questions[0].Field);
    CHECK(clarify->Questions[0].Text.find("одной суммой") != std::string::npos);
}

TEST_CASE(DecisionProducesValidOptions) {
    Fixture fixture;
    const auto response = fixture.Decision.Decide(BaseRequirements());
    const auto* ok = AsOk(response);
    CHECK(ok != nullptr);
    CHECK(!ok->Options.empty());
    CHECK(ok->Options.size() <= 3);

    std::int64_t previous = 0;
    for (const auto& option : ok->Options) {
        CHECK_EQ(std::size_t(3), option.ListingIds.size());
        CHECK(option.TotalRub >= previous);
        previous = option.TotalRub;
        CHECK(option.TotalRub <= 10000);
        CHECK(option.Explanation.find("Комплект:") == 0);
        CHECK(option.Explanation.find("Итого") != std::string::npos);
        CHECK(option.Explanation.find("СЕКРЕТНЫЙ") == std::string::npos);
        CHECK(option.Explanation.find("Не показывать") == std::string::npos);
    }

    CHECK_EQ(std::string("t-80"), ok->Options[0].ListingIds[0]);
    CHECK_EQ(std::string("c-null"), ok->Options[0].ListingIds[1]);
    CHECK_EQ(std::string("l-null"), ok->Options[0].ListingIds[2]);
    CHECK_EQ(std::int64_t(5000), ok->Options[0].TotalRub);
    CHECK(ok->Options[0].Explanation.find("стол шириной 80 см") != std::string::npos);
}

TEST_CASE(DecisionHonoursOptionLimit) {
    Fixture fixture;
    ba::DecisionEngine single(fixture.Search, testdata::Cities(), 1);
    const auto response = single.Decide(BaseRequirements());
    const auto* ok = AsOk(response);
    CHECK(ok != nullptr);
    CHECK_EQ(std::size_t(1), ok->Options.size());
}

TEST_CASE(DecisionReportsMissingCategory) {
    Fixture fixture;
    auto requirements = BaseRequirements();
    requirements.CityId = "azov";
    requirements.Categories = {ba::Category::Table, ba::Category::Chair};
    const auto response = fixture.Decision.Decide(requirements);
    const auto* noMatch = AsNoMatch(response);
    CHECK(noMatch != nullptr);
    CHECK(noMatch->Message.find("Азов") != std::string::npos);
    CHECK(noMatch->Message.find("нет столов") != std::string::npos);
}

TEST_CASE(DecisionReportsBudgetGap) {
    Fixture fixture;
    auto requirements = BaseRequirements();
    requirements.BudgetRub = 100;
    const auto response = fixture.Decision.Decide(requirements);
    const auto* noMatch = AsNoMatch(response);
    CHECK(noMatch != nullptr);
    CHECK(noMatch->Message.find("5 000 ₽") != std::string::npos);
    CHECK(noMatch->Message.find("100 ₽") != std::string::npos);
}

TEST_CASE(DecisionReportsBlockedAttribute) {
    Fixture fixture;
    auto requirements = BaseRequirements();
    requirements.Categories = {ba::Category::Table};
    ba::NumericConstraint impossible;
    impossible.Max = ba::NumericBound{10, true};
    requirements.TableWidth = impossible;
    const auto response = fixture.Decision.Decide(requirements);
    const auto* noMatch = AsNoMatch(response);
    CHECK(noMatch != nullptr);
    CHECK(noMatch->Message.find("ширина стола") != std::string::npos);
    CHECK(noMatch->Message.find("10 см") != std::string::npos);
}

TEST_CASE(ResponseSerializesExactShapes) {
    auto ok = ba::Response::MakeOk({ba::BundleOption{{"a", "b", "c"}, 1000, "Комплект: тест."}});
    auto json = ok.ToJson();
    CHECK_EQ(std::size_t(2), json.size());
    CHECK_EQ(std::string("ok"), json["status"].get<std::string>());
    CHECK_EQ(std::size_t(1), json["options"].size());
    CHECK_EQ(std::size_t(3), json["options"][0].size());
    CHECK_EQ(std::int64_t(1000), json["options"][0]["total_rub"].get<std::int64_t>());

    auto clarify = ba::Response::MakeClarify({{"budget_rub", "Какой бюджет?"}});
    json = clarify.ToJson();
    CHECK_EQ(std::size_t(2), json.size());
    CHECK_EQ(std::string("clarify"), json["status"].get<std::string>());
    CHECK_EQ(std::size_t(1), json["questions"].size());
    CHECK_EQ(std::size_t(2), json["questions"][0].size());
    CHECK_EQ(std::string("budget_rub"), json["questions"][0]["field"].get<std::string>());

    auto noMatch = ba::Response::MakeNoMatch("Ничего не найдено.");
    json = noMatch.ToJson();
    CHECK_EQ(std::size_t(2), json.size());
    CHECK_EQ(std::string("no_match"), json["status"].get<std::string>());
    CHECK_EQ(std::string("Ничего не найдено."), json["message"].get<std::string>());
}

TEST_CASE(ResponseRejectsEmptyPayloads) {
    CHECK_THROWS_AS(ba::Response::MakeOk({}), std::logic_error);
    CHECK_THROWS_AS(ba::Response::MakeClarify({}), std::logic_error);
    CHECK_THROWS_AS(ba::Response::MakeNoMatch(""), std::logic_error);

    std::vector<ba::BundleOption> four(4);
    CHECK_THROWS_AS(ba::Response::MakeOk(four), std::logic_error);
}
