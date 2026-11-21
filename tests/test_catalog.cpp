#include "TestHarness.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "TestData.hpp"
#include "ba/core/Catalog.hpp"
#include "ba/core/Directories.hpp"
#include "ba/utils/Errors.hpp"

namespace {

std::string Dump(const nlohmann::json& value) {
    return value.dump(2);
}

} // namespace

TEST_CASE(CatalogIndexesListings) {
    ba::Catalog catalog({testdata::MakeListing("t1", ba::Category::Table, 5000),
                         testdata::MakeListing("c1", ba::Category::Chair, 3000),
                         testdata::MakeListing("l1", ba::Category::Lamp, 1000),
                         testdata::MakeListing("t2", ba::Category::Table, 4000, "azov")});

    CHECK_EQ(std::size_t(4), catalog.Size());
    CHECK(catalog.FindById("c1") != nullptr);
    CHECK_EQ(std::int64_t(3000), catalog.FindById("c1")->PriceRub);
    CHECK(catalog.FindById("missing") == nullptr);
    CHECK_EQ(std::size_t(3), catalog.InCity("taganrog").size());
    CHECK_EQ(std::size_t(1), catalog.InCityAndCategory("taganrog", ba::Category::Table).size());
    CHECK_EQ(std::size_t(1), catalog.InCityAndCategory("taganrog", ba::Category::Chair).size());
    CHECK_EQ(std::size_t(0), catalog.InCityAndCategory("azov", ba::Category::Lamp).size());
}

TEST_CASE(CatalogRejectsDuplicateIds) {
    CHECK_THROWS_AS(
        ba::Catalog({testdata::MakeListing("same", ba::Category::Table, 100),
                     testdata::MakeListing("same", ba::Category::Chair, 200)}),
        ba::AppError);
}

TEST_CASE(CatalogLoadsValidFile) {
    const auto json = nlohmann::json::array(
        {testdata::ListingJson("t1", "table", 5000, "taganrog", {{"width_cm", 90}}),
         testdata::ListingJson("c1", "chair", 3000, "taganrog",
                               {{"adjustable_height", true}}),
         testdata::ListingJson("l1", "lamp", 1000, "azov",
                               {{"color_temperature_k", 4000}, {"material", "metal"},
                                {"power_w", 12}, {"dimmable", false}, {"rating", 4.5}})});
    const testdata::TempFile file("catalog.json", Dump(json));

    const auto catalog = ba::LoadCatalog(file.String(), testdata::Cities());
    CHECK_EQ(std::size_t(3), catalog.Size());
    CHECK_EQ(std::int64_t(90), *catalog.FindById("t1")->WidthCm);
    CHECK_EQ(std::optional<bool>(true), catalog.FindById("c1")->AdjustableHeight);
    CHECK_EQ(std::int64_t(4000), *catalog.FindById("l1")->ColorTemperatureK);
    CHECK_EQ(std::size_t(4), catalog.FindById("l1")->ExtraAttributes.size());
}

TEST_CASE(CatalogAllowsMissingAttributes) {
    const auto json = nlohmann::json::array(
        {testdata::ListingJson("t1", "table", 5000, "taganrog", nlohmann::json::object())});
    const testdata::TempFile file("catalog.json", Dump(json));

    const auto catalog = ba::LoadCatalog(file.String(), testdata::Cities());
    const auto* listing = catalog.FindById("t1");
    CHECK(listing != nullptr);
    CHECK(!listing->WidthCm.has_value());
    CHECK(!listing->AdjustableHeight.has_value());
    CHECK(!listing->ColorTemperatureK.has_value());
}

TEST_CASE(CatalogAllowsEmptyArray) {
    const testdata::TempFile file("catalog.json", "[]");
    const auto catalog = ba::LoadCatalog(file.String(), testdata::Cities());
    CHECK_EQ(std::size_t(0), catalog.Size());
}

TEST_CASE(CatalogRejectsWrongRootShape) {
    const testdata::TempFile object("catalog.json", "{}");
    CHECK_THROWS_AS(ba::LoadCatalog(object.String(), testdata::Cities()), ba::AppError);

    const testdata::TempFile number("catalog.json", "42");
    CHECK_THROWS_AS(ba::LoadCatalog(number.String(), testdata::Cities()), ba::AppError);
}

TEST_CASE(CatalogRejectsMalformedJsonAndMissingFiles) {
    const testdata::TempFile broken("catalog.json", "[{]");
    CHECK_THROWS_AS(ba::LoadCatalog(broken.String(), testdata::Cities()), ba::AppError);
    CHECK_THROWS_AS(ba::LoadCatalog("no-such-catalog-file.json", testdata::Cities()),
                    ba::AppError);
}

TEST_CASE(CatalogRejectsUnknownCategory) {
    const auto json = nlohmann::json::array(
        {testdata::ListingJson("x1", "sofa", 5000, "taganrog", nlohmann::json::object())});
    const testdata::TempFile file("catalog.json", Dump(json));
    CHECK_THROWS_AS(ba::LoadCatalog(file.String(), testdata::Cities()), ba::AppError);
}

TEST_CASE(CatalogRejectsUnknownCity) {
    const auto json = nlohmann::json::array(
        {testdata::ListingJson("t1", "table", 5000, "moscow", nlohmann::json::object())});
    const testdata::TempFile file("catalog.json", Dump(json));
    CHECK_THROWS_AS(ba::LoadCatalog(file.String(), testdata::Cities()), ba::AppError);
}

TEST_CASE(CatalogValidatesRequiredFields) {
    auto missingTitle = testdata::ListingJson("t1", "table", 5000, "taganrog", {});
    missingTitle.erase("title");
    const testdata::TempFile noTitle("catalog.json", Dump(nlohmann::json::array({missingTitle})));
    CHECK_THROWS_AS(ba::LoadCatalog(noTitle.String(), testdata::Cities()), ba::AppError);

    auto emptyTitle = testdata::ListingJson("t1", "table", 5000, "taganrog", {});
    emptyTitle["title"] = "";
    const testdata::TempFile blankTitle("catalog.json",
                                        Dump(nlohmann::json::array({emptyTitle})));
    CHECK_THROWS_AS(ba::LoadCatalog(blankTitle.String(), testdata::Cities()), ba::AppError);

    auto extraField = testdata::ListingJson("t1", "table", 5000, "taganrog", {});
    extraField["discount"] = 10;
    const testdata::TempFile unknownField("catalog.json",
                                          Dump(nlohmann::json::array({extraField})));
    CHECK_THROWS_AS(ba::LoadCatalog(unknownField.String(), testdata::Cities()), ba::AppError);

    auto emptyDescription = testdata::ListingJson("t1", "table", 5000, "taganrog", {});
    emptyDescription["description"] = "";
    const testdata::TempFile allowed("catalog.json",
                                     Dump(nlohmann::json::array({emptyDescription})));
    CHECK_EQ(std::size_t(1), ba::LoadCatalog(allowed.String(), testdata::Cities()).Size());
}

TEST_CASE(CatalogValidatesPrices) {
    for (const auto price : {nlohmann::json(0), nlohmann::json(-100), nlohmann::json(12.5),
                             nlohmann::json("5000")}) {
        auto item = testdata::ListingJson("t1", "table", 5000, "taganrog", {});
        item["price_rub"] = price;
        const testdata::TempFile file("catalog.json",
                                      Dump(nlohmann::json::array({item})));
        CHECK_THROWS_AS(ba::LoadCatalog(file.String(), testdata::Cities()), ba::AppError);
    }
}

TEST_CASE(CatalogValidatesKnownAttributeTypes) {
    const auto make = [](const nlohmann::json& attributes) {
        return nlohmann::json::array(
            {testdata::ListingJson("x", "table", 5000, "taganrog", attributes)});
    };

    const testdata::TempFile widthString("catalog.json",
                                         Dump(make({{"width_cm", "90"}})));
    CHECK_THROWS_AS(ba::LoadCatalog(widthString.String(), testdata::Cities()), ba::AppError);

    const testdata::TempFile widthFloat("catalog.json", Dump(make({{"width_cm", 90.5}})));
    CHECK_THROWS_AS(ba::LoadCatalog(widthFloat.String(), testdata::Cities()), ba::AppError);

    const testdata::TempFile adjustableString(
        "catalog.json", Dump(make({{"adjustable_height", "yes"}})));
    CHECK_THROWS_AS(ba::LoadCatalog(adjustableString.String(), testdata::Cities()),
                    ba::AppError);

    const testdata::TempFile temperatureBool(
        "catalog.json", Dump(make({{"color_temperature_k", true}})));
    CHECK_THROWS_AS(ba::LoadCatalog(temperatureBool.String(), testdata::Cities()),
                    ba::AppError);

    const testdata::TempFile attributesArray("catalog.json", Dump(make(nlohmann::json::array())));
    CHECK_THROWS_AS(ba::LoadCatalog(attributesArray.String(), testdata::Cities()),
                    ba::AppError);

    const testdata::TempFile nestedValue("catalog.json",
                                         Dump(make({{"dimensions", {{"width", 90}}}})));
    CHECK_THROWS_AS(ba::LoadCatalog(nestedValue.String(), testdata::Cities()), ba::AppError);
}

TEST_CASE(CatalogRejectsTooManyListings) {
    nlohmann::json listings = nlohmann::json::array();
    for (int i = 0; i < 251; ++i) {
        listings.push_back(testdata::ListingJson("id-" + std::to_string(i), "table", 1000,
                                                 "taganrog", {}));
    }
    const testdata::TempFile file("catalog.json", Dump(listings));
    CHECK_THROWS_AS(ba::LoadCatalog(file.String(), testdata::Cities()), ba::AppError);
}

TEST_CASE(CityDirectoryLoadsValidFile) {
    const auto json = nlohmann::json::array(
        {nlohmann::json{{"id", "taganrog"}, {"name", "Таганрог"}},
         nlohmann::json{{"id", "azov"}, {"name", "Азов"}}});
    const testdata::TempFile file("cities.json", Dump(json));
    const auto cities = ba::LoadCityDirectory(file.String());
    CHECK_EQ(std::size_t(2), cities.All().size());
    CHECK(cities.Contains("azov"));
    CHECK(!cities.Contains("moscow"));
    CHECK(cities.FindByName("Таганрог") != nullptr);
    CHECK(cities.FindById("taganrog") != nullptr);
    CHECK_EQ(std::string("Таганрог"), cities.FindById("taganrog")->Name);
}

TEST_CASE(CityDirectoryRejectsInvalidFiles) {
    const testdata::TempFile empty("cities.json", "[]");
    CHECK_THROWS_AS(ba::LoadCityDirectory(empty.String()), ba::AppError);

    const testdata::TempFile duplicateId(
        "cities.json",
        Dump(nlohmann::json::array(
            {nlohmann::json{{"id", "a"}, {"name", "A"}},
             nlohmann::json{{"id", "a"}, {"name", "B"}}})));
    CHECK_THROWS_AS(ba::LoadCityDirectory(duplicateId.String()), ba::AppError);

    const testdata::TempFile duplicateName(
        "cities.json",
        Dump(nlohmann::json::array(
            {nlohmann::json{{"id", "a"}, {"name", "A"}},
             nlohmann::json{{"id", "b"}, {"name", "A"}}})));
    CHECK_THROWS_AS(ba::LoadCityDirectory(duplicateName.String()), ba::AppError);

    const testdata::TempFile extraKey(
        "cities.json",
        Dump(nlohmann::json::array(
            {nlohmann::json{{"id", "a"}, {"name", "A"}, {"population", 1}}})));
    CHECK_THROWS_AS(ba::LoadCityDirectory(extraKey.String()), ba::AppError);

    const testdata::TempFile missingName(
        "cities.json", Dump(nlohmann::json::array({nlohmann::json{{"id", "a"}}})));
    CHECK_THROWS_AS(ba::LoadCityDirectory(missingName.String()), ba::AppError);
}

TEST_CASE(CategoryDirectoryLoadsValidFile) {
    const auto json = nlohmann::json::array(
        {nlohmann::json{{"id", "table"}, {"name", "Стол"}, {"attributes", nlohmann::json::object({{"width_cm", "см"}})}},
         nlohmann::json{{"id", "chair"}, {"name", "Кресло"}, {"attributes", nlohmann::json::object()}},
         nlohmann::json{{"id", "lamp"}, {"name", "Лампа"}, {"attributes", nlohmann::json::object()}}});
    const testdata::TempFile file("categories.json", Dump(json));
    const auto categories = ba::LoadCategoryDirectory(file.String());
    CHECK_EQ(std::size_t(3), categories.All().size());
    CHECK_EQ(std::string("Стол"), categories.NameOf(ba::Category::Table));
    CHECK(categories.Find(ba::Category::Lamp) != nullptr);
}

TEST_CASE(CategoryDirectoryRejectsIncompleteOrInvalidFiles) {
    const testdata::TempFile incomplete(
        "categories.json",
        Dump(nlohmann::json::array(
            {nlohmann::json{{"id", "table"}, {"name", "Стол"}, {"attributes", nlohmann::json::object()}},
             nlohmann::json{{"id", "chair"}, {"name", "Кресло"}, {"attributes", nlohmann::json::object()}}})));
    CHECK_THROWS_AS(ba::LoadCategoryDirectory(incomplete.String()), ba::AppError);

    const testdata::TempFile unknown(
        "categories.json",
        Dump(nlohmann::json::array(
            {nlohmann::json{{"id", "table"}, {"name", "Стол"}, {"attributes", nlohmann::json::object()}},
             nlohmann::json{{"id", "chair"}, {"name", "Кресло"}, {"attributes", nlohmann::json::object()}},
             nlohmann::json{{"id", "sofa"}, {"name", "Диван"}, {"attributes", nlohmann::json::object()}}})));
    CHECK_THROWS_AS(ba::LoadCategoryDirectory(unknown.String()), ba::AppError);

    const testdata::TempFile duplicate(
        "categories.json",
        Dump(nlohmann::json::array(
            {nlohmann::json{{"id", "table"}, {"name", "Стол"}, {"attributes", nlohmann::json::object()}},
             nlohmann::json{{"id", "table"}, {"name", "Столик"}, {"attributes", nlohmann::json::object()}},
             nlohmann::json{{"id", "lamp"}, {"name", "Лампа"}, {"attributes", nlohmann::json::object()}}})));
    CHECK_THROWS_AS(ba::LoadCategoryDirectory(duplicate.String()), ba::AppError);

    const testdata::TempFile missingAttributes(
        "categories.json",
        Dump(nlohmann::json::array(
            {nlohmann::json{{"id", "table"}, {"name", "Стол"}},
             nlohmann::json{{"id", "chair"}, {"name", "Кресло"}},
             nlohmann::json{{"id", "lamp"}, {"name", "Лампа"}}})));
    CHECK_THROWS_AS(ba::LoadCategoryDirectory(missingAttributes.String()), ba::AppError);
}
