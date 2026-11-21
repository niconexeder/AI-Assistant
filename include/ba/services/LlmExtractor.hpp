#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ba/models/Directory.hpp"
#include "ba/models/Message.hpp"
#include "ba/models/Requirements.hpp"
#include "ba/services/Config.hpp"
#include "ba/services/HttpTransport.hpp"
#include "ba/services/LlmClient.hpp"

namespace ba {

struct LlmFacts {
    std::optional<std::string> CityId;
    std::optional<std::int64_t> BudgetRub;
    std::vector<Category> Categories;
    std::optional<std::int64_t> TableWidthMaxCm;
    std::optional<bool> ChairAdjustableHeight;
    std::optional<std::int64_t> LampColorTemperatureK;
};

// Uses the organizers' model only to fill conditions the deterministic
// interpreter could not read from the dialogue. Explicit deterministic facts
// always win, and model output never reaches the response verbatim.
class LlmExtractor {
public:
    LlmExtractor(LlmConfig config,
                 const CityDirectory& cities,
                 const CategoryDirectory& categories,
                 std::shared_ptr<IHttpTransport> transport);

    bool Available() const noexcept { return Client_.HasCredentials(); }

    LlmFacts Extract(const std::vector<Message>& messages) const;

    Requirements Merge(const Requirements& deterministic,
                       const LlmFacts& facts,
                       const std::string& dialogueText) const;

private:
    nlohmann::json BuildSchema() const;
    std::string BuildSystemPrompt() const;
    std::string BuildDialogueText(const std::vector<Message>& messages) const;

    LlmClient Client_;
    const CityDirectory* Cities_ = nullptr;
    const CategoryDirectory* Categories_ = nullptr;
};

} // namespace ba
