#include "TestHarness.hpp"

#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "TestData.hpp"
#include "ba/services/Config.hpp"
#include "ba/utils/Errors.hpp"

namespace {

class EnvironmentVariable {
public:
    explicit EnvironmentVariable(const char* name) : Name_(name) {
#ifdef _WIN32
        char* buffer = nullptr;
        std::size_t length = 0;
        if (_dupenv_s(&buffer, &length, name) == 0 && buffer != nullptr) {
            Old_ = std::string(buffer);
            std::free(buffer);
        }
#else
        const char* value = std::getenv(name);
        if (value != nullptr) {
            Old_ = std::string(value);
        }
#endif
    }

    ~EnvironmentVariable() {
        if (Old_.has_value()) {
            Assign(Old_->c_str());
        } else {
            Clear();
        }
    }

    EnvironmentVariable(const EnvironmentVariable&) = delete;
    EnvironmentVariable& operator=(const EnvironmentVariable&) = delete;

    void Set(const char* value) { Assign(value); }

    void Clear() {
#ifdef _WIN32
        _putenv_s(Name_.c_str(), "");
#else
        unsetenv(Name_.c_str());
#endif
    }

private:
    void Assign(const char* value) {
#ifdef _WIN32
        _putenv_s(Name_.c_str(), value);
#else
        setenv(Name_.c_str(), value, 1);
#endif
    }

    std::string Name_;
    std::optional<std::string> Old_;
};

struct CleanEnvironment {
    EnvironmentVariable BaseUrl{"LLM_BASE_URL"};
    EnvironmentVariable Model{"LLM_MODEL"};
    EnvironmentVariable ApiKey{"LLM_API_KEY"};
    EnvironmentVariable UserAgent{"LLM_USER_AGENT"};

    CleanEnvironment() {
        BaseUrl.Clear();
        Model.Clear();
        ApiKey.Clear();
        UserAgent.Clear();
    }
};

ba::Config LoadJson(const nlohmann::json& value) {
    const testdata::TempFile file("config.json", value.dump(2));
    return ba::LoadConfig(file.String());
}

} // namespace

TEST_CASE(ConfigDefaultsMatchContract) {
    const auto config = ba::DefaultConfig();
    CHECK_EQ(std::size_t(3), config.App.MaxOptions);
    CHECK_EQ(std::size_t(1024U * 1024U), config.App.MaxInputBytes);
    CHECK(config.Llm.Mode == ba::LlmMode::Auto);
    CHECK_EQ(std::string("google/gemma-4-31B-it"), config.Llm.Model);
    CHECK(config.Llm.BaseUrl.rfind("https://", 0) == 0);
    CHECK(config.Llm.ApiKey.empty());
    CHECK_EQ(20, config.Llm.TimeoutSeconds);
    CHECK_EQ(512, config.Llm.MaxTokens);
    CHECK(!config.Llm.Required);
}

TEST_CASE(ConfigLoadsWithoutFile) {
    CleanEnvironment environment;
    const auto config = ba::LoadConfig("");
    CHECK(config.Llm.Mode == ba::LlmMode::Auto);
    CHECK(config.Llm.ApiKey.empty());
    CHECK_EQ(3U, config.App.MaxOptions);
}

TEST_CASE(ConfigReadsFullFile) {
    CleanEnvironment environment;
    const auto config = LoadJson(
        {{"llm",
          {{"mode", "always"},
           {"baseUrl", "https://example.test/v1"},
           {"model", "test-model"},
           {"apiKey", "secret-key"},
           {"userAgent", "test-agent"},
           {"timeoutSeconds", 30},
           {"maxTokens", 1000},
           {"temperature", 0.5},
           {"maxRetries", 2},
           {"required", true}}},
         {"app", {{"maxOptions", 2}, {"maxInputBytes", 2048}, {"logLevel", "debug"}}}});

    CHECK(config.Llm.Mode == ba::LlmMode::Always);
    CHECK_EQ(std::string("https://example.test/v1"), config.Llm.BaseUrl);
    CHECK_EQ(std::string("test-model"), config.Llm.Model);
    CHECK_EQ(std::string("secret-key"), config.Llm.ApiKey);
    CHECK_EQ(std::string("test-agent"), config.Llm.UserAgent);
    CHECK_EQ(30, config.Llm.TimeoutSeconds);
    CHECK_EQ(1000, config.Llm.MaxTokens);
    CHECK_NEAR(0.5, config.Llm.Temperature, 1e-9);
    CHECK_EQ(2, config.Llm.MaxRetries);
    CHECK(config.Llm.Required);
    CHECK_EQ(2U, config.App.MaxOptions);
    CHECK_EQ(2048U, config.App.MaxInputBytes);
    CHECK(config.App.Log == ba::LogLevel::Debug);
}

TEST_CASE(ConfigAppliesDefaultsToPartialFile) {
    CleanEnvironment environment;
    const auto config = LoadJson({{"llm", {{"mode", "off"}}}});
    CHECK(config.Llm.Mode == ba::LlmMode::Off);
    CHECK_EQ(std::string("google/gemma-4-31B-it"), config.Llm.Model);
    CHECK_EQ(3U, config.App.MaxOptions);
}

TEST_CASE(ConfigParsesModeAliasesAndLogLevels) {
    CleanEnvironment environment;
    CHECK(LoadJson({{"llm", {{"mode", "NONE"}}}}).Llm.Mode == ba::LlmMode::Off);
    CHECK(LoadJson({{"llm", {{"mode", "Disabled"}}}}).Llm.Mode == ba::LlmMode::Off);
    CHECK(LoadJson({{"llm", {{"mode", "on"}}}}).Llm.Mode == ba::LlmMode::Always);
    CHECK(LoadJson({{"llm", {{"mode", "REQUIRED"}}}}).Llm.Mode == ba::LlmMode::Always);
    CHECK(LoadJson({{"app", {{"logLevel", "WARNING"}}}}).App.Log == ba::LogLevel::Warn);
    CHECK(LoadJson({{"app", {{"logLevel", "Error"}}}}).App.Log == ba::LogLevel::Error);
    CHECK(LoadJson({{"app", {{"logLevel", "info"}}}}).App.Log == ba::LogLevel::Info);
}

TEST_CASE(ConfigEnvironmentOverridesFile) {
    CleanEnvironment environment;
    environment.BaseUrl.Set("https://env.test/v1");
    environment.Model.Set("env-model");
    environment.ApiKey.Set("env-key");
    environment.UserAgent.Set("env-agent");

    const auto config = LoadJson(
        {{"llm", {{"baseUrl", "https://file.test/v1"}, {"model", "file-model"},
                  {"apiKey", "file-key"}, {"userAgent", "file-agent"}}}});
    CHECK_EQ(std::string("https://env.test/v1"), config.Llm.BaseUrl);
    CHECK_EQ(std::string("env-model"), config.Llm.Model);
    CHECK_EQ(std::string("env-key"), config.Llm.ApiKey);
    CHECK_EQ(std::string("env-agent"), config.Llm.UserAgent);
}

TEST_CASE(ConfigEmptyEnvironmentValuesDoNotOverride) {
    CleanEnvironment environment;
    environment.BaseUrl.Set("");
    environment.ApiKey.Set("");

    const auto config = LoadJson({{"llm", {{"apiKey", "file-key"}}}});
    CHECK_EQ(std::string("file-key"), config.Llm.ApiKey);
    CHECK(config.Llm.BaseUrl.rfind("https://", 0) == 0);
}

TEST_CASE(ConfigRejectsUnknownFields) {
    CleanEnvironment environment;
    CHECK_THROWS_AS(LoadJson({{"unknown", 1}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"llm", {{"unknown", 1}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"app", {{"unknown", 1}}}}), ba::AppError);
}

TEST_CASE(ConfigRejectsInvalidTypes) {
    CleanEnvironment environment;
    CHECK_THROWS_AS(LoadJson({{"llm", {{"mode", 1}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"llm", {{"baseUrl", true}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"llm", {{"timeoutSeconds", "30"}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"llm", {{"temperature", "hot"}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"llm", {{"required", 1}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"app", {{"maxOptions", 1.5}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"app", {{"logLevel", 3}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson(nlohmann::json::array()), ba::AppError);
}

TEST_CASE(ConfigRejectsInvalidValues) {
    CleanEnvironment environment;
    CHECK_THROWS_AS(LoadJson({{"llm", {{"mode", "sometimes"}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"llm", {{"baseUrl", "ftp://example.test"}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"llm", {{"baseUrl", "https://exa mple.test"}}}}),
                    ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"llm", {{"baseUrl", ""}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"llm", {{"model", ""}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"llm", {{"userAgent", ""}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"llm", {{"timeoutSeconds", 0}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"llm", {{"timeoutSeconds", 91}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"llm", {{"maxTokens", 0}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"llm", {{"maxTokens", 2001}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"llm", {{"temperature", -0.1}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"llm", {{"temperature", 2.1}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"llm", {{"maxRetries", -1}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"llm", {{"maxRetries", 6}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"app", {{"maxOptions", 0}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"app", {{"maxOptions", 4}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"app", {{"maxInputBytes", 1023}}}}), ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"app", {{"maxInputBytes", 64U * 1024U * 1024U + 1}}}}),
                    ba::AppError);
    CHECK_THROWS_AS(LoadJson({{"app", {{"logLevel", "verbose"}}}}), ba::AppError);
}

TEST_CASE(ConfigRejectsMalformedAndMissingFiles) {
    CleanEnvironment environment;
    const testdata::TempFile broken("config.json", "{oops");
    CHECK_THROWS_AS(ba::LoadConfig(broken.String()), ba::AppError);
    CHECK_THROWS_AS(ba::LoadConfig("no-such-config-file.json"), ba::AppError);
}

TEST_CASE(ConfigDescriptionRedactsApiKey) {
    CleanEnvironment environment;
    auto config = ba::DefaultConfig();
    config.Llm.ApiKey = "super-secret-key";
    const auto description = ba::DescribeConfig(config);
    CHECK(description.find("super-secret-key") == std::string::npos);
    CHECK(description.find("\"apiKeyConfigured\":true") != std::string::npos);
    CHECK(description.find("google/gemma-4-31B-it") != std::string::npos);
}
