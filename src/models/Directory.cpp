#include "ba/models/Directory.hpp"

#include <algorithm>
#include <stdexcept>

namespace ba {

CityDirectory::CityDirectory(std::vector<City> cities) : Cities_(std::move(cities)) {
    std::sort(Cities_.begin(), Cities_.end(), [](const City& left, const City& right) {
        return left.Id < right.Id;
    });
}

bool CityDirectory::Contains(std::string_view id) const {
    return FindById(id) != nullptr;
}

const City* CityDirectory::FindById(std::string_view id) const {
    const auto it = std::lower_bound(
        Cities_.begin(), Cities_.end(), id,
        [](const City& city, std::string_view value) { return city.Id < value; });
    if (it == Cities_.end() || it->Id != id) {
        return nullptr;
    }
    return &*it;
}

const City* CityDirectory::FindByName(std::string_view name) const {
    const auto it = std::find_if(Cities_.begin(), Cities_.end(), [&](const City& city) {
        return city.Name == name;
    });
    return it == Cities_.end() ? nullptr : &*it;
}

CategoryDirectory::CategoryDirectory(std::vector<CategoryInfo> categories)
    : Categories_(std::move(categories)) {
    std::sort(Categories_.begin(), Categories_.end(), [](const CategoryInfo& left,
                                                        const CategoryInfo& right) {
        return static_cast<int>(left.Id) < static_cast<int>(right.Id);
    });
}

const CategoryInfo* CategoryDirectory::Find(Category category) const {
    const auto it = std::find_if(Categories_.begin(), Categories_.end(),
                                 [&](const CategoryInfo& info) { return info.Id == category; });
    return it == Categories_.end() ? nullptr : &*it;
}

std::string CategoryDirectory::NameOf(Category category) const {
    const auto* info = Find(category);
    return info != nullptr ? info->Name : std::string(CategoryNameRu(category));
}

} // namespace ba
