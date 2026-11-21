#include "ba/services/LlmClient.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "ba/utils/Errors.hpp"
#include "ba/utils/RussianText.hpp"

namespace ba {

namespace {

constexpr std::size_t MaxMessagesBytes = 32768;
constexpr std::size_t MaxRequestBytes = 65536;
constexpr std::size_t MaxSchemaBytes = 16384;
constexpr int MaxTotalSeconds = 100;

std::string JoinUrl(const std::string& baseUrl) {
    std::string result = baseUrl;
    while (!result.empty() && result.back() == '/') {
        result.pop_back();
    }
    return result + "/chat/completions";
}

std::string Shorten(const std::string& text, std::size_t limit) {
    if (text.size() <= limit) {
        return text;
    }
    return text.substr(0, limit) + "...";
}

std::string ExtractErrorMessage(const std::string& body) {
    try {
        const auto root = nlohmann::json::parse(body);
        if (root.is_object() && root.contains("error") && root["error"].is_object()) {
            const auto& error = root["error"];
            std::string message;
            if (error.contains("code") && error["code"].is_string()) {
                message += error["code"].get<std::string>();
            }
            if (error.contains("message") && error["message"].is_string()) {
                if (!message.empty()) {
                    message += ": ";
                }
                message += error["message"].get<std::string>();
            }
            if (!message.empty()) {
                return Shorten(message, 300);
            }
        }
    } catch (const nlohmann::json::exception&) {
        // Fall through to the raw text.
    }
    return Shorten(text::SanitizeText(body, 300), 300);
}

bool RetryableStatus(long status) {
    return status == 429 || status >= 500;
}

} // namespace

LlmClient::LlmClient(LlmConfig config, std::shared_ptr<IHttpTransport> transport)
    : Config_(std::move(config)), Transport_(std::move(transport)) {}

nlohmann::json LlmClient::RequestJson(const std::string& systemPrompt,
                                      const std::string& userPrompt,
                                      const nlohmann::json& schema,
                                      const std::string& schemaName) const {
    if (Config_.ApiKey.empty()) {
        throw AppError(ErrorKind::Configuration,
                       "LLM_API_KEY не задан, вызов модели невозможен");
    }
    if (Transport_ == nullptr) {
        throw AppError(ErrorKind::ExternalService, "HTTP-транспорт для вызова модели не создан");
    }
    if (systemPrompt.size() + userPrompt.size() > MaxMessagesBytes) {
        throw AppError(ErrorKind::Configuration,
                       "Тексты сообщений модели превышают 32 768 байт");
    }
    if (schema.dump().size() > MaxSchemaBytes) {
        throw AppError(ErrorKind::Configuration, "JSON-схема модели превышает 16 384 байта");
    }

    nlohmann::json body;
    body["model"] = Config_.Model;
    body["messages"] = nlohmann::json::array(
        {nlohmann::json{{"role", "system"}, {"content", systemPrompt}},
         nlohmann::json{{"role", "user"}, {"content", userPrompt}}});
    body["max_tokens"] = Config_.MaxTokens;
    body["temperature"] = Config_.Temperature;
    body["stream"] = false;
    body["n"] = 1;
    body["reasoning"] = nlohmann::json{{"enabled", false}};
    body["response_format"] = nlohmann::json{
        {"type", "json_schema"},
        {"json_schema", nlohmann::json{{"name", schemaName},
                                       {"strict", true},
                                       {"schema", schema}}}};

    const auto serialized = body.dump();
    if (serialized.size() > MaxRequestBytes) {
        throw AppError(ErrorKind::Configuration,
                       "JSON-запрос к модели превышает 65 536 байт");
    }

    const auto start = std::chrono::steady_clock::now();
    const int attempts = std::max(1, Config_.MaxRetries + 1);

    for (int attempt = 0; attempt < attempts; ++attempt) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::steady_clock::now() - start)
                                 .count();
        if (elapsed >= MaxTotalSeconds) {
            throw AppError(ErrorKind::ExternalService,
                           "Общее время ожидания ответа модели превышено");
        }

        HttpRequest request;
        request.Url = JoinUrl(Config_.BaseUrl);
        request.Body = serialized;
        request.UserAgent = Config_.UserAgent;
        request.Authorization = "Bearer " + Config_.ApiKey;
        request.TimeoutSeconds = std::min<int>(
            Config_.TimeoutSeconds, static_cast<int>(MaxTotalSeconds - elapsed));

        HttpResponse response;
        try {
            response = Transport_->Send(request);
        } catch (const AppError& error) {
            if (error.Kind() == ErrorKind::Configuration) {
                throw;
            }
            if (attempt + 1 >= attempts) {
                throw;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(700 * (attempt + 1)));
            continue;
        }

        if (response.StatusCode == 200) {
            nlohmann::json envelope;
            try {
                envelope = nlohmann::json::parse(response.Body);
            } catch (const nlohmann::json::exception&) {
                throw AppError(ErrorKind::ExternalService,
                               "Модель вернула ответ, который не является JSON");
            }
            if (!envelope.is_object() || !envelope.contains("choices") ||
                !envelope["choices"].is_array() || envelope["choices"].empty()) {
                throw AppError(ErrorKind::ExternalService,
                               "В ответе модели отсутствует choices[0]");
            }
            const auto& choice = envelope["choices"].front();
            if (!choice.is_object() || !choice.contains("message") ||
                !choice["message"].is_object() ||
                !choice["message"].contains("content") ||
                !choice["message"]["content"].is_string()) {
                throw AppError(ErrorKind::ExternalService,
                               "В ответе модели отсутствует message.content");
            }
            if (choice.contains("finish_reason") && choice["finish_reason"].is_string() &&
                choice["finish_reason"].get<std::string>() != "stop") {
                throw AppError(ErrorKind::ExternalService,
                               "Модель не завершила ответ (finish_reason != stop)");
            }
            const auto content = choice["message"]["content"].get<std::string>();
            if (content.empty()) {
                throw AppError(ErrorKind::ExternalService, "Модель вернула пустой ответ");
            }
            nlohmann::json parsed;
            try {
                parsed = nlohmann::json::parse(content);
            } catch (const nlohmann::json::exception&) {
                throw AppError(ErrorKind::ExternalService,
                               "Содержимое ответа модели не является корректным JSON");
            }
            if (!parsed.is_object()) {
                throw AppError(ErrorKind::ExternalService,
                               "Содержимое ответа модели должно быть JSON-объектом");
            }
            return parsed;
        }

        if (response.StatusCode == 401) {
            throw AppError(ErrorKind::ExternalService,
                           "Модель отклонила ключ доступа (HTTP 401)");
        }
        if (response.StatusCode == 403) {
            throw AppError(ErrorKind::ExternalService,
                           "Доступ к модели запрещён или истёк (HTTP 403)");
        }
        if (!RetryableStatus(response.StatusCode) || attempt + 1 >= attempts) {
            throw AppError(ErrorKind::ExternalService,
                           "HTTP " + std::to_string(response.StatusCode) + " от модели: " +
                               ExtractErrorMessage(response.Body));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(700 * (attempt + 1)));
    }

    throw AppError(ErrorKind::ExternalService, "Не удалось получить ответ модели");
}

} // namespace ba
