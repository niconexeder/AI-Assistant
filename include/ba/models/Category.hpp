#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace ba {

enum class Category {
    Table,
    Chair,
    Lamp
};

inline constexpr std::array<Category, 3> AllCategories{
    Category::Table,
    Category::Chair,
    Category::Lamp
};

constexpr std::string_view CategoryId(Category category) {
    switch (category) {
    case Category::Table:
        return "table";
    case Category::Chair:
        return "chair";
    case Category::Lamp:
        return "lamp";
    }
    return "table";
}

constexpr std::string_view CategoryNameRu(Category category) {
    switch (category) {
    case Category::Table:
        return "Стол";
    case Category::Chair:
        return "Кресло";
    case Category::Lamp:
        return "Лампа";
    }
    return "Товар";
}

constexpr std::string_view CategoryNameRuLower(Category category) {
    switch (category) {
    case Category::Table:
        return "стол";
    case Category::Chair:
        return "кресло";
    case Category::Lamp:
        return "лампа";
    }
    return "товар";
}

constexpr std::string_view CategoryAttributeKey(Category category) {
    switch (category) {
    case Category::Table:
        return "width_cm";
    case Category::Chair:
        return "adjustable_height";
    case Category::Lamp:
        return "color_temperature_k";
    }
    return "width_cm";
}

inline std::optional<Category> CategoryFromId(std::string_view id) {
    if (id == "table") {
        return Category::Table;
    }
    if (id == "chair") {
        return Category::Chair;
    }
    if (id == "lamp") {
        return Category::Lamp;
    }
    return std::nullopt;
}

} // namespace ba
