#include "ba/utils/RussianText.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace ba::text {

namespace {

struct CodePoint {
    char32_t Value = 0;
    std::size_t Length = 1;
};

CodePoint Decode(std::string_view text, std::size_t offset) {
    const auto byte = static_cast<unsigned char>(text[offset]);
    if (byte < 0x80) {
        return {byte, 1};
    }
    if ((byte & 0xE0) == 0xC0 && offset + 1 < text.size()) {
        return {static_cast<char32_t>(((byte & 0x1F) << 6) |
                                      (static_cast<unsigned char>(text[offset + 1]) & 0x3F)),
                2};
    }
    if ((byte & 0xF0) == 0xE0 && offset + 2 < text.size()) {
        return {static_cast<char32_t>(((byte & 0x0F) << 12) |
                                      ((static_cast<unsigned char>(text[offset + 1]) & 0x3F) << 6) |
                                      (static_cast<unsigned char>(text[offset + 2]) & 0x3F)),
                3};
    }
    if ((byte & 0xF8) == 0xF0 && offset + 3 < text.size()) {
        return {static_cast<char32_t>(((byte & 0x07) << 18) |
                                      ((static_cast<unsigned char>(text[offset + 1]) & 0x3F) << 12) |
                                      ((static_cast<unsigned char>(text[offset + 2]) & 0x3F) << 6) |
                                      (static_cast<unsigned char>(text[offset + 3]) & 0x3F)),
                4};
    }
    return {byte, 1};
}

void AppendCodePoint(std::string& out, char32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

char32_t LowerCodePoint(char32_t cp) {
    if (cp >= U'А' && cp <= U'Я') {
        return cp + 0x20;
    }
    if (cp == U'Ё') {
        return U'ё';
    }
    if (cp >= U'A' && cp <= U'Z') {
        return cp + ('a' - 'A');
    }
    return cp;
}

bool IsAsciiSpace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

std::string NormalizeWhitespace(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool pendingSpace = false;
    for (std::size_t i = 0; i < text.size();) {
        const auto cp = Decode(text, i);
        if (cp.Value == U' ' || cp.Value == U'\t' || cp.Value == U'\n' ||
            cp.Value == U'\r' || cp.Value == U'\f' || cp.Value == U'\v') {
            pendingSpace = !out.empty();
            i += cp.Length;
            continue;
        }
        if (pendingSpace) {
            out.push_back(' ');
            pendingSpace = false;
        }
        AppendCodePoint(out, cp.Value);
        i += cp.Length;
    }
    return out;
}

} // namespace

std::string ToLowerUtf8(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const auto cp = Decode(text, i);
        AppendCodePoint(out, LowerCodePoint(cp.Value));
        i += cp.Length;
    }
    return out;
}

std::string Normalize(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const auto cp = Decode(text, i);
        char32_t value = LowerCodePoint(cp.Value);
        if (value == U'ё') {
            value = U'е';
        }
        if ((value >= 0x2010 && value <= 0x2015) || value == 0x2212) {
            value = U'-';
        }
        if (value == 0x00A0 || value == 0x202F) {
            value = U' ';
        }
        if (value == 0x2026) {
            lowered.append("...");
            i += cp.Length;
            continue;
        }
        AppendCodePoint(lowered, value);
        i += cp.Length;
    }
    return Trim(NormalizeWhitespace(lowered));
}

std::string Trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && IsAsciiSpace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin && IsAsciiSpace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::vector<std::string> SplitWords(const std::string& normalizedText) {
    std::vector<std::string> words;
    std::string current;
    for (const unsigned char byte : normalizedText) {
        const bool wordByte = byte >= 0x80 || std::isalnum(byte) != 0;
        if (wordByte) {
            current.push_back(static_cast<char>(byte));
        } else if (!current.empty()) {
            words.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) {
        words.push_back(current);
    }
    return words;
}

bool ContainsAny(std::string_view haystack, const std::vector<std::string_view>& needles) {
    return std::any_of(needles.begin(), needles.end(), [&](std::string_view needle) {
        return !needle.empty() && haystack.find(needle) != std::string_view::npos;
    });
}

std::string FormatInteger(std::int64_t value) {
    const bool negative = value < 0;
    const auto magnitude = negative
        ? static_cast<std::uint64_t>(-(value + 1)) + 1U
        : static_cast<std::uint64_t>(value);
    std::string digits = std::to_string(magnitude);
    std::string grouped;
    grouped.reserve(digits.size() + digits.size() / 3 + 1);
    const std::size_t firstGroup = digits.size() % 3 == 0 ? 3 : digits.size() % 3;
    for (std::size_t i = 0; i < digits.size();) {
        if (!grouped.empty()) {
            grouped.push_back(' ');
        }
        const std::size_t length = grouped.empty() ? firstGroup : 3;
        grouped.append(digits.substr(i, length));
        i += length;
    }
    return negative ? "-" + grouped : grouped;
}

std::string FormatRub(std::int64_t value) {
    return FormatInteger(value) + " ₽";
}

std::string JoinWithComma(const std::vector<std::string>& items) {
    std::string result;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        result += items[i];
    }
    return result;
}

std::string JoinWithAnd(const std::vector<std::string>& items) {
    if (items.empty()) {
        return {};
    }
    if (items.size() == 1) {
        return items.front();
    }
    std::string result;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            result += (i + 1 == items.size()) ? " и " : ", ";
        }
        result += items[i];
    }
    return result;
}

std::size_t Utf8CodePointCount(std::string_view text) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size();) {
        i += Decode(text, i).Length;
        ++count;
    }
    return count;
}

std::string TruncateUtf8(std::string_view text, std::size_t maxCodePoints) {
    std::size_t i = 0;
    std::size_t count = 0;
    while (i < text.size() && count < maxCodePoints) {
        i += Decode(text, i).Length;
        ++count;
    }
    return std::string(text.substr(0, i));
}

std::string SanitizeText(std::string_view text, std::size_t maxCodePoints) {
    std::string out;
    out.reserve(text.size());
    bool pendingSpace = false;
    for (std::size_t i = 0; i < text.size();) {
        const auto cp = Decode(text, i);
        const bool control = cp.Value < 0x20 || cp.Value == 0x7F;
        if (control || cp.Value == U' ' || cp.Value == U'\t' || cp.Value == U'\n' ||
            cp.Value == U'\r') {
            pendingSpace = !out.empty();
            i += cp.Length;
            continue;
        }
        if (pendingSpace) {
            out.push_back(' ');
            pendingSpace = false;
        }
        AppendCodePoint(out, cp.Value);
        i += cp.Length;
    }
    return Trim(TruncateUtf8(out, maxCodePoints));
}

} // namespace ba::text
