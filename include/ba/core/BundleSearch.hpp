#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "ba/core/Catalog.hpp"
#include "ba/models/Directory.hpp"
#include "ba/models/Requirements.hpp"

namespace ba {

struct Bundle {
    std::vector<const Listing*> Listings;
    std::int64_t TotalRub = 0;
};

struct CategoryAvailability {
    Category CategoryValue = Category::Table;
    std::size_t InCity = 0;
    std::size_t Matching = 0;
    std::optional<std::int64_t> Cheapest;
};

struct SearchDiagnostics {
    std::vector<CategoryAvailability> Categories;
    std::optional<std::int64_t> CheapestComplete;
    std::size_t ListingsInCity = 0;
};

class BundleSearch {
public:
    BundleSearch(const Catalog& catalog, const CategoryDirectory& categories);

    std::vector<Bundle> Find(const Requirements& requirements,
                             std::size_t maxOptions) const;
    SearchDiagnostics Diagnose(const Requirements& requirements) const;

    static bool Matches(const Listing& listing, const Requirements& requirements);

private:
    const Catalog* Catalog_ = nullptr;
    const CategoryDirectory* Categories_ = nullptr;
};

} // namespace ba
