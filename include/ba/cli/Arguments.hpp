#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ba/utils/Logger.hpp"

namespace ba {

struct CliOptions {
    std::string CatalogPath;
    std::string ConfigPath;
    bool NoLlm = false;
    bool DumpRequirements = false;
    bool ShowHelp = false;
    bool ShowVersion = false;
    std::optional<LogLevel> LogLevelOverride;
};

CliOptions ParseArguments(const std::vector<std::string>& arguments);

std::string UsageText();

} // namespace ba
