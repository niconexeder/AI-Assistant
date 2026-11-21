#pragma once

#include <cstddef>
#include <string>

#include "ba/utils/Logger.hpp"

namespace ba {

enum class LlmMode {
    Off,
    Auto,
    Always
};

struct LlmConfig {
    LlmMode Mode = LlmMode::Auto;
    std::string BaseUrl =
        "https://sfedu-hackathon-ai-gateway.sfedu-hackathon-ai-gateway.workers.dev/v1";
    std::string Model = "google/gemma-4-31B-it";
    std::string ApiKey;
    std::string UserAgent = "buyer-assistant/1.0";
    int TimeoutSeconds = 20;
    int MaxTokens = 512;
    double Temperature = 0.0;
    int MaxRetries = 1;
    bool Required = false;
};

struct AppConfig {
    std::size_t MaxOptions = 3;
    std::size_t MaxInputBytes = 1024U * 1024U;
    LogLevel Log = LogLevel::Info;
};

struct Config {
    LlmConfig Llm;
    AppConfig App;
};

Config DefaultConfig();

// Loads an optional JSON configuration file and applies LLM_* environment
// overrides. Passing an empty path means "defaults plus environment".
Config LoadConfig(const std::string& path);

// Redacted JSON description safe for logs (the API key is never included).
std::string DescribeConfig(const Config& config);

} // namespace ba
