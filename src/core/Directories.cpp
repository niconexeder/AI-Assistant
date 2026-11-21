#include "ba/core/Directories.hpp"

#include <set>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "ba/utils/Errors.hpp"
#include "ba/utils/JsonIO.hpp"

namespace ba {

namespace {

void RequireExactKeys(const nlohmann::json& object,
                      const std::set<std::string>& allowed,
                      const std::string& context) {
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (allowed.count(it.key()) == 0) {
            throw AppError(ErrorKind::Configuration,
                           "Неизвестное поле '" + it.key() + "' в " + context);
        }
    }
}

std::string RequireString(const nlohmann::json& object, const char* key,
                          const std::string& context, bool allowEmpty = false) {
    if (!object.contains(key) || !object[key].is_string()) {
        throw AppError(ErrorKind::Configuration,
                       "Поле '" + std::string(key) + "' должно быть строкой в " + context);
    }
    auto value = object[key].get<std::string>();
    if (!allowEmpty && value.empty()) {
        throw AppError(ErrorKind::Configuration,
                       "Поле '" + std::string(key) + "' не может быть пустым в " + context);
    }
    return value;
}

} // namespace

CityDirectory LoadCityDirectory(const std::string& path) {
    nlohmann::json root;
    try {
        root = jsonio::ParseJson(jsonio::ReadFile(path, 4U * 1024U * 1024U),
                                 "справочнике городов");
    } catch (const AppError& error) {
        throw AppError(ErrorKind::Configuration, error.what());
    }
    if (!root.is_array() || root.empty()) {
        throw AppError(ErrorKind::Configuration,
                       "Справочник городов должен быть непустым массивом: " + path);
    }

    std::vector<City> cities;
    std::set<std::string> ids;
    std::set<std::string> names;
    for (const auto& item : root) {
        if (!item.is_object()) {
            throw AppError(ErrorKind::Configuration,
                           "Элемент справочника городов должен быть объектом: " + path);
        }
        RequireExactKeys(item, {"id", "name"}, "справочнике городов");
        City city{RequireString(item, "id", "справочнике городов"),
                  RequireString(item, "name", "справочнике городов")};
        if (!ids.insert(city.Id).second) {
            throw AppError(ErrorKind::Configuration,
                           "Повторяющийся id города: " + city.Id);
        }
        if (!names.insert(city.Name).second) {
            throw AppError(ErrorKind::Configuration,
                           "Повторяющееся название города: " + city.Name);
        }
        cities.push_back(std::move(city));
    }
    return CityDirectory(std::move(cities));
}

CategoryDirectory LoadCategoryDirectory(const std::string& path) {
    nlohmann::json root;
    try {
        root = jsonio::ParseJson(jsonio::ReadFile(path, 4U * 1024U * 1024U),
                                 "справочнике категорий");
    } catch (const AppError& error) {
        throw AppError(ErrorKind::Configuration, error.what());
    }
    if (!root.is_array() || root.empty()) {
        throw AppError(ErrorKind::Configuration,
                       "Справочник категорий должен быть непустым массивом: " + path);
    }

    std::vector<CategoryInfo> categories;
    std::set<int> seen;
    for (const auto& item : root) {
        if (!item.is_object()) {
            throw AppError(ErrorKind::Configuration,
                           "Элемент справочника категорий должен быть объектом: " + path);
        }
        RequireExactKeys(item, {"id", "name", "attributes"}, "справочнике категорий");
        const auto id = RequireString(item, "id", "справочнике категорий");
        const auto name = RequireString(item, "name", "справочнике категорий");
        if (!item.contains("attributes") || !item["attributes"].is_object()) {
            throw AppError(ErrorKind::Configuration,
                           "Поле 'attributes' должно быть объектом в справочнике категорий");
        }
        const auto category = CategoryFromId(id);
        if (!category.has_value()) {
            throw AppError(ErrorKind::Configuration,
                           "Неподдерживаемая категория в справочнике: " + id);
        }
        if (!seen.insert(static_cast<int>(*category)).second) {
            throw AppError(ErrorKind::Configuration,
                           "Повторяющаяся категория в справочнике: " + id);
        }
        categories.push_back(CategoryInfo{*category, name});
    }

    if (categories.size() != AllCategories.size()) {
        throw AppError(ErrorKind::Configuration,
                       "Справочник должен содержать table, chair и lamp: " + path);
    }
    return CategoryDirectory(std::move(categories));
}

} // namespace ba
