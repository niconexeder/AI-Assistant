#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ba/models/Category.hpp"

namespace ba {

struct NumericBound {
    std::int64_t Value = 0;
    bool Inclusive = true;
};

struct NumericConstraint {
    std::optional<NumericBound> Min;
    std::optional<NumericBound> Max;

    bool Matches(std::int64_t value) const;
    bool IsEmpty() const noexcept { return !Min.has_value() && !Max.has_value(); }
    std::string Describe() const;
};

struct Requirements {
    std::optional<std::string> CityId;
    std::optional<std::int64_t> BudgetRub;
    std::vector<Category> Categories;
    std::optional<NumericConstraint> TableWidth;
    std::optional<bool> ChairAdjustableHeight;
    std::optional<NumericConstraint> LampColorTemperature;

    bool CityInvalid = false;
    bool CategoriesInvalid = false;
    bool BudgetAmbiguous = false;
    std::vector<std::string> UnsupportedCategories;

    bool HasCoreConditions() const;
    bool NeedsClarification() const;
    bool HasCategory(Category category) const;
    void AddCategory(Category category);
    void RemoveCategory(Category category);
};

} // namespace ba
