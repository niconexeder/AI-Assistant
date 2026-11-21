#include "TestHarness.hpp"

#include <optional>
#include <string>
#include <vector>

#include "TestData.hpp"
#include "ba/core/DialogueAnalyzer.hpp"

namespace {

struct Fixture {
    ba::CityDirectory Cities = testdata::Cities();
    ba::CategoryDirectory Categories = testdata::Categories();
    ba::DialogueAnalyzer Analyzer{Cities, Categories};
};

ba::Message User(std::string text) {
    return ba::Message{ba::Role::User, std::move(text)};
}

ba::Message Assistant(std::string text) {
    return ba::Message{ba::Role::Assistant, std::move(text)};
}

} // namespace

TEST_CASE(DialogueAccumulatesCoreConditions) {
    Fixture fixture;
    const auto requirements = fixture.Analyzer.Analyze(
        {User("Нужны стол и кресло в Таганроге"), Assistant("Какой бюджет?"),
         User("До 20 000 рублей")});

    CHECK_EQ(std::string("taganrog"), *requirements.CityId);
    CHECK_EQ(std::int64_t(20000), *requirements.BudgetRub);
    CHECK_EQ(std::size_t(2), requirements.Categories.size());
    CHECK(requirements.HasCategory(ba::Category::Table));
    CHECK(requirements.HasCategory(ba::Category::Chair));
    CHECK(requirements.HasCoreConditions());
    CHECK(!requirements.NeedsClarification());
}

TEST_CASE(DialogueLastBudgetWins) {
    Fixture fixture;
    auto requirements = fixture.Analyzer.Analyze(
        {User("Бюджет 10000 рублей"), Assistant("Понял"), User("Лучше 15000 рублей")});
    CHECK_EQ(std::int64_t(15000), *requirements.BudgetRub);
    CHECK(!requirements.BudgetAmbiguous);

    requirements = fixture.Analyzer.Analyze(
        {User("Бюджет 15000 рублей"), Assistant("Понял"),
         User("Бюджет 10000 или 12000 рублей")});
    CHECK(!requirements.BudgetRub.has_value());
    CHECK(requirements.BudgetAmbiguous);
}

TEST_CASE(DialogueClarifiedBudgetClearsAmbiguity) {
    Fixture fixture;
    const auto requirements = fixture.Analyzer.Analyze(
        {User("Нужен стол в Таганроге, бюджет 10000 или 15000"),
         Assistant("Уточните бюджет"), User("15000 рублей")});
    CHECK_EQ(std::int64_t(15000), *requirements.BudgetRub);
    CHECK(!requirements.BudgetAmbiguous);
}

TEST_CASE(DialogueExclusionRemovesCategory) {
    Fixture fixture;
    auto requirements = fixture.Analyzer.Analyze(
        {User("Нужны стол, кресло и лампа. Таганрог. До 15000 рублей"),
         Assistant("Хорошо"), User("Без стола")});
    CHECK_EQ(std::size_t(2), requirements.Categories.size());
    CHECK(!requirements.HasCategory(ba::Category::Table));
    CHECK(requirements.HasCategory(ba::Category::Chair));
    CHECK(requirements.HasCategory(ba::Category::Lamp));

    requirements = fixture.Analyzer.Analyze(
        {User("Стол не нужен, нужна лампа. Таганрог. До 15000 рублей")});
    CHECK_EQ(std::size_t(1), requirements.Categories.size());
    CHECK_EQ(ba::Category::Lamp, requirements.Categories.front());
}

TEST_CASE(DialogueAdditiveCategoryKeepsPrevious) {
    Fixture fixture;
    const auto requirements = fixture.Analyzer.Analyze(
        {User("Нужен стол. Таганрог. До 15000 рублей"), Assistant("Что ещё?"),
         User("Ещё кресло")});
    CHECK_EQ(std::size_t(2), requirements.Categories.size());
    CHECK(requirements.HasCategory(ba::Category::Table));
    CHECK(requirements.HasCategory(ba::Category::Chair));
}

TEST_CASE(DialogueRestartReplacesCategories) {
    Fixture fixture;
    auto requirements = fixture.Analyzer.Analyze(
        {User("Нужны стол и кресло. Таганрог. До 15000 рублей"),
         Assistant("Хорошо"), User("Лучше только лампа")});
    CHECK_EQ(std::size_t(1), requirements.Categories.size());
    CHECK_EQ(ba::Category::Lamp, requirements.Categories.front());

    requirements = fixture.Analyzer.Analyze(
        {User("Нужны стол и кресло. Таганрог. До 15000 рублей"),
         Assistant("Хорошо"), User("Передумал: нужна лампа")});
    CHECK_EQ(std::size_t(1), requirements.Categories.size());
    CHECK_EQ(ba::Category::Lamp, requirements.Categories.front());
}

TEST_CASE(DialogueAnswersCategoriesQuestionReplaces) {
    Fixture fixture;
    const auto requirements = fixture.Analyzer.Analyze(
        {User("Таганрог, до 15000 рублей"), Assistant("Какие товары нужны?"),
         User("Стол и лампа")});
    CHECK_EQ(std::size_t(2), requirements.Categories.size());
    CHECK(requirements.HasCategory(ba::Category::Table));
    CHECK(requirements.HasCategory(ba::Category::Lamp));
    CHECK(!requirements.HasCategory(ba::Category::Chair));
}

TEST_CASE(DialogueUnsupportedCategoryCanBeCorrected) {
    Fixture fixture;
    auto requirements = fixture.Analyzer.Analyze({User("Нужен диван")});
    CHECK(requirements.CategoriesInvalid);
    CHECK_EQ(std::size_t(1), requirements.UnsupportedCategories.size());
    CHECK(requirements.Categories.empty());

    requirements = fixture.Analyzer.Analyze(
        {User("Нужен диван"), Assistant("Я подбираю только стол, кресло и лампу"),
         User("Тогда только стол и кресло")});
    CHECK(!requirements.CategoriesInvalid);
    CHECK(requirements.UnsupportedCategories.empty());
    CHECK_EQ(std::size_t(2), requirements.Categories.size());
}

TEST_CASE(DialogueUnsupportedCategoryWithoutNouns) {
    Fixture fixture;
    const auto requirements = fixture.Analyzer.Analyze({User("Хочу шкаф и кровать")});
    CHECK(requirements.CategoriesInvalid);
    CHECK_EQ(std::size_t(2), requirements.UnsupportedCategories.size());
}

TEST_CASE(DialogueCityCanBeCorrected) {
    Fixture fixture;
    auto requirements = fixture.Analyzer.Analyze(
        {User("Нужен стол в Москве"), Assistant("Доступны Таганрог, Ростов-на-Дону, Азов"),
         User("Тогда Таганрог")});
    CHECK_EQ(std::string("taganrog"), *requirements.CityId);
    CHECK(!requirements.CityInvalid);

    requirements = fixture.Analyzer.Analyze(
        {User("Таганрог"), Assistant("Хорошо"), User("Нет, лучше Москва")});
    CHECK(!requirements.CityId.has_value());
    CHECK(requirements.CityInvalid);
}

TEST_CASE(DialogueKeepsInvalidCityUntilCorrected) {
    Fixture fixture;
    const auto requirements = fixture.Analyzer.Analyze(
        {User("Нужен стол в Москве"), Assistant("В каком городе искать?"),
         User("До 15000 рублей")});
    CHECK(!requirements.CityId.has_value());
    CHECK(requirements.CityInvalid);
}

TEST_CASE(DialogueCarriesAndOverridesAttributes) {
    Fixture fixture;
    auto requirements = fixture.Analyzer.Analyze(
        {User("Стол не шире 80 см, кресло с регулировкой высоты, лампа 4000 К. "
              "Таганрог. До 20000 рублей")});
    CHECK_EQ(std::int64_t(80), requirements.TableWidth->Max->Value);
    CHECK_EQ(std::optional<bool>(true), requirements.ChairAdjustableHeight);
    CHECK_EQ(std::int64_t(4000), requirements.LampColorTemperature->Min->Value);

    requirements = fixture.Analyzer.Analyze(
        {User("Стол не шире 80 см. Таганрог. До 20000 рублей"), Assistant("Хорошо"),
         User("Нет, стол до 100 см")});
    CHECK_EQ(std::int64_t(100), requirements.TableWidth->Max->Value);
}

TEST_CASE(DialogueResetsAttributes) {
    Fixture fixture;
    const auto requirements = fixture.Analyzer.Analyze(
        {User("Стол не шире 80 см, кресло с регулировкой высоты, лампа 4000 К. "
              "Таганрог. До 20000 рублей"),
         Assistant("Понял"), User("Ширина стола не важна, регулировка высоты не важна")});
    CHECK(!requirements.TableWidth.has_value());
    CHECK(!requirements.ChairAdjustableHeight.has_value());
    CHECK(requirements.LampColorTemperature.has_value());

    const auto reset = fixture.Analyzer.Analyze(
        {User("Лампа 4000 К. Таганрог. До 20000 рублей"), Assistant("Понял"),
         User("Цветовая температура не принципиальна")});
    CHECK(!reset.LampColorTemperature.has_value());
}

TEST_CASE(DialogueEmptyConversationNeedsEverything) {
    Fixture fixture;
    const auto requirements = fixture.Analyzer.Analyze({});
    CHECK(!requirements.CityId.has_value());
    CHECK(!requirements.BudgetRub.has_value());
    CHECK(requirements.Categories.empty());
    CHECK(requirements.NeedsClarification());
}

TEST_CASE(DialogueAssistantTextIsNotParsedAsUserFacts) {
    Fixture fixture;
    const auto requirements = fixture.Analyzer.Analyze(
        {User("Нужен стол"), Assistant("В Таганроге до 15000 рублей есть варианты")});
    CHECK(!requirements.CityId.has_value());
    CHECK(!requirements.BudgetRub.has_value());
}

TEST_CASE(DialogueNegationDoesNotLeakAcrossClauses) {
    Fixture fixture;
    auto requirements = fixture.Analyzer.Analyze(
        {User("Не нужен стол, нужно кресло. Таганрог. До 15000 рублей")});
    CHECK_EQ(std::size_t(1), requirements.Categories.size());
    CHECK_EQ(ba::Category::Chair, requirements.Categories.front());

    requirements = fixture.Analyzer.Analyze(
        {User("Нужен стол, не нужна лампа. Таганрог. До 15000 рублей")});
    CHECK_EQ(std::size_t(1), requirements.Categories.size());
    CHECK_EQ(ba::Category::Table, requirements.Categories.front());

    requirements = fixture.Analyzer.Analyze(
        {User("Стол, кресло и лампа не нужны. Таганрог. До 15000 рублей")});
    CHECK(requirements.Categories.empty());
}

TEST_CASE(DialogueReplacementAfterExclusion) {
    Fixture fixture;
    const auto requirements = fixture.Analyzer.Analyze(
        {User("Нужны стол и лампа. Таганрог. До 15000 рублей"),
         Assistant("Хорошо"), User("Без стола, нужна только лампа")});
    CHECK_EQ(std::size_t(1), requirements.Categories.size());
    CHECK_EQ(ba::Category::Lamp, requirements.Categories.front());
}
