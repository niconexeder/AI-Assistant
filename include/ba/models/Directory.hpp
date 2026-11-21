#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ba/models/Category.hpp"

namespace ba {

struct City {
    std::string Id;
    std::string Name;
};

class CityDirectory {
public:
    explicit CityDirectory(std::vector<City> cities);

    const std::vector<City>& All() const noexcept { return Cities_; }
    bool Contains(std::string_view id) const;
    const City* FindById(std::string_view id) const;
    const City* FindByName(std::string_view name) const;

private:
    std::vector<City> Cities_;
};

struct CategoryInfo {
    Category Id = Category::Table;
    std::string Name;
};

class CategoryDirectory {
public:
    explicit CategoryDirectory(std::vector<CategoryInfo> categories);

    const std::vector<CategoryInfo>& All() const noexcept { return Categories_; }
    const CategoryInfo* Find(Category category) const;
    std::string NameOf(Category category) const;

private:
    std::vector<CategoryInfo> Categories_;
};

} // namespace ba
