#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include "ba/app/Application.hpp"
#include "ba/cli/Arguments.hpp"
#include "ba/services/Config.hpp"
#include "ba/utils/Errors.hpp"
#include "ba/utils/JsonIO.hpp"

namespace {

constexpr const char* kVersion = "buyer-assistant 1.0.0";

std::string StripUtf8Bom(std::string text) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return text;
}

#ifdef _WIN32
std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::vector<std::string> CollectArguments(int argc, char** argv) {
    std::vector<std::string> result;
    int wideCount = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &wideCount);
    if (wide != nullptr) {
        for (int i = 1; i < wideCount; ++i) {
            result.push_back(ToUtf8(wide[i]));
        }
        LocalFree(wide);
        return result;
    }
    for (int i = 1; i < argc; ++i) {
        result.emplace_back(argv[i]);
    }
    return result;
}

std::filesystem::path ExecutableDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD length =
        GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}
#else
std::vector<std::string> CollectArguments(int argc, char** argv) {
    std::vector<std::string> result;
    for (int i = 1; i < argc; ++i) {
        result.emplace_back(argv[i]);
    }
    return result;
}
#endif

std::string LocateDefaultConfig() {
    std::error_code error;
    const auto local = std::filesystem::current_path(error) / "config.json";
    if (!error && std::filesystem::is_regular_file(local, error)) {
        return local.string();
    }
#ifdef _WIN32
    error.clear();
    const auto beside = ExecutableDirectory() / "config.json";
    if (!error && !beside.empty() && std::filesystem::is_regular_file(beside, error)) {
        return beside.string();
    }
#endif
    return {};
}

std::string LoadConfigPath(const std::string& explicitPath) {
    if (!explicitPath.empty()) {
        return explicitPath;
    }
    return LocateDefaultConfig();
}

int Run(const std::vector<std::string>& arguments) {
    try {
        const auto options = ba::ParseArguments(arguments);
        if (options.ShowHelp) {
            std::cout << ba::UsageText();
            return 0;
        }
        if (options.ShowVersion) {
            std::cout << kVersion << '\n';
            return 0;
        }

        auto config = ba::LoadConfig(LoadConfigPath(options.ConfigPath));
        if (options.LogLevelOverride.has_value()) {
            config.App.Log = *options.LogLevelOverride;
        }
        if (options.NoLlm) {
            config.Llm.Mode = ba::LlmMode::Off;
        }

        ba::Application application(options.CatalogPath, config, options.NoLlm,
                                    options.DumpRequirements);

        const auto input = ba::jsonio::ReadAll(std::cin, config.App.MaxInputBytes);
        const auto request =
            ba::jsonio::ParseObject(StripUtf8Bom(input), "запросе из stdin");
        const auto response = application.Handle(request);

        std::cout << response.ToJson().dump(
                         2, ' ', false, nlohmann::json::error_handler_t::replace)
                  << '\n';
        std::cout.flush();
        return 0;
    } catch (const ba::AppError& error) {
        std::cerr << "[buyer-assistant] ERROR: " << error.what() << '\n';
        return ba::ExitCodeFor(error.Kind());
    } catch (const std::exception& error) {
        std::cerr << "[buyer-assistant] ERROR: " << error.what() << '\n';
        return ba::ExitCodeFor(ba::ErrorKind::Internal);
    } catch (...) {
        std::cerr << "[buyer-assistant] ERROR: неизвестная внутренняя ошибка\n";
        return ba::ExitCodeFor(ba::ErrorKind::Internal);
    }
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::ios::sync_with_stdio(false);
    return Run(CollectArguments(argc, argv));
}
