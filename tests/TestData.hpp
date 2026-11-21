#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ba/models/Category.hpp"
#include "ba/models/Directory.hpp"
#include "ba/models/Listing.hpp"
#include "ba/models/Message.hpp"
#include "ba/models/Requirements.hpp"

namespace testdata {

inline ba::CityDirectory Cities() {
    return ba::CityDirectory({ba::City{"taganrog", "Таганрог"},
                              ba::City{"rostov-na-donu", "Ростов-на-Дону"},
                              ba::City{"azov", "Азов"}});
}

inline ba::CategoryDirectory Categories() {
    return ba::CategoryDirectory({ba::CategoryInfo{ba::Category::Table, "Стол"},
                                  ba::CategoryInfo{ba::Category::Chair, "Кресло"},
                                  ba::CategoryInfo{ba::Category::Lamp, "Лампа"}});
}

inline ba::Listing MakeListing(const std::string& id,
                               ba::Category category,
                               std::int64_t price,
                               const std::string& cityId = "taganrog",
                               std::optional<std::int64_t> width = std::nullopt,
                               std::optional<bool> adjustable = std::nullopt,
                               std::optional<std::int64_t> temperature = std::nullopt,
                               const std::string& title = "Товар",
                               const std::string& description = "Описание") {
    ba::Listing listing;
    listing.Id = id;
    listing.CategoryValue = category;
    listing.PriceRub = price;
    listing.CityId = cityId;
    listing.WidthCm = width;
    listing.AdjustableHeight = adjustable;
    listing.ColorTemperatureK = temperature;
    listing.Title = title;
    listing.Description = description;
    return listing;
}

inline nlohmann::json ListingJson(const std::string& id,
                                  const std::string& category,
                                  std::int64_t price,
                                  const std::string& cityId,
                                  const nlohmann::json& attributes,
                                  const std::string& title = "Товар",
                                  const std::string& description = "Описание") {
    return nlohmann::json{{"id", id},
                          {"category", category},
                          {"title", title},
                          {"description", description},
                          {"price_rub", price},
                          {"city_id", cityId},
                          {"attributes",
                           attributes.is_null() ? nlohmann::json::object() : attributes}};
}

inline nlohmann::json UserRequest(const std::string& text) {
    return nlohmann::json{
        {"messages", nlohmann::json::array({nlohmann::json{{"role", "user"}, {"text", text}}})}};
}

inline nlohmann::json Dialogue(
    const std::vector<std::pair<ba::Role, std::string>>& turns) {
    nlohmann::json messages = nlohmann::json::array();
    for (const auto& turn : turns) {
        messages.push_back(nlohmann::json{
            {"role", turn.first == ba::Role::User ? "user" : "assistant"},
            {"text", turn.second}});
    }
    return nlohmann::json{{"messages", messages}};
}

class TempFile {
public:
    TempFile(const std::string& prefix, const std::string& content) {
        static std::atomic<unsigned long long> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto name = prefix + "-" + std::to_string(stamp) + "-" +
                          std::to_string(counter.fetch_add(1));
        Path_ = std::filesystem::temp_directory_path() / name;
        std::ofstream output(Path_, std::ios::binary);
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    ~TempFile() {
        std::error_code error;
        std::filesystem::remove(Path_, error);
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    const std::filesystem::path& Path() const noexcept { return Path_; }
    std::string String() const { return Path_.string(); }

private:
    std::filesystem::path Path_;
};

inline TempFile MakeJsonFile(const std::string& prefix, const nlohmann::json& value) {
    return TempFile(prefix + ".json", value.dump(2));
}

class TempDir {
public:
    explicit TempDir(const std::string& prefix) {
        static std::atomic<unsigned long long> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        Path_ = std::filesystem::temp_directory_path() /
                (prefix + "-" + std::to_string(stamp) + "-" +
                 std::to_string(counter.fetch_add(1)));
        std::filesystem::create_directories(Path_);
    }

    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(Path_, error);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& Path() const noexcept { return Path_; }
    std::string String() const { return Path_.string(); }

    std::string Write(const std::string& name, const std::string& content) const {
        const auto path = Path_ / name;
        std::ofstream output(path, std::ios::binary);
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        return path.string();
    }

    std::string WriteJson(const std::string& name, const nlohmann::json& value) const {
        return Write(name, value.dump(2));
    }

private:
    std::filesystem::path Path_;
};

inline ba::Requirements FullRequirements() {
    ba::Requirements requirements;
    requirements.CityId = "taganrog";
    requirements.BudgetRub = 100000;
    requirements.Categories = {ba::Category::Table, ba::Category::Chair, ba::Category::Lamp};
    return requirements;
}

} // namespace testdata
