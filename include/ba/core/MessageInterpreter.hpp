#pragma once

#include <string>
#include <vector>

#include "ba/models/Directory.hpp"
#include "ba/models/MessageFacts.hpp"

namespace ba {

class MessageInterpreter {
public:
    MessageInterpreter(const CityDirectory& cities, const CategoryDirectory& categories);

    MessageFacts Interpret(const std::string& text,
                           const std::string& previousAssistantText = {}) const;

private:
    struct CityMatcher {
        std::string Id;
        std::vector<std::string> Stems;
    };

    const CityDirectory* Cities_ = nullptr;
    const CategoryDirectory* Categories_ = nullptr;
    std::vector<CityMatcher> CityMatchers_;
};

} // namespace ba
