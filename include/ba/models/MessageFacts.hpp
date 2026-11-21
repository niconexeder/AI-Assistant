#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ba/models/Category.hpp"
#include "ba/models/Requirements.hpp"

namespace ba {

struct MessageFacts {
    std::vector<Category> MentionedCategories;
    std::vector<Category> ExcludedCategories;
    bool CategoriesInvalid = false;
    bool CategoriesRestart = false;
    bool CategoriesAdditive = false;
    bool AnswersCategoriesQuestion = false;
    std::vector<std::string> UnsupportedCategories;

    std::optional<std::string> CityId;
    bool CityInvalid = false;

    std::optional<std::int64_t> BudgetRub;
    bool BudgetAmbiguous = false;
    bool BudgetMentioned = false;

    std::optional<NumericConstraint> TableWidth;
    std::optional<NumericConstraint> LampColorTemperature;
    std::optional<bool> ChairAdjustableHeight;

    bool ResetTableWidth = false;
    bool ResetLampColorTemperature = false;
    bool ResetChairAdjustableHeight = false;
};

} // namespace ba
