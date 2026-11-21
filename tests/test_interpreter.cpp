#include "TestHarness.hpp"

#include <optional>
#include <string>

#include "TestData.hpp"
#include "ba/core/MessageInterpreter.hpp"

namespace {

struct Fixture {
    ba::CityDirectory Cities = testdata::Cities();
    ba::CategoryDirectory Categories = testdata::Categories();
    ba::MessageInterpreter Interpreter{Cities, Categories};
};

} // namespace

TEST_CASE(InterpreterRecognizesCityInflections) {
    Fixture fixture;
    CHECK_EQ(std::string("taganrog"), *fixture.Interpreter.Interpret("Ищу в Таганроге").CityId);
    CHECK_EQ(std::string("rostov-na-donu"),
             *fixture.Interpreter.Interpret("Ростов-на-Дону, центр").CityId);
    CHECK_EQ(std::string("rostov-na-donu"),
             *fixture.Interpreter.Interpret("в Ростове-на-Дону").CityId);
    CHECK_EQ(std::string("azov"), *fixture.Interpreter.Interpret("город Азов").CityId);
}

TEST_CASE(InterpreterRejectsUnknownOrAmbiguousCity) {
    Fixture fixture;
    const auto moscow = fixture.Interpreter.Interpret("Нужен стол в Москве");
    CHECK(moscow.CityInvalid);
    CHECK(!moscow.CityId.has_value());

    const auto ambiguous = fixture.Interpreter.Interpret("Таганрог или Азов");
    CHECK(ambiguous.CityInvalid);
    CHECK(!ambiguous.CityId.has_value());
}

TEST_CASE(InterpreterReadsCategoriesAndNegation) {
    Fixture fixture;
    auto facts = fixture.Interpreter.Interpret("Нужны стол, кресло и лампа");
    CHECK_EQ(std::size_t(3), facts.MentionedCategories.size());

    facts = fixture.Interpreter.Interpret("Стол не нужен, нужна лампа");
    CHECK_EQ(std::size_t(1), facts.MentionedCategories.size());
    CHECK_EQ(ba::Category::Lamp, facts.MentionedCategories[0]);
    CHECK_EQ(std::size_t(1), facts.ExcludedCategories.size());
    CHECK_EQ(ba::Category::Table, facts.ExcludedCategories[0]);

    facts = fixture.Interpreter.Interpret("Без стола, нужно кресло");
    CHECK_EQ(std::size_t(1), facts.MentionedCategories.size());
    CHECK_EQ(ba::Category::Chair, facts.MentionedCategories[0]);
    CHECK_EQ(std::size_t(1), facts.ExcludedCategories.size());
    CHECK_EQ(ba::Category::Table, facts.ExcludedCategories[0]);
}

TEST_CASE(InterpreterRejectsUnsupportedCategories) {
    Fixture fixture;
    const auto facts = fixture.Interpreter.Interpret("Нужен диван и шкаф");
    CHECK(facts.CategoriesInvalid);
    CHECK(facts.MentionedCategories.empty());
    CHECK_EQ(std::size_t(2), facts.UnsupportedCategories.size());
}

TEST_CASE(InterpreterParsesBudgets) {
    Fixture fixture;
    CHECK_EQ(std::int64_t(15000),
             *fixture.Interpreter.Interpret("До 15 000 ₽ за всё").BudgetRub);
    CHECK_EQ(std::int64_t(15000),
             *fixture.Interpreter.Interpret("Бюджет 15 тыс рублей").BudgetRub);
    CHECK_EQ(std::int64_t(15000),
             *fixture.Interpreter.Interpret("Пятнадцать тысяч рублей").BudgetRub);
    CHECK_EQ(std::int64_t(20000),
             *fixture.Interpreter.Interpret("Бюджет 20к").BudgetRub);
    CHECK_EQ(std::int64_t(15000),
             *fixture.Interpreter.Interpret("Рассчитываю на 10-15 тысяч").BudgetRub);
    CHECK_EQ(std::int64_t(15000),
             *fixture.Interpreter.Interpret("15000").BudgetRub);
}

TEST_CASE(InterpreterMarksAmbiguousBudget) {
    Fixture fixture;
    const auto facts = fixture.Interpreter.Interpret("Бюджет 10000 или 15000 рублей");
    CHECK(facts.BudgetAmbiguous);
    CHECK(!facts.BudgetRub.has_value());
}

TEST_CASE(InterpreterParsesTableWidth) {
    Fixture fixture;
    auto facts = fixture.Interpreter.Interpret("Стол не шире 80 см");
    CHECK(facts.TableWidth.has_value());
    CHECK_EQ(std::int64_t(80), facts.TableWidth->Max->Value);
    CHECK(facts.TableWidth->Max->Inclusive);
    CHECK(!facts.TableWidth->Min.has_value());

    facts = fixture.Interpreter.Interpret("Стол шире 80 см");
    CHECK(facts.TableWidth.has_value());
    CHECK_EQ(std::int64_t(80), facts.TableWidth->Min->Value);
    CHECK(!facts.TableWidth->Min->Inclusive);

    facts = fixture.Interpreter.Interpret("Стол уже 90 см");
    CHECK(facts.TableWidth.has_value());
    CHECK_EQ(std::int64_t(90), facts.TableWidth->Max->Value);
    CHECK(!facts.TableWidth->Max->Inclusive);

    facts = fixture.Interpreter.Interpret("Стол шириной от 70 до 90 см");
    CHECK(facts.TableWidth.has_value());
    CHECK_EQ(std::int64_t(70), facts.TableWidth->Min->Value);
    CHECK_EQ(std::int64_t(90), facts.TableWidth->Max->Value);
}

TEST_CASE(InterpreterParsesMetreWidthAndMissingAttributeRule) {
    Fixture fixture;
    const auto facts =
        fixture.Interpreter.Interpret("Стол не шире метра, кресло и лампа. Таганрог. "
                                      "До 15 000 ₽ за всё.");
    CHECK(facts.TableWidth.has_value());
    CHECK_EQ(std::int64_t(100), facts.TableWidth->Max->Value);
    CHECK(facts.TableWidth->Max->Inclusive);
    CHECK_EQ(std::int64_t(15000), *facts.BudgetRub);
    CHECK_EQ(std::string("taganrog"), *facts.CityId);
    CHECK_EQ(std::size_t(3), facts.MentionedCategories.size());
}

TEST_CASE(InterpreterDoesNotTreatMoneyAsWidth) {
    Fixture fixture;
    const auto facts = fixture.Interpreter.Interpret("Стол до 5000 рублей");
    CHECK(facts.TableWidth.has_value() == false);
    CHECK_EQ(std::int64_t(5000), *facts.BudgetRub);
}

TEST_CASE(InterpreterParsesLampTemperature) {
    Fixture fixture;
    auto facts = fixture.Interpreter.Interpret("Лампа 4000 К");
    CHECK(facts.LampColorTemperature.has_value());
    CHECK_EQ(std::int64_t(4000), facts.LampColorTemperature->Min->Value);
    CHECK_EQ(std::int64_t(4000), facts.LampColorTemperature->Max->Value);

    facts = fixture.Interpreter.Interpret("Температура света от 3000 до 4000 кельвин");
    CHECK(facts.LampColorTemperature.has_value());
    CHECK_EQ(std::int64_t(3000), facts.LampColorTemperature->Min->Value);
    CHECK_EQ(std::int64_t(4000), facts.LampColorTemperature->Max->Value);
}

TEST_CASE(InterpreterParsesChairAdjustability) {
    Fixture fixture;
    CHECK(fixture.Interpreter.Interpret("Кресло с регулировкой высоты").ChairAdjustableHeight ==
          std::optional<bool>(true));
    CHECK(fixture.Interpreter.Interpret("Кресло без регулировки высоты").ChairAdjustableHeight ==
          std::optional<bool>(false));
    CHECK(fixture.Interpreter.Interpret("Кресло, высота не регулируется")
              .ChairAdjustableHeight == std::optional<bool>(false));
}

TEST_CASE(InterpreterResetsAttributeConstraints) {
    Fixture fixture;
    auto facts = fixture.Interpreter.Interpret("Ширина стола не важна");
    CHECK(facts.ResetTableWidth);
    CHECK(!facts.TableWidth.has_value());

    facts = fixture.Interpreter.Interpret("Цветовая температура не принципиальна");
    CHECK(facts.ResetLampColorTemperature);

    facts = fixture.Interpreter.Interpret("Регулировка высоты не важна");
    CHECK(facts.ResetChairAdjustableHeight);
}

TEST_CASE(InterpreterSeparatesQuantityFromBudget) {
    Fixture fixture;
    const auto facts = fixture.Interpreter.Interpret(
        "Стол 100 см, кресло 2 штуки, лампа 4000 К, бюджет 20000 рублей");
    CHECK_EQ(std::int64_t(20000), *facts.BudgetRub);
    CHECK(facts.TableWidth.has_value());
    CHECK_EQ(std::int64_t(100), facts.TableWidth->Min->Value);
    CHECK(facts.LampColorTemperature.has_value());
    CHECK_EQ(std::int64_t(4000), facts.LampColorTemperature->Min->Value);
}
