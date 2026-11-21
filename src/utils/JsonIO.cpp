#include "ba/utils/JsonIO.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include "ba/utils/Errors.hpp"

namespace ba::jsonio {

std::string ReadAll(std::istream& input, std::size_t maxBytes) {
    std::string result;
    char buffer[8192];
    while (input.good()) {
        input.read(buffer, static_cast<std::streamsize>(sizeof(buffer)));
        const auto count = input.gcount();
        if (count <= 0) {
            break;
        }
        if (result.size() + static_cast<std::size_t>(count) > maxBytes) {
            throw AppError(ErrorKind::Input, "Входной JSON превышает допустимый размер");
        }
        result.append(buffer, static_cast<std::size_t>(count));
    }
    if (input.bad()) {
        throw AppError(ErrorKind::Input, "Не удалось прочитать входной поток");
    }
    return result;
}

std::string ReadFile(const std::string& path, std::size_t maxBytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw AppError(ErrorKind::Input, "Не удалось открыть файл: " + path);
    }
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size < 0 || static_cast<std::uintmax_t>(size) > maxBytes) {
        throw AppError(ErrorKind::Input, "Файл слишком большой: " + path);
    }
    file.seekg(0, std::ios::beg);
    std::string result(static_cast<std::size_t>(size), '\0');
    if (!result.empty()) {
        file.read(result.data(), static_cast<std::streamsize>(result.size()));
        if (!file) {
            throw AppError(ErrorKind::Input, "Не удалось прочитать файл: " + path);
        }
    }
    if (result.size() >= 3 &&
        static_cast<unsigned char>(result[0]) == 0xEF &&
        static_cast<unsigned char>(result[1]) == 0xBB &&
        static_cast<unsigned char>(result[2]) == 0xBF) {
        result.erase(0, 3);
    }
    return result;
}

nlohmann::json ParseJson(const std::string& text, const std::string& sourceName) {
    if (text.empty()) {
        throw AppError(ErrorKind::Input, "Пустой JSON: " + sourceName);
    }
    try {
        return nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error& error) {
        throw AppError(ErrorKind::Input,
                       "Некорректный JSON в " + sourceName + ": " + error.what());
    }
}

nlohmann::json ParseObject(const std::string& text, const std::string& sourceName) {
    auto value = ParseJson(text, sourceName);
    if (!value.is_object()) {
        throw AppError(ErrorKind::Input, "Ожидался JSON-объект: " + sourceName);
    }
    return value;
}

} // namespace ba::jsonio
