#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ba/core/BundleSearch.hpp"
#include "ba/core/Catalog.hpp"
#include "ba/models/Directory.hpp"
#include "ba/models/Requirements.hpp"
#include "ba/models/Response.hpp"

namespace ba {

// Turns accumulated requirements into the single response object required by
// the contract. Only structured catalog fields and trusted directories are
// used for user-facing explanations; listing titles and descriptions never
// reach the output.
class DecisionEngine {
public:
    DecisionEngine(const BundleSearch& search,
                   const CityDirectory& cities,
                   std::size_t maxOptions);

    Response Decide(const Requirements& requirements) const;

private:
    std::vector<ClarifyQuestion> BuildQuestions(const Requirements& requirements) const;
    std::string BuildNoMatchMessage(const Requirements& requirements) const;
    std::string BuildExplanation(const Bundle& bundle) const;

    const BundleSearch* Search_ = nullptr;
    const CityDirectory* Cities_ = nullptr;
    std::size_t MaxOptions_ = 3;
};

} // namespace ba
