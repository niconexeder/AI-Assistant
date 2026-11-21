#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "ba/models/Category.hpp"
#include "ba/models/Directory.hpp"
#include "ba/models/Listing.hpp"

namespace ba {

class Catalog {
public:
    explicit Catalog(std::vector<Listing> listings);

    const std::vector<Listing>& All() const noexcept { return Listings_; }
    std::size_t Size() const noexcept { return Listings_.size(); }
    const Listing* FindById(const std::string& id) const;
    std::vector<const Listing*> InCity(const std::string& cityId) const;
    std::vector<const Listing*> InCityAndCategory(const std::string& cityId,
                                                  Category category) const;

private:
    void BuildIndex();

    std::vector<Listing> Listings_;
    std::unordered_map<std::string, std::size_t> Index_;
};

Catalog LoadCatalog(const std::string& path, const CityDirectory& cities);

} // namespace ba
