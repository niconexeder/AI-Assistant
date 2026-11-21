#include "ba/services/Config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "ba/utils/Errors.hpp"
#include "ba/utils/JsonIO.hpp"

namespace ba {

namespace {

void RequireExactKeys(const nlohmann::json& object,
                      const std::set<std::string>& allowed,
                      const std::string& context) {
    if (!object.is_object()) {
        throw AppError(ErrorKind::Configuration,
                       "Секция '" + context + "' должна быть JSON-объектом");
    }
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (allowed.count(it.key()) == 0) {
            throw AppError(ErrorKind::Configuration,
                           "Неизвестное поле '" + it.key() + "' в конфигурации (" + context +
                               ")");
        }
    }
}

std::string ReadString(const nlohmann::json& object,
                       const char* key,
                       const std::string& context,
                       bool allowEmpty) {
    if (!object.contains(key)) {
        return {};
    }
    if (!object[key].is_string()) {
        throw AppError(ErrorKind::Configuration,
                       "Поле '" + std::string(key) + "' должно быть строкой (" + context + ")");
    }
    const auto value = object[key].get<std::string>();
    if (!allowEmpty && value.empty()) {
        throw AppError(ErrorKind::Configuration,
                       "Поле '" + std::string(key) + "' не может быть пустым (" + context + ")");
    }
    return value;
}

std::int64_t ReadInteger(const nlohmann::json& object,
                         const char* key,
                         std::int64_t fallback,
                         const std::string& context) {
    if (!object.contains(key)) {
        return fallback;
    }
    if (!object[key].is_number_integer() && !object[key].is_number_unsigned()) {
        throw AppError(ErrorKind::Configuration,
                       "Поле '" + std::string(key) + "' должно быть целым числом (" + context +
                           ")");
    }
    return object[key].get<std::int64_t>();
}

bool ReadBoolean(const nlohmann::json& object,
                 const char* key,
                 bool fallback,
                 const std::string& context) {
    if (!object.contains(key)) {
        return fallback;
    }
    if (!object[key].is_boolean()) {
        throw AppError(ErrorKind::Configuration,
                       "Поле '" + std::string(key) + "' должно быть boolean (" + context + ")");
    }
    return object[key].get<bool>();
}

double ReadNumber(const nlohmann::json& object,
                  const char* key,
                  double fallback,
                  const std::string& context) {
    if (!object.contains(key)) {
        return fallback;
    }
    if (!object[key].is_number()) {
        throw AppError(ErrorKind::Configuration,
                       "Поле '" + std::string(key) + "' должно быть числом (" + context + ")");
    }
    return object[key].get<double>();
}

std::string UpperAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

LlmMode ParseMode(const std::string& value) {
    const auto upper = UpperAscii(value);
    if (upper == "OFF" || upper == "NONE" || upper == "DISABLED") {
        return LlmMode::Off;
    }
    if (upper == "AUTO") {
        return LlmMode::Auto;
    }
    if (upper == "ALWAYS" || upper == "ON" || upper == "REQUIRED") {
        return LlmMode::Always;
    }
    throw AppError(ErrorKind::Configuration,
                   "llm.mode должен быть одним из: off, auto, always");
}

LogLevel ParseLogLevel(const std::string& value) {
    const auto upper = UpperAscii(value);
    if (upper == "ERROR") {
        return LogLevel::Error;
    }
    if (upper == "WARN" || upper == "WARNING") {
        return LogLevel::Warn;
    }
    if (upper == "INFO") {
        return LogLevel::Info;
    }
    if (upper == "DEBUG") {
        return LogLevel::Debug;
    }
    throw AppError(ErrorKind::Configuration,
                   "app.logLevel должен быть одним из: error, warn, info, debug");
}

std::string ModeName(LlmMode mode) {
    switch (mode) {
    case LlmMode::Off:
        return "off";
    case LlmMode::Auto:
        return "auto";
    case LlmMode::Always:
        return "always";
    }
    return "auto";
}

std::string LogLevelName(LogLevel level) {
    switch (level) {
    case LogLevel::Error:
        return "error";
    case LogLevel::Warn:
        return "warn";
    case LogLevel::Info:
        return "info";
    case LogLevel::Debug:
        return "debug";
    }
    return "info";
}

void RequireSingleLine(const std::string& value, const char* field) {
    for (const unsigned char ch : value) {
        if (ch < 0x20 || ch == 0x7F) {
            throw AppError(ErrorKind::Configuration,
                           std::string("Поле '") + field + "' содержит управляющие символы");
        }
    }
}

void Validate(const Config& config) {
    const auto& llm = config.Llm;
    if (llm.BaseUrl.rfind("http://", 0) != 0 && llm.BaseUrl.rfind("https://", 0) != 0) {
        throw AppError(ErrorKind::Configuration,
                       "llm.baseUrl должен начинаться с http:// или https://");
    }
    RequireSingleLine(llm.BaseUrl, "llm.baseUrl");
    for (const unsigned char ch : llm.BaseUrl) {
        if (std::isspace(ch) != 0) {
            throw AppError(ErrorKind::Configuration, "llm.baseUrl не должен содержать пробелы");
        }
    }
    RequireSingleLine(llm.Model, "llm.model");
    if (llm.Model.empty()) {
        throw AppError(ErrorKind::Configuration, "llm.model не может быть пустым");
    }
    RequireSingleLine(llm.UserAgent, "llm.userAgent");
    if (llm.UserAgent.empty() || llm.UserAgent.size() > 200) {
        throw AppError(ErrorKind::Configuration,
                       "llm.userAgent должен содержать от 1 до 200 символов");
    }
    RequireSingleLine(llm.ApiKey, "llm.apiKey");
    if (llm.TimeoutSeconds < 1 || llm.TimeoutSeconds > 90) {
        throw AppError(ErrorKind::Configuration, "llm.timeoutSeconds должен быть от 1 до 90");
    }
    if (llm.MaxTokens < 1 || llm.MaxTokens > 2000) {
        throw AppError(ErrorKind::Configuration, "llm.maxTokens должен быть от 1 до 2000");
    }
    if (llm.Temperature < 0.0 || llm.Temperature > 2.0) {
        throw AppError(ErrorKind::Configuration, "llm.temperature должен быть от 0 до 2");
    }
    if (llm.MaxRetries < 0 || llm.MaxRetries > 5) {
        throw AppError(ErrorKind::Configuration, "llm.maxRetries должен быть от 0 до 5");
    }
    if (config.App.MaxOptions < 1 || config.App.MaxOptions > 3) {
        throw AppError(ErrorKind::Configuration, "app.maxOptions должен быть от 1 до 3");
    }
    if (config.App.MaxInputBytes < 1024 || config.App.MaxInputBytes > 64U * 1024U * 1024U) {
        throw AppError(ErrorKind::Configuration,
                       "app.maxInputBytes должен быть от 1024 до 67108864");
    }
}

std::string EnvOrEmpty(const char* name) {
#ifdef _WIN32
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) == 0 && buffer != nullptr) {
        std::string value(buffer);
        std::free(buffer);
        return value;
    }
    return {};
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
#endif
}

} // namespace

Config DefaultConfig() {
    return Config{};
}

Config LoadConfig(const std::string& path) {
    Config config;

    if (!path.empty()) {
        std::ifstream probe(path);
        if (!probe) {
            throw AppError(ErrorKind::Configuration, "Не удалось открыть конфигурацию: " + path);
        }
        probe.close();

        nlohmann::json root;
        try {
            root = jsonio::ParseJson(jsonio::ReadFile(path, 1024U * 1024U),
                                     "конфигурации " + path);
        } catch (const AppError& error) {
            throw AppError(ErrorKind::Configuration, error.what());
        }
        if (!root.is_object()) {
            throw AppError(ErrorKind::Configuration,
                           "Конфигурация должна быть JSON-объектом: " + path);
        }
        RequireExactKeys(root, {"llm", "app"}, "корень");

        if (root.contains("llm")) {
            const auto& llm = root["llm"];
            RequireExactKeys(llm,
                             {"mode", "baseUrl", "model", "apiKey", "userAgent",
                              "timeoutSeconds", "maxTokens", "temperature", "maxRetries",
                              "required"},
                             "llm");
            if (llm.contains("mode")) {
                config.Llm.Mode = ParseMode(ReadString(llm, "mode", "llm", false));
            }
            if (llm.contains("baseUrl")) {
                config.Llm.BaseUrl = ReadString(llm, "baseUrl", "llm", false);
            }
            if (llm.contains("model")) {
                config.Llm.Model = ReadString(llm, "model", "llm", false);
            }
            if (llm.contains("apiKey")) {
                config.Llm.ApiKey = ReadString(llm, "apiKey", "llm", true);
            }
            if (llm.contains("userAgent")) {
                config.Llm.UserAgent = ReadString(llm, "userAgent", "llm", false);
            }
            config.Llm.TimeoutSeconds = static_cast<int>(
                ReadInteger(llm, "timeoutSeconds", config.Llm.TimeoutSeconds, "llm"));
            config.Llm.MaxTokens = static_cast<int>(
                ReadInteger(llm, "maxTokens", config.Llm.MaxTokens, "llm"));
            config.Llm.Temperature =
                ReadNumber(llm, "temperature", config.Llm.Temperature, "llm");
            config.Llm.MaxRetries = static_cast<int>(
                ReadInteger(llm, "maxRetries", config.Llm.MaxRetries, "llm"));
            config.Llm.Required = ReadBoolean(llm, "required", config.Llm.Required, "llm");
        }

        if (root.contains("app")) {
            const auto& app = root["app"];
            RequireExactKeys(app, {"maxOptions", "maxInputBytes", "logLevel"}, "app");
            config.App.MaxOptions = static_cast<std::size_t>(
                ReadInteger(app, "maxOptions", static_cast<std::int64_t>(config.App.MaxOptions),
                            "app"));
            config.App.MaxInputBytes = static_cast<std::size_t>(
                ReadInteger(app, "maxInputBytes",
                            static_cast<std::int64_t>(config.App.MaxInputBytes), "app"));
            if (app.contains("logLevel")) {
                config.App.Log = ParseLogLevel(ReadString(app, "logLevel", "app", false));
            }
        }
    }

    const auto baseUrl = EnvOrEmpty("LLM_BASE_URL");
    const auto model = EnvOrEmpty("LLM_MODEL");
    const auto key = EnvOrEmpty("LLM_API_KEY");
    const auto userAgent = EnvOrEmpty("LLM_USER_AGENT");
    if (!baseUrl.empty()) {
        config.Llm.BaseUrl = baseUrl;
    }
    if (!model.empty()) {
        config.Llm.Model = model;
    }
    if (!key.empty()) {
        config.Llm.ApiKey = key;
    }
    if (!userAgent.empty()) {
        config.Llm.UserAgent = userAgent;
    }

    Validate(config);
    return config;
}

std::string DescribeConfig(const Config& config) {
    nlohmann::json root;
    root["llm"]["mode"] = ModeName(config.Llm.Mode);
    root["llm"]["baseUrl"] = config.Llm.BaseUrl;
    root["llm"]["model"] = config.Llm.Model;
    root["llm"]["apiKeyConfigured"] = !config.Llm.ApiKey.empty();
    root["llm"]["userAgent"] = config.Llm.UserAgent;
    root["llm"]["timeoutSeconds"] = config.Llm.TimeoutSeconds;
    root["llm"]["maxTokens"] = config.Llm.MaxTokens;
    root["llm"]["temperature"] = config.Llm.Temperature;
    root["llm"]["maxRetries"] = config.Llm.MaxRetries;
    root["llm"]["required"] = config.Llm.Required;
    root["app"]["maxOptions"] = config.App.MaxOptions;
    root["app"]["maxInputBytes"] = config.App.MaxInputBytes;
    root["app"]["logLevel"] = LogLevelName(config.App.Log);
    return root.dump();
}

} // namespace ba
