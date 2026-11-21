#include "ba/models/Requirements.hpp"

#include <algorithm>
#include <sstream>

#include "ba/utils/RussianText.hpp"

namespace ba {

namespace {

std::string BoundText(const NumericBound& bound, bool isMin) {
    std::string result;
    if (isMin) {
        result = bound.Inclusive ? "не менее " : "больше ";
    } else {
        result = bound.Inclusive ? "не более " : "меньше ";
    }
    result += text::FormatInteger(bound.Value);
    return result;
}

} // namespace

bool NumericConstraint::Matches(std::int64_t value) const {
    if (Min.has_value()) {
        if (Min->Inclusive ? value < Min->Value : value <= Min->Value) {
            return false;
        }
    }
    if (Max.has_value()) {
        if (Max->Inclusive ? value > Max->Value : value >= Max->Value) {
            return false;
        }
    }
    return true;
}

std::string NumericConstraint::Describe() const {
    if (!Min.has_value() && !Max.has_value()) {
        return "без ограничений";
    }
    if (Min.has_value() && Max.has_value()) {
        if (Min->Value == Max->Value && Min->Inclusive && Max->Inclusive) {
            return "ровно " + text::FormatInteger(Min->Value);
        }
        return "от " + text::FormatInteger(Min->Value) + " до " +
               text::FormatInteger(Max->Value);
    }
    if (Min.has_value()) {
        return BoundText(*Min, true);
    }
    return BoundText(*Max, false);
}

bool Requirements::HasCoreConditions() const {
    return CityId.has_value() && BudgetRub.has_value() && !Categories.empty() &&
           !CityInvalid && !CategoriesInvalid && !BudgetAmbiguous;
}

bool Requirements::NeedsClarification() const {
    return !HasCoreConditions();
}

bool Requirements::HasCategory(Category category) const {
    return std::find(Categories.begin(), Categories.end(), category) != Categories.end();
}

void Requirements::AddCategory(Category category) {
    if (!HasCategory(category)) {
        Categories.push_back(category);
        std::sort(Categories.begin(), Categories.end(), [](Category left, Category right) {
            return static_cast<int>(left) < static_cast<int>(right);
        });
    }
}

void Requirements::RemoveCategory(Category category) {
    Categories.erase(std::remove(Categories.begin(), Categories.end(), category),
                     Categories.end());
}

} // namespace ba
