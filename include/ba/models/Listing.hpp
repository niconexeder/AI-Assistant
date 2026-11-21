#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

#include "ba/models/Category.hpp"

namespace ba {

using AttributeValue = std::variant<std::string, std::int64_t, double, bool>;

struct Listing {
    std::string Id;
    Category CategoryValue = Category::Table;
    std::string Title;
    std::string Description;
    std::int64_t PriceRub = 0;
    std::string CityId;

    std::optional<std::int64_t> WidthCm;
    std::optional<bool> AdjustableHeight;
    std::optional<std::int64_t> ColorTemperatureK;

    std::unordered_map<std::string, AttributeValue> ExtraAttributes;
};

} // namespace ba
