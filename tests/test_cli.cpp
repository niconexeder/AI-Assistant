#include "TestHarness.hpp"

#include <string>
#include <vector>

#include "ba/cli/Arguments.hpp"
#include "ba/utils/Errors.hpp"

TEST_CASE(CliParsesCatalogAndFlags) {
    const auto options = ba::ParseArguments(
        {"--catalog", "catalog.json", "--no-llm", "--dump-requirements"});
    CHECK_EQ(std::string("catalog.json"), options.CatalogPath);
    CHECK(options.NoLlm);
    CHECK(options.DumpRequirements);
    CHECK(!options.ShowHelp);
    CHECK(!options.ShowVersion);
}

TEST_CASE(CliParsesEqualsFormsAndLogLevel) {
    auto options = ba::ParseArguments(
        {"--catalog=data/catalog.json", "--config=config.json", "--log-level=DEBUG"});
    CHECK_EQ(std::string("data/catalog.json"), options.CatalogPath);
    CHECK_EQ(std::string("config.json"), options.ConfigPath);
    CHECK(options.LogLevelOverride.has_value());
    CHECK(*options.LogLevelOverride == ba::LogLevel::Debug);

    options = ba::ParseArguments({"--catalog", "c.json", "--log-level", "warning"});
    CHECK(*options.LogLevelOverride == ba::LogLevel::Warn);
}

TEST_CASE(CliAllowsHelpAndVersionWithoutCatalog) {
    auto options = ba::ParseArguments({"--help"});
    CHECK(options.ShowHelp);
    options = ba::ParseArguments({"-h"});
    CHECK(options.ShowHelp);
    options = ba::ParseArguments({"--version"});
    CHECK(options.ShowVersion);
}

TEST_CASE(CliRejectsMissingCatalogAndUnknownArguments) {
    CHECK_THROWS_AS(ba::ParseArguments({}), ba::AppError);
    CHECK_THROWS_AS(ba::ParseArguments({"--no-llm"}), ba::AppError);
    CHECK_THROWS_AS(ba::ParseArguments({"--unknown"}), ba::AppError);
    CHECK_THROWS_AS(ba::ParseArguments({"--catalog"}), ba::AppError);
    CHECK_THROWS_AS(ba::ParseArguments({"--config"}), ba::AppError);
    CHECK_THROWS_AS(ba::ParseArguments({"--log-level"}), ba::AppError);
    CHECK_THROWS_AS(ba::ParseArguments({"--catalog", "c.json", "--log-level", "loud"}),
                    ba::AppError);
}

TEST_CASE(CliUsageTextDocumentsOptionsAndEnvironment) {
    const auto usage = ba::UsageText();
    CHECK(usage.find("--catalog PATH") != std::string::npos);
    CHECK(usage.find("--config PATH") != std::string::npos);
    CHECK(usage.find("--no-llm") != std::string::npos);
    CHECK(usage.find("--dump-requirements") != std::string::npos);
    CHECK(usage.find("--log-level") != std::string::npos);
    CHECK(usage.find("LLM_API_KEY") != std::string::npos);
}

TEST_CASE(CliMapsErrorsToExitCodes) {
    CHECK_EQ(2, ba::ExitCodeFor(ba::ErrorKind::Usage));
    CHECK_EQ(3, ba::ExitCodeFor(ba::ErrorKind::Input));
    CHECK_EQ(3, ba::ExitCodeFor(ba::ErrorKind::Configuration));
    CHECK_EQ(3, ba::ExitCodeFor(ba::ErrorKind::Catalog));
    CHECK_EQ(4, ba::ExitCodeFor(ba::ErrorKind::ExternalService));
    CHECK_EQ(1, ba::ExitCodeFor(ba::ErrorKind::Internal));
}
