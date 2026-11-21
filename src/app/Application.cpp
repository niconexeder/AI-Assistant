#include "ba/app/Application.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "ba/core/Directories.hpp"
#include "ba/utils/Errors.hpp"
#include "ba/utils/RussianText.hpp"

namespace ba {

namespace {

CityDirectory DefaultCities() {
    return CityDirectory(
        {City{"taganrog", "Таганрог"},
         City{"rostov-na-donu", "Ростов-на-Дону"},
         City{"azov", "Азов"}});
}

CategoryDirectory DefaultCategories() {
    return CategoryDirectory({CategoryInfo{Category::Table, "Стол"},
                              CategoryInfo{Category::Chair, "Кресло"},
                              CategoryInfo{Category::Lamp, "Лампа"}});
}

CityDirectory LoadCitiesBeside(const std::string& catalogPath) {
    const auto path =
        std::filesystem::path(catalogPath).parent_path() / "cities.json";
    if (std::filesystem::exists(path)) {
        return LoadCityDirectory(path.string());
    }
    return DefaultCities();
}

CategoryDirectory LoadCategoriesBeside(const std::string& catalogPath) {
    const auto path =
        std::filesystem::path(catalogPath).parent_path() / "categories.json";
    if (std::filesystem::exists(path)) {
        return LoadCategoryDirectory(path.string());
    }
    return DefaultCategories();
}

LogLevel EffectiveLogLevel(const Config& config, bool dumpRequirements) {
    if (dumpRequirements) {
        return static_cast<LogLevel>(
            std::max(static_cast<int>(config.App.Log), static_cast<int>(LogLevel::Info)));
    }
    return config.App.Log;
}

std::shared_ptr<IHttpTransport> ResolveTransport(
    const Config& config,
    bool disableLlm,
    const std::shared_ptr<IHttpTransport>& provided) {
    if (disableLlm || config.Llm.Mode == LlmMode::Off || config.Llm.ApiKey.empty()) {
        return provided;
    }
    if (provided != nullptr) {
        return provided;
    }
    return CreateHttpTransport();
}

nlohmann::json BoundJson(const NumericBound& bound) {
    return nlohmann::json{{"value", bound.Value}, {"inclusive", bound.Inclusive}};
}

nlohmann::json ConstraintJson(const std::optional<NumericConstraint>& constraint) {
    if (!constraint.has_value()) {
        return nullptr;
    }
    nlohmann::json result;
    if (constraint->Min.has_value()) {
        result["min"] = BoundJson(*constraint->Min);
    }
    if (constraint->Max.has_value()) {
        result["max"] = BoundJson(*constraint->Max);
    }
    return result;
}

nlohmann::json RequirementsJson(const Requirements& requirements) {
    nlohmann::json result;
    result["city_id"] = requirements.CityId.has_value()
                            ? nlohmann::json(*requirements.CityId)
                            : nlohmann::json(nullptr);
    result["city_invalid"] = requirements.CityInvalid;
    result["budget_rub"] = requirements.BudgetRub.has_value()
                               ? nlohmann::json(*requirements.BudgetRub)
                               : nlohmann::json(nullptr);
    result["budget_ambiguous"] = requirements.BudgetAmbiguous;
    result["categories"] = nlohmann::json::array();
    for (const auto category : requirements.Categories) {
        result["categories"].push_back(std::string(CategoryId(category)));
    }
    result["categories_invalid"] = requirements.CategoriesInvalid;
    result["unsupported_categories"] = requirements.UnsupportedCategories;
    result["table_width"] = ConstraintJson(requirements.TableWidth);
    result["chair_adjustable_height"] =
        requirements.ChairAdjustableHeight.has_value()
            ? nlohmann::json(*requirements.ChairAdjustableHeight)
            : nlohmann::json(nullptr);
    result["lamp_color_temperature"] =
        ConstraintJson(requirements.LampColorTemperature);
    return result;
}

bool HasVagueCue(const std::vector<Message>& messages) {
    std::string text;
    for (const auto& message : messages) {
        if (message.Speaker == Role::User) {
            text += text::Normalize(message.Text);
            text += ' ';
        }
    }
    return text::ContainsAny(
        text, {"недорог", "подешевл", "дешев", "бюджетн", "компакт", "узк", "тепл",
               "холодн", "нейтральн", "удобн", "эргоном", "примерно", "около", "где-то",
               "подходящ", "нечто", "что-нибудь", "какой-нибудь"});
}

} // namespace

Application::Application(const std::string& catalogPath,
                         Config config,
                         bool disableLlm,
                         bool dumpRequirements,
                         std::shared_ptr<IHttpTransport> transport)
    : Cities_(LoadCitiesBeside(catalogPath)),
      Categories_(LoadCategoriesBeside(catalogPath)),
      Catalog_(LoadCatalog(catalogPath, Cities_)),
      Analyzer_(Cities_, Categories_),
      Search_(Catalog_, Categories_),
      Decision_(Search_, Cities_, config.App.MaxOptions),
      Config_(std::move(config)),
      DisableLlm_(disableLlm),
      DumpRequirements_(dumpRequirements),
      Extractor_(Config_.Llm, Cities_, Categories_,
                 ResolveTransport(Config_, DisableLlm_, transport)),
      Logger_(std::cerr, EffectiveLogLevel(Config_, dumpRequirements)) {
    const auto mode = EffectiveMode();
    if (mode == LlmMode::Always && Config_.Llm.ApiKey.empty()) {
        throw AppError(ErrorKind::Configuration,
                       "llm.mode=always требует заданного LLM_API_KEY");
    }
    if (mode != LlmMode::Off && Config_.Llm.Required && Config_.Llm.ApiKey.empty()) {
        throw AppError(ErrorKind::Configuration,
                       "llm.required=true, но LLM_API_KEY не задан");
    }
    Logger_.Debug("Конфигурация: " + DescribeConfig(Config_));
}

LlmMode Application::EffectiveMode() const noexcept {
    if (DisableLlm_) {
        return LlmMode::Off;
    }
    return Config_.Llm.Mode;
}

std::vector<Message> Application::ParseMessages(const nlohmann::json& request,
                                                std::size_t maxInputBytes) {
    if (!request.is_object()) {
        throw AppError(ErrorKind::Input, "Запрос должен быть JSON-объектом");
    }
    for (auto it = request.begin(); it != request.end(); ++it) {
        if (it.key() != "messages") {
            throw AppError(ErrorKind::Input,
                           "Неизвестное поле запроса: " + it.key());
        }
    }
    if (!request.contains("messages") || !request["messages"].is_array()) {
        throw AppError(ErrorKind::Input, "Поле 'messages' должно быть массивом");
    }
    const auto& rawMessages = request["messages"];
    if (rawMessages.empty() || rawMessages.size() > 20) {
        throw AppError(ErrorKind::Input,
                       "В запросе должно быть от 1 до 20 сообщений");
    }
    if (request.dump().size() > maxInputBytes) {
        throw AppError(ErrorKind::Input, "Запрос превышает допустимый размер");
    }

    std::vector<Message> messages;
    messages.reserve(rawMessages.size());
    for (std::size_t i = 0; i < rawMessages.size(); ++i) {
        const auto& item = rawMessages[i];
        if (!item.is_object()) {
            throw AppError(ErrorKind::Input,
                           "Сообщение " + std::to_string(i + 1) + " должно быть объектом");
        }
        for (auto it = item.begin(); it != item.end(); ++it) {
            if (it.key() != "role" && it.key() != "text") {
                throw AppError(ErrorKind::Input,
                               "Неизвестное поле сообщения: " + it.key());
            }
        }
        if (!item.contains("role") || !item["role"].is_string()) {
            throw AppError(ErrorKind::Input,
                           "У сообщения " + std::to_string(i + 1) +
                               " отсутствует строковое поле 'role'");
        }
        if (!item.contains("text") || !item["text"].is_string()) {
            throw AppError(ErrorKind::Input,
                           "У сообщения " + std::to_string(i + 1) +
                               " отсутствует строковое поле 'text'");
        }
        const auto role = item["role"].get<std::string>();
        if (role != "user" && role != "assistant") {
            throw AppError(ErrorKind::Input,
                           "Недопустимая роль сообщения: " + role);
        }
        const auto messageText = item["text"].get<std::string>();
        if (messageText.empty()) {
            throw AppError(ErrorKind::Input,
                           "Текст сообщения " + std::to_string(i + 1) + " пуст");
        }

        Message message;
        message.Speaker = role == "user" ? Role::User : Role::Assistant;
        message.Text = messageText;

        const auto expected = i % 2 == 0 ? Role::User : Role::Assistant;
        if (message.Speaker != expected) {
            throw AppError(ErrorKind::Input,
                           "Роли сообщений должны чередоваться, начиная с user");
        }
        messages.push_back(std::move(message));
    }

    if (messages.back().Speaker != Role::User) {
        throw AppError(ErrorKind::Input, "Последнее сообщение должно быть от user");
    }
    return messages;
}

bool Application::ShouldConsultLlm(const Requirements& requirements,
                                   const std::vector<Message>& messages) const {
    const auto mode = EffectiveMode();
    if (mode == LlmMode::Off || !Extractor_.Available()) {
        return false;
    }
    if (mode == LlmMode::Always) {
        return true;
    }
    if (requirements.CityInvalid || requirements.CategoriesInvalid ||
        requirements.BudgetAmbiguous) {
        return false;
    }
    if (!requirements.CityId.has_value() || !requirements.BudgetRub.has_value() ||
        requirements.Categories.empty()) {
        return true;
    }
    return HasVagueCue(messages);
}

Response Application::Handle(const nlohmann::json& request) {
    const auto messages = ParseMessages(request, Config_.App.MaxInputBytes);

    auto requirements = Analyzer_.Analyze(messages);

    if (ShouldConsultLlm(requirements, messages)) {
        Logger_.Debug("Обращаюсь к модели для дополнения условий");
        const auto facts = Extractor_.Extract(messages);
        std::string dialogueText;
        for (const auto& message : messages) {
            dialogueText += message.Text;
            dialogueText += '\n';
        }
        requirements = Extractor_.Merge(requirements, facts, dialogueText);
    }

    if (DumpRequirements_) {
        Logger_.Info("requirements: " + RequirementsJson(requirements).dump());
    }

    return Decision_.Decide(requirements);
}

} // namespace ba
