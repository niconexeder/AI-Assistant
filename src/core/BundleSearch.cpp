#include "ba/core/BundleSearch.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <numeric>
#include <utility>

namespace ba {

namespace {

constexpr std::int64_t InfiniteBudget = std::numeric_limits<std::int64_t>::max() / 4;

bool BetterBundle(const Bundle& left, const Bundle& right) {
    if (left.TotalRub != right.TotalRub) {
        return left.TotalRub < right.TotalRub;
    }
    for (std::size_t i = 0; i < left.Listings.size() && i < right.Listings.size(); ++i) {
        if (left.Listings[i]->Id != right.Listings[i]->Id) {
            return left.Listings[i]->Id < right.Listings[i]->Id;
        }
    }
    return left.Listings.size() < right.Listings.size();
}

bool SameBundle(const Bundle& left, const Bundle& right) {
    if (left.Listings.size() != right.Listings.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.Listings.size(); ++i) {
        if (left.Listings[i]->Id != right.Listings[i]->Id) {
            return false;
        }
    }
    return true;
}

std::int64_t SafeAdd(std::int64_t left, std::int64_t right) {
    if (right > std::numeric_limits<std::int64_t>::max() - left) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return left + right;
}

} // namespace

BundleSearch::BundleSearch(const Catalog& catalog, const CategoryDirectory& categories)
    : Catalog_(&catalog), Categories_(&categories) {}

bool BundleSearch::Matches(const Listing& listing, const Requirements& requirements) {
    switch (listing.CategoryValue) {
    case Category::Table:
        if (requirements.TableWidth.has_value()) {
            return listing.WidthCm.has_value() &&
                   requirements.TableWidth->Matches(*listing.WidthCm);
        }
        return true;
    case Category::Chair:
        if (requirements.ChairAdjustableHeight.has_value()) {
            return listing.AdjustableHeight.has_value() &&
                   *listing.AdjustableHeight == *requirements.ChairAdjustableHeight;
        }
        return true;
    case Category::Lamp:
        if (requirements.LampColorTemperature.has_value()) {
            return listing.ColorTemperatureK.has_value() &&
                   requirements.LampColorTemperature->Matches(*listing.ColorTemperatureK);
        }
        return true;
    }
    return false;
}

std::vector<Bundle> BundleSearch::Find(const Requirements& requirements,
                                       std::size_t maxOptions) const {
    if (maxOptions == 0 || !requirements.CityId.has_value() ||
        requirements.Categories.empty()) {
        return {};
    }
    maxOptions = std::min<std::size_t>(maxOptions, 3);

    struct CandidateGroup {
        Category CategoryValue;
        std::vector<const Listing*> Listings;
    };

    std::vector<CandidateGroup> groups;
    for (const auto category : requirements.Categories) {
        CandidateGroup group;
        group.CategoryValue = category;
        for (const auto* listing :
             Catalog_->InCityAndCategory(*requirements.CityId, category)) {
            if (Matches(*listing, requirements) &&
                (!requirements.BudgetRub.has_value() ||
                 listing->PriceRub <= *requirements.BudgetRub)) {
                group.Listings.push_back(listing);
            }
        }
        std::sort(group.Listings.begin(), group.Listings.end(),
                  [](const Listing* left, const Listing* right) {
                      if (left->PriceRub != right->PriceRub) {
                          return left->PriceRub < right->PriceRub;
                      }
                      return left->Id < right->Id;
                  });
        if (group.Listings.empty()) {
            return {};
        }
        groups.push_back(std::move(group));
    }

    std::vector<std::size_t> order(groups.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        return groups[left].Listings.size() < groups[right].Listings.size();
    });

    std::vector<std::int64_t> suffixMinimum(groups.size() + 1, 0);
    for (std::size_t i = groups.size(); i-- > 0;) {
        suffixMinimum[i] = SafeAdd(suffixMinimum[i + 1],
                                   groups[order[i]].Listings.front()->PriceRub);
    }

    const std::int64_t budget =
        requirements.BudgetRub.value_or(InfiniteBudget);
    std::vector<const Listing*> chosen(groups.size(), nullptr);
    std::vector<Bundle> best;

    auto consider = [&](std::int64_t total) {
        Bundle bundle;
        bundle.TotalRub = total;
        bundle.Listings.reserve(groups.size());
        for (std::size_t i = 0; i < groups.size(); ++i) {
            if (chosen[i] == nullptr) {
                return;
            }
            bundle.Listings.push_back(chosen[i]);
        }
        for (const auto& existing : best) {
            if (SameBundle(existing, bundle)) {
                return;
            }
        }
        if (best.size() < maxOptions) {
            best.push_back(std::move(bundle));
        } else {
            auto worst = std::max_element(best.begin(), best.end(), BetterBundle);
            if (worst != best.end() && BetterBundle(bundle, *worst)) {
                *worst = std::move(bundle);
            }
        }
    };

    std::int64_t worstTotal = InfiniteBudget;
    std::function<void(std::size_t, std::int64_t)> visit =
        [&](std::size_t level, std::int64_t total) {
            if (total > budget) {
                return;
            }
            if (level == order.size()) {
                consider(total);
                if (best.size() >= maxOptions) {
                    worstTotal = std::max_element(best.begin(), best.end(), BetterBundle)->TotalRub;
                }
                return;
            }
            if (total + suffixMinimum[level] > budget) {
                return;
            }
            if (best.size() >= maxOptions && total + suffixMinimum[level] > worstTotal) {
                return;
            }
            const auto groupIndex = order[level];
            const auto& group = groups[groupIndex];
            for (const auto* listing : group.Listings) {
                if (total + listing->PriceRub > budget) {
                    break;
                }
                chosen[groupIndex] = listing;
                visit(level + 1, total + listing->PriceRub);
                chosen[groupIndex] = nullptr;
            }
        };

    visit(0, 0);

    std::sort(best.begin(), best.end(), BetterBundle);
    return best;
}

SearchDiagnostics BundleSearch::Diagnose(const Requirements& requirements) const {
    SearchDiagnostics diagnostics;
    if (!requirements.CityId.has_value()) {
        return diagnostics;
    }
    diagnostics.ListingsInCity = Catalog_->InCity(*requirements.CityId).size();

    std::int64_t complete = 0;
    bool hasComplete = true;
    for (const auto category : requirements.Categories) {
        CategoryAvailability availability;
        availability.CategoryValue = category;
        const auto inCity = Catalog_->InCityAndCategory(*requirements.CityId, category);
        availability.InCity = inCity.size();
        for (const auto* listing : inCity) {
            if (!Matches(*listing, requirements)) {
                continue;
            }
            ++availability.Matching;
            if (!availability.Cheapest.has_value() ||
                listing->PriceRub < *availability.Cheapest) {
                availability.Cheapest = listing->PriceRub;
            }
        }
        if (!availability.Cheapest.has_value()) {
            hasComplete = false;
        } else {
            complete = SafeAdd(complete, *availability.Cheapest);
        }
        diagnostics.Categories.push_back(availability);
    }
    if (hasComplete) {
        diagnostics.CheapestComplete = complete;
    }
    return diagnostics;
}

} // namespace ba
