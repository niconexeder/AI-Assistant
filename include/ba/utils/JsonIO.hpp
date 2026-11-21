#pragma once

#include <cstddef>
#include <istream>
#include <string>

#include <nlohmann/json.hpp>

namespace ba::jsonio {

std::string ReadAll(std::istream& input, std::size_t maxBytes);
std::string ReadFile(const std::string& path, std::size_t maxBytes);
nlohmann::json ParseObject(const std::string& text, const std::string& sourceName);
nlohmann::json ParseJson(const std::string& text, const std::string& sourceName);

} // namespace ba::jsonio
