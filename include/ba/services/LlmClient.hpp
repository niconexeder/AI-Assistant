#pragma once

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "ba/services/Config.hpp"
#include "ba/services/HttpTransport.hpp"

namespace ba {

// Thin, validated client for the organizers' Chat Completions subset.
// It returns the parsed JSON object that the model placed in
// choices[0].message.content.
class LlmClient {
public:
    LlmClient(LlmConfig config, std::shared_ptr<IHttpTransport> transport);

    bool HasCredentials() const noexcept { return !Config_.ApiKey.empty(); }

    nlohmann::json RequestJson(const std::string& systemPrompt,
                               const std::string& userPrompt,
                               const nlohmann::json& schema,
                               const std::string& schemaName) const;

private:
    LlmConfig Config_;
    std::shared_ptr<IHttpTransport> Transport_;
};

} // namespace ba
