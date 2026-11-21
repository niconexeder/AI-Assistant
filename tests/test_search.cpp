#include "TestHarness.hpp"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "TestData.hpp"
#include "ba/core/BundleSearch.hpp"
#include "ba/core/Catalog.hpp"
#include "ba/models/Requirements.hpp"

namespace {

struct Fixture {
    Fixture()
        : CatalogData({testdata::MakeListing("t-80", ba::Category::Table, 3500, "taganrog", 80),
                       testdata::MakeListing("t-100", ba::Category::Table, 4000, "taganrog", 100),
                       testdata::MakeListing("t-120", ba::Category::Table, 5000, "taganrog", 120),
                       testdata::MakeListing("t-null", ba::Category::Table, 3000, "taganrog"),
                       testdata::MakeListing("c-fixed", ba::Category::Chair, 1500, "taganrog",
                                             std::nullopt, false),
                       testdata::MakeListing("c-adjust", ba::Category::Chair, 2000, "taganrog",
                                             std::nullopt, true),
                       testdata::MakeListing("c-null", ba::Category::Chair, 1000, "taganrog"),
                       testdata::MakeListing("l-3000", ba::Category::Lamp, 800, "taganrog",
                                             std::nullopt, std::nullopt, 3000),
                       testdata::MakeListing("l-4000", ba::Category::Lamp, 1200, "taganrog",
                                             std::nullopt, std::nullopt, 4000),
                       testdata::MakeListing("l-null", ba::Category::Lamp, 500, "taganrog"),
                       testdata::MakeListing("r-t", ba::Category::Table, 1000, "rostov-na-donu",
                                             80),
                       testdata::MakeListing("a-c", ba::Category::Chair, 100, "azov",
                                             std::nullopt, true)}),
          Categories(testdata::Categories()),
          Search(CatalogData, Categories) {}

    ba::Catalog CatalogData;
    ba::CategoryDirectory Categories;
    ba::BundleSearch Search;
};

ba::Requirements BaseRequirements() {
    auto requirements = testdata::FullRequirements();
    requirements.BudgetRub = 10000;
    ba::NumericConstraint width;
    width.Max = ba::NumericBound{100, true};
    requirements.TableWidth = width;
    return requirements;
}

} // namespace

TEST_CASE(SearchMatchesNumericConstraints) {
    ba::Listing table = testdata::MakeListing("t", ba::Category::Table, 100, "taganrog", 100);
    auto requirements = BaseRequirements();

    CHECK(ba::BundleSearch::Matches(table, requirements));
    table.WidthCm = 101;
    CHECK(!ba::BundleSearch::Matches(table, requirements));
    table.WidthCm.reset();
    CHECK(!ba::BundleSearch::Matches(table, requirements));

    requirements.TableWidth.reset();
    CHECK(ba::BundleSearch::Matches(table, requirements));
}

TEST_CASE(SearchRespectsInclusiveAndExclusiveBounds) {
    auto requirements = BaseRequirements();
    ba::Listing table = testdata::MakeListing("t", ba::Category::Table, 100, "taganrog", 100);

    requirements.TableWidth->Max = ba::NumericBound{100, true};
    CHECK(ba::BundleSearch::Matches(table, requirements));
    requirements.TableWidth->Max = ba::NumericBound{100, false};
    CHECK(!ba::BundleSearch::Matches(table, requirements));

    requirements.TableWidth.reset();
    requirements.ChairAdjustableHeight = false;
    ba::Listing chair = testdata::MakeListing("c", ba::Category::Chair, 100, "taganrog",
                                              std::nullopt, false);
    CHECK(ba::BundleSearch::Matches(chair, requirements));
    chair.AdjustableHeight = true;
    CHECK(!ba::BundleSearch::Matches(chair, requirements));
    chair.AdjustableHeight.reset();
    CHECK(!ba::BundleSearch::Matches(chair, requirements));

    requirements.ChairAdjustableHeight.reset();
    ba::NumericConstraint temperature;
    temperature.Min = ba::NumericBound{3000, true};
    temperature.Max = ba::NumericBound{4000, true};
    requirements.LampColorTemperature = temperature;
    ba::Listing lamp = testdata::MakeListing("l", ba::Category::Lamp, 100, "taganrog",
                                             std::nullopt, std::nullopt, 4000);
    CHECK(ba::BundleSearch::Matches(lamp, requirements));
    lamp.ColorTemperatureK = 4001;
    CHECK(!ba::BundleSearch::Matches(lamp, requirements));
    lamp.ColorTemperatureK.reset();
    CHECK(!ba::BundleSearch::Matches(lamp, requirements));
}

TEST_CASE(SearchFindsCheapestBundlesInOrder) {
    Fixture fixture;
    const auto bundles = fixture.Search.Find(BaseRequirements(), 3);
    CHECK_EQ(std::size_t(3), bundles.size());
    CHECK_EQ(std::int64_t(5000), bundles[0].TotalRub);
    CHECK_EQ(std::int64_t(5300), bundles[1].TotalRub);
    CHECK_EQ(std::int64_t(5500), bundles[2].TotalRub);

    CHECK_EQ(std::string("t-80"), bundles[0].Listings[0]->Id);
    CHECK_EQ(std::string("c-null"), bundles[0].Listings[1]->Id);
    CHECK_EQ(std::string("l-null"), bundles[0].Listings[2]->Id);

    for (const auto& bundle : bundles) {
        CHECK_EQ(std::size_t(3), bundle.Listings.size());
        std::int64_t total = 0;
        std::set<ba::Category> categories;
        for (const auto* listing : bundle.Listings) {
            total += listing->PriceRub;
            CHECK_EQ(std::string("taganrog"), listing->CityId);
            CHECK(categories.insert(listing->CategoryValue).second);
            if (listing->CategoryValue == ba::Category::Table) {
                CHECK(listing->WidthCm.has_value());
                CHECK(*listing->WidthCm <= 100);
            }
        }
        CHECK_EQ(bundle.TotalRub, total);
        CHECK(total <= 10000);
    }
}

TEST_CASE(SearchHonoursExactBudget) {
    Fixture fixture;
    auto requirements = BaseRequirements();
    requirements.BudgetRub = 5000;
    auto bundles = fixture.Search.Find(requirements, 3);
    CHECK_EQ(std::size_t(1), bundles.size());
    CHECK_EQ(std::int64_t(5000), bundles[0].TotalRub);

    requirements.BudgetRub = 4999;
    bundles = fixture.Search.Find(requirements, 3);
    CHECK(bundles.empty());
}

TEST_CASE(SearchHonoursOptionalAttributes) {
    Fixture fixture;
    auto requirements = BaseRequirements();
    requirements.ChairAdjustableHeight = true;
    const auto bundles = fixture.Search.Find(requirements, 1);
    CHECK_EQ(std::size_t(1), bundles.size());
    CHECK_EQ(std::int64_t(6000), bundles[0].TotalRub);
    CHECK_EQ(std::string("c-adjust"), bundles[0].Listings[1]->Id);

    requirements = BaseRequirements();
    ba::NumericConstraint temperature;
    temperature.Min = ba::NumericBound{4000, true};
    temperature.Max = ba::NumericBound{4000, true};
    requirements.LampColorTemperature = temperature;
    const auto lampBundles = fixture.Search.Find(requirements, 1);
    CHECK_EQ(std::size_t(1), lampBundles.size());
    CHECK_EQ(std::string("l-4000"), lampBundles[0].Listings[2]->Id);
}

TEST_CASE(SearchLimitsOptionCount) {
    Fixture fixture;
    CHECK_EQ(std::size_t(1), fixture.Search.Find(BaseRequirements(), 1).size());
    CHECK_EQ(std::size_t(2), fixture.Search.Find(BaseRequirements(), 2).size());
    CHECK_EQ(std::size_t(3), fixture.Search.Find(BaseRequirements(), 99).size());
    CHECK(fixture.Search.Find(BaseRequirements(), 0).empty());
}

TEST_CASE(SearchRequiresCityAndCategories) {
    Fixture fixture;
    auto requirements = BaseRequirements();
    requirements.CityId.reset();
    CHECK(fixture.Search.Find(requirements, 3).empty());

    requirements = BaseRequirements();
    requirements.Categories.clear();
    CHECK(fixture.Search.Find(requirements, 3).empty());
}

TEST_CASE(SearchReturnsNothingWhenConditionIsImpossible) {
    Fixture fixture;
    auto requirements = BaseRequirements();
    requirements.TableWidth->Max = ba::NumericBound{10, true};
    CHECK(fixture.Search.Find(requirements, 3).empty());
}

TEST_CASE(SearchDiagnosticsReportAvailability) {
    Fixture fixture;
    const auto diagnostics = fixture.Search.Diagnose(BaseRequirements());
    CHECK_EQ(std::size_t(3), diagnostics.Categories.size());
    CHECK_EQ(std::size_t(10), diagnostics.ListingsInCity);
    CHECK_EQ(std::size_t(4), diagnostics.Categories[0].InCity);
    CHECK_EQ(std::size_t(2), diagnostics.Categories[0].Matching);
    CHECK_EQ(std::int64_t(3500), *diagnostics.Categories[0].Cheapest);
    CHECK_EQ(std::size_t(3), diagnostics.Categories[1].Matching);
    CHECK_EQ(std::int64_t(1000), *diagnostics.Categories[1].Cheapest);
    CHECK_EQ(std::size_t(3), diagnostics.Categories[2].Matching);
    CHECK_EQ(std::int64_t(500), *diagnostics.Categories[2].Cheapest);
    CHECK_EQ(std::int64_t(5000), *diagnostics.CheapestComplete);
}

TEST_CASE(SearchDiagnosticsDistinguishMissingCategory) {
    Fixture fixture;
    auto requirements = BaseRequirements();
    requirements.CityId = "azov";
    const auto diagnostics = fixture.Search.Diagnose(requirements);
    CHECK_EQ(std::size_t(1), diagnostics.ListingsInCity);
    CHECK_EQ(std::size_t(3), diagnostics.Categories.size());
    CHECK_EQ(std::size_t(0), diagnostics.Categories[0].InCity);
    CHECK_EQ(std::size_t(0), diagnostics.Categories[0].Matching);
    CHECK(!diagnostics.Categories[0].Cheapest.has_value());
    CHECK_EQ(std::size_t(1), diagnostics.Categories[1].InCity);
    CHECK_EQ(std::size_t(1), diagnostics.Categories[1].Matching);
    CHECK_EQ(std::int64_t(100), *diagnostics.Categories[1].Cheapest);
    CHECK_EQ(std::size_t(0), diagnostics.Categories[2].InCity);
    CHECK(!diagnostics.CheapestComplete.has_value());
}

TEST_CASE(SearchDiagnosticsWithoutCity) {
    Fixture fixture;
    auto requirements = BaseRequirements();
    requirements.CityId.reset();
    const auto diagnostics = fixture.Search.Diagnose(requirements);
    CHECK_EQ(std::size_t(0), diagnostics.ListingsInCity);
    CHECK(diagnostics.Categories.empty());
    CHECK(!diagnostics.CheapestComplete.has_value());
}

TEST_CASE(SearchDiagnosticsWithImpossibleAttribute) {
    Fixture fixture;
    auto requirements = BaseRequirements();
    requirements.ChairAdjustableHeight = false;
    requirements.TableWidth->Max = ba::NumericBound{10, true};
    const auto diagnostics = fixture.Search.Diagnose(requirements);
    CHECK_EQ(std::size_t(0), diagnostics.Categories[0].Matching);
    CHECK_EQ(std::size_t(1), diagnostics.Categories[1].Matching);
    CHECK(!diagnostics.CheapestComplete.has_value());
}
