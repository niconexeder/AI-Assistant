#include "ba/cli/Arguments.hpp"

#include <algorithm>
#include <cctype>
#include <string>

#include "ba/utils/Errors.hpp"

namespace ba {

namespace {

std::string UpperAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

LogLevel ParseLevel(const std::string& value) {
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
    throw AppError(ErrorKind::Usage,
                   "Неизвестный уровень логирования: " + value);
}

std::string TakeValue(const std::vector<std::string>& arguments,
                      std::size_t& index,
                      const std::string& name) {
    if (index + 1 >= arguments.size()) {
        throw AppError(ErrorKind::Usage, "Для " + name + " требуется значение");
    }
    ++index;
    return arguments[index];
}

} // namespace

CliOptions ParseArguments(const std::vector<std::string>& arguments) {
    CliOptions options;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto& argument = arguments[i];
        if (argument == "--help" || argument == "-h") {
            options.ShowHelp = true;
        } else if (argument == "--version") {
            options.ShowVersion = true;
        } else if (argument == "--no-llm") {
            options.NoLlm = true;
        } else if (argument == "--dump-requirements") {
            options.DumpRequirements = true;
        } else if (argument == "--catalog") {
            options.CatalogPath = TakeValue(arguments, i, "--catalog");
        } else if (argument.rfind("--catalog=", 0) == 0) {
            options.CatalogPath = argument.substr(10);
        } else if (argument == "--config") {
            options.ConfigPath = TakeValue(arguments, i, "--config");
        } else if (argument.rfind("--config=", 0) == 0) {
            options.ConfigPath = argument.substr(9);
        } else if (argument == "--log-level") {
            options.LogLevelOverride = ParseLevel(TakeValue(arguments, i, "--log-level"));
        } else if (argument.rfind("--log-level=", 0) == 0) {
            options.LogLevelOverride = ParseLevel(argument.substr(12));
        } else {
            throw AppError(ErrorKind::Usage, "Неизвестный аргумент: " + argument);
        }
    }

    if (!options.ShowHelp && !options.ShowVersion && options.CatalogPath.empty()) {
        throw AppError(ErrorKind::Usage, "Не указан обязательный аргумент --catalog");
    }
    return options;
}

std::string UsageText() {
    return
        "Использование: run --catalog PATH [параметры]\n"
        "\n"
        "Читает один JSON-запрос из stdin и выводит JSON-ответ в stdout.\n"
        "\n"
        "Параметры:\n"
        "  --catalog PATH          путь к JSON-каталогу объявлений (обязательно)\n"
        "  --config PATH           путь к JSON-конфигурации\n"
        "  --no-llm                не обращаться к языковой модели\n"
        "  --dump-requirements     вывести разобранные условия в stderr\n"
        "  --log-level LEVEL       error, warn, info или debug\n"
        "  --help, -h              показать эту справку\n"
        "  --version               показать версию\n"
        "\n"
        "Переменные окружения: LLM_BASE_URL, LLM_MODEL, LLM_API_KEY, LLM_USER_AGENT.\n";
}

} // namespace ba
