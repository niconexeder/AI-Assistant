#include "ba/core/Catalog.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "ba/utils/Errors.hpp"
#include "ba/utils/JsonIO.hpp"

namespace ba {

namespace {

constexpr std::size_t MaxCatalogListings = 250;
constexpr std::size_t MaxCatalogBytes = 16U * 1024U * 1024U;

void RequireExactKeys(const nlohmann::json& object,
                      const std::set<std::string>& allowed,
                      const std::string& context) {
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (allowed.count(it.key()) == 0) {
            throw AppError(ErrorKind::Catalog,
                           "Неизвестное поле '" + it.key() + "' в " + context);
        }
    }
}

const nlohmann::json& RequireField(const nlohmann::json& object, const char* key,
                                   const std::string& context) {
    if (!object.contains(key)) {
        throw AppError(ErrorKind::Catalog,
                       "Отсутствует поле '" + std::string(key) + "' в " + context);
    }
    return object[key];
}

std::string RequireString(const nlohmann::json& object, const char* key,
                          const std::string& context, bool allowEmpty = false) {
    const auto& value = RequireField(object, key, context);
    if (!value.is_string()) {
        throw AppError(ErrorKind::Catalog,
                       "Поле '" + std::string(key) + "' должно быть строкой в " + context);
    }
    auto result = value.get<std::string>();
    if (!allowEmpty && result.empty()) {
        throw AppError(ErrorKind::Catalog,
                       "Поле '" + std::string(key) + "' не может быть пустым в " + context);
    }
    return result;
}

std::int64_t RequirePositiveInteger(const nlohmann::json& object, const char* key,
                                    const std::string& context) {
    const auto& value = RequireField(object, key, context);
    if (!value.is_number_integer() && !value.is_number_unsigned()) {
        throw AppError(ErrorKind::Catalog,
                       "Поле '" + std::string(key) + "' должно быть целым числом в " +
                           context);
    }
    if (value.is_number_unsigned()) {
        const auto unsignedValue = value.get<std::uint64_t>();
        if (unsignedValue > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            throw AppError(ErrorKind::Catalog,
                           "Поле '" + std::string(key) + "' выходит за допустимый диапазон в " +
                               context);
        }
    }
    const auto result = value.get<std::int64_t>();
    if (result <= 0) {
        throw AppError(ErrorKind::Catalog,
                       "Поле '" + std::string(key) + "' должно быть больше нуля в " + context);
    }
    return result;
}

AttributeValue ParseExtraAttribute(const nlohmann::json& value, const std::string& key,
                                   const std::string& listingId) {
    const std::string context = "характеристике '" + key + "' объявления " + listingId;
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_boolean()) {
        return value.get<bool>();
    }
    if (value.is_number_integer() || value.is_number_unsigned()) {
        if (value.is_number_unsigned() &&
            value.get<std::uint64_t>() >
                static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            throw AppError(ErrorKind::Catalog, "Число вне диапазона в " + context);
        }
        return value.get<std::int64_t>();
    }
    if (value.is_number_float()) {
        const double number = value.get<double>();
        if (!std::isfinite(number)) {
            throw AppError(ErrorKind::Catalog, "Некорректное число в " + context);
        }
        return number;
    }
    throw AppError(ErrorKind::Catalog,
                   "Значение в " + context + " должно быть строкой, числом или boolean");
}

Listing ParseListing(const nlohmann::json& item, const CityDirectory& cities) {
    if (!item.is_object()) {
        throw AppError(ErrorKind::Catalog, "Элемент каталога должен быть объектом");
    }
    RequireExactKeys(item,
                     {"id", "category", "title", "description", "price_rub", "city_id",
                      "attributes"},
                     "объявлении каталога");

    const auto id = RequireString(item, "id", "объявлении каталога");
    const std::string context = "объявлении " + id;
    const auto categoryId = RequireString(item, "category", context);
    const auto category = CategoryFromId(categoryId);
    if (!category.has_value()) {
        throw AppError(ErrorKind::Catalog,
                       "Неподдерживаемая категория '" + categoryId + "' в " + context);
    }

    Listing listing;
    listing.Id = id;
    listing.CategoryValue = *category;
    listing.Title = RequireString(item, "title", context);
    listing.Description = RequireString(item, "description", context, true);
    listing.PriceRub = RequirePositiveInteger(item, "price_rub", context);
    listing.CityId = RequireString(item, "city_id", context);
    if (!cities.Contains(listing.CityId)) {
        throw AppError(ErrorKind::Catalog,
                       "Город '" + listing.CityId + "' отсутствует в справочнике (объявление " +
                           id + ")");
    }

    const auto& attributes = RequireField(item, "attributes", context);
    if (!attributes.is_object()) {
        throw AppError(ErrorKind::Catalog, "Поле 'attributes' должно быть объектом в " + context);
    }
    for (auto it = attributes.begin(); it != attributes.end(); ++it) {
        const auto& key = it.key();
        const auto& value = it.value();
        if (key == "width_cm") {
            if (!value.is_number_integer() && !value.is_number_unsigned()) {
                throw AppError(ErrorKind::Catalog,
                               "width_cm должно быть целым числом в " + context);
            }
            listing.WidthCm = RequirePositiveInteger(attributes, "width_cm", context);
        } else if (key == "adjustable_height") {
            if (!value.is_boolean()) {
                throw AppError(ErrorKind::Catalog,
                               "adjustable_height должно быть boolean в " + context);
            }
            listing.AdjustableHeight = value.get<bool>();
        } else if (key == "color_temperature_k") {
            if (!value.is_number_integer() && !value.is_number_unsigned()) {
                throw AppError(ErrorKind::Catalog,
                               "color_temperature_k должно быть целым числом в " + context);
            }
            listing.ColorTemperatureK =
                RequirePositiveInteger(attributes, "color_temperature_k", context);
        } else {
            listing.ExtraAttributes.emplace(key, ParseExtraAttribute(value, key, id));
        }
    }

    return listing;
}

} // namespace

Catalog::Catalog(std::vector<Listing> listings) : Listings_(std::move(listings)) {
    BuildIndex();
}

void Catalog::BuildIndex() {
    Index_.clear();
    Index_.reserve(Listings_.size());
    for (std::size_t i = 0; i < Listings_.size(); ++i) {
        if (!Index_.emplace(Listings_[i].Id, i).second) {
            throw AppError(ErrorKind::Catalog,
                           "Повторяющийся id объявления: " + Listings_[i].Id);
        }
    }
}

const Listing* Catalog::FindById(const std::string& id) const {
    const auto it = Index_.find(id);
    if (it == Index_.end()) {
        return nullptr;
    }
    return &Listings_[it->second];
}

std::vector<const Listing*> Catalog::InCity(const std::string& cityId) const {
    std::vector<const Listing*> result;
    for (const auto& listing : Listings_) {
        if (listing.CityId == cityId) {
            result.push_back(&listing);
        }
    }
    return result;
}

std::vector<const Listing*> Catalog::InCityAndCategory(const std::string& cityId,
                                                       Category category) const {
    std::vector<const Listing*> result;
    for (const auto& listing : Listings_) {
        if (listing.CityId == cityId && listing.CategoryValue == category) {
            result.push_back(&listing);
        }
    }
    return result;
}

Catalog LoadCatalog(const std::string& path, const CityDirectory& cities) {
    nlohmann::json root;
    try {
        root = jsonio::ParseJson(jsonio::ReadFile(path, MaxCatalogBytes), "каталоге " + path);
    } catch (const AppError& error) {
        throw AppError(ErrorKind::Catalog, error.what());
    }
    if (!root.is_array()) {
        throw AppError(ErrorKind::Catalog, "Каталог должен быть JSON-массивом: " + path);
    }
    if (root.size() > MaxCatalogListings) {
        throw AppError(ErrorKind::Catalog,
                       "Каталог содержит больше 250 объявлений: " + path);
    }

    std::vector<Listing> listings;
    listings.reserve(root.size());
    for (const auto& item : root) {
        listings.push_back(ParseListing(item, cities));
    }
    return Catalog(std::move(listings));
}

} // namespace ba
