#pragma once

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ba/core/BundleSearch.hpp"
#include "ba/core/Catalog.hpp"
#include "ba/core/DecisionEngine.hpp"
#include "ba/core/DialogueAnalyzer.hpp"
#include "ba/models/Directory.hpp"
#include "ba/models/Message.hpp"
#include "ba/models/Response.hpp"
#include "ba/services/Config.hpp"
#include "ba/services/HttpTransport.hpp"
#include "ba/services/LlmExtractor.hpp"
#include "ba/utils/Logger.hpp"

namespace ba {

class Application {
public:
    // Loads the catalog, uses cities.json/categories.json from the catalog's
    // directory when present, otherwise falls back to the contract defaults.
    explicit Application(const std::string& catalogPath,
                         Config config,
                         bool disableLlm = false,
                         bool dumpRequirements = false,
                         std::shared_ptr<IHttpTransport> transport = nullptr);

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    Response Handle(const nlohmann::json& request);

    static std::vector<Message> ParseMessages(const nlohmann::json& request,
                                              std::size_t maxInputBytes);

    const Catalog& CatalogData() const noexcept { return Catalog_; }
    const CityDirectory& Cities() const noexcept { return Cities_; }
    const CategoryDirectory& Categories() const noexcept { return Categories_; }

private:
    LlmMode EffectiveMode() const noexcept;
    bool ShouldConsultLlm(const Requirements& requirements,
                          const std::vector<Message>& messages) const;

    CityDirectory Cities_;
    CategoryDirectory Categories_;
    Catalog Catalog_;
    DialogueAnalyzer Analyzer_;
    BundleSearch Search_;
    DecisionEngine Decision_;
    Config Config_;
    bool DisableLlm_ = false;
    bool DumpRequirements_ = false;
    LlmExtractor Extractor_;
    Logger Logger_;
};

} // namespace ba
