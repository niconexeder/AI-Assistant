#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ba::text {

std::string ToLowerUtf8(std::string_view text);
std::string Normalize(std::string_view text);
std::string Trim(std::string_view text);

std::vector<std::string> SplitWords(const std::string& normalizedText);
bool ContainsAny(std::string_view haystack, const std::vector<std::string_view>& needles);

std::string FormatInteger(std::int64_t value);
std::string FormatRub(std::int64_t value);
std::string JoinWithComma(const std::vector<std::string>& items);
std::string JoinWithAnd(const std::vector<std::string>& items);

std::size_t Utf8CodePointCount(std::string_view text);
std::string TruncateUtf8(std::string_view text, std::size_t maxCodePoints);
std::string SanitizeText(std::string_view text, std::size_t maxCodePoints);

} // namespace ba::text
