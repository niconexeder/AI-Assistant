#include "ba/core/MessageInterpreter.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ba/utils/RussianText.hpp"

namespace ba {

namespace {

using text::ContainsAny;
using text::FormatInteger;
using text::Normalize;
using text::ToLowerUtf8;

constexpr double MaxMoney = 1.0e12;

struct Token {
    std::string Text;
    std::size_t Begin = 0;
    std::size_t End = 0;
};

bool IsWordByte(unsigned char byte) {
    return byte >= 0x80 || std::isalnum(byte) != 0;
}

std::vector<Token> Tokenize(const std::string& normalized) {
    std::vector<Token> tokens;
    std::size_t i = 0;
    while (i < normalized.size()) {
        if (!IsWordByte(static_cast<unsigned char>(normalized[i]))) {
            ++i;
            continue;
        }
        const std::size_t begin = i;
        while (i < normalized.size() &&
               IsWordByte(static_cast<unsigned char>(normalized[i]))) {
            ++i;
        }
        tokens.push_back(Token{normalized.substr(begin, i - begin), begin, i});
    }
    return tokens;
}

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string WindowBefore(const std::string& text, std::size_t position, std::size_t length) {
    return position > length ? text.substr(position - length, length)
                             : text.substr(0, position);
}

std::string WindowAfter(const std::string& text, std::size_t position, std::size_t length) {
    return position < text.size() ? text.substr(position, length) : std::string{};
}

bool WindowContains(const std::string& text, std::size_t begin, std::size_t end,
                    std::size_t radius, const std::vector<std::string_view>& needles) {
    const auto from = begin > radius ? begin - radius : 0;
    const auto to = std::min(text.size(), end + radius);
    return ContainsAny(std::string_view(text).substr(from, to - from), needles);
}

bool HasAny(const std::string& text, const std::vector<std::string_view>& needles) {
    return ContainsAny(text, needles);
}

// ---------- Number lexemes ----------

struct NumberWord {
    double Value = 0;
    bool Scale = false;
    bool Half = false;
};

std::optional<NumberWord> LookupNumberWord(std::string_view word) {
    static const std::unordered_map<std::string, double> kSimple = {
        {"ноль", 0},      {"нуль", 0},       {"один", 1},        {"одна", 1},
        {"одно", 1},      {"два", 2},        {"две", 2},         {"три", 3},
        {"четыре", 4},    {"пять", 5},       {"шесть", 6},       {"семь", 7},
        {"восемь", 8},    {"девять", 9},     {"десять", 10},     {"одиннадцать", 11},
        {"двенадцать", 12}, {"тринадцать", 13}, {"четырнадцать", 14},
        {"пятнадцать", 15}, {"шестнадцать", 16}, {"семнадцать", 17},
        {"восемнадцать", 18}, {"девятнадцать", 19}, {"двадцать", 20},
        {"тридцать", 30}, {"сорок", 40},    {"пятьдесят", 50},  {"шестьдесят", 60},
        {"семьдесят", 70}, {"восемьдесят", 80}, {"девяносто", 90},
        {"сто", 100},     {"двести", 200},  {"триста", 300},    {"четыреста", 400},
        {"пятьсот", 500}, {"шестьсот", 600}, {"семьсот", 700},  {"восемьсот", 800},
        {"девятьсот", 900}};
    if (const auto it = kSimple.find(std::string(word)); it != kSimple.end()) {
        return NumberWord{it->second, false, false};
    }
    if (word == "полтора" || word == "полторы") {
        return NumberWord{1.5, false, true};
    }
    if (word == "тысяча" || word == "тысячи" || word == "тысяч") {
        return NumberWord{1000, true, false};
    }
    if (word == "миллион" || word == "миллиона" || word == "миллионов") {
        return NumberWord{1000000, true, false};
    }
    if (word == "миллиард" || word == "миллиарда" || word == "миллиардов") {
        return NumberWord{1000000000, true, false};
    }
    return std::nullopt;
}

bool ParseNumericChunk(const std::string& raw, double& value) {
    std::string compact;
    compact.reserve(raw.size());
    for (const char ch : raw) {
        if (ch != ' ' && ch != '\'' && ch != '\t') {
            compact.push_back(ch);
        }
    }
    if (compact.empty()) {
        return false;
    }
    std::size_t firstSeparator = std::string::npos;
    char separator = '\0';
    int separatorCount = 0;
    for (std::size_t i = 0; i < compact.size(); ++i) {
        if (compact[i] == '.' || compact[i] == ',') {
            if (firstSeparator == std::string::npos) {
                firstSeparator = i;
                separator = compact[i];
            } else if (compact[i] != separator) {
                return false;
            }
            ++separatorCount;
        } else if (!std::isdigit(static_cast<unsigned char>(compact[i]))) {
            return false;
        }
    }

    auto parseDigits = [](std::string_view digits) -> std::optional<long double> {
        if (digits.empty()) {
            return std::nullopt;
        }
        long double result = 0;
        for (const char ch : digits) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                return std::nullopt;
            }
            result = result * 10 + (ch - '0');
        }
        return result;
    };

    if (separatorCount == 0) {
        const auto digits = parseDigits(compact);
        if (!digits.has_value()) {
            return false;
        }
        value = static_cast<double>(*digits);
        return true;
    }

    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= compact.size(); ++i) {
        if (i == compact.size() || compact[i] == separator) {
            parts.push_back(compact.substr(start, i - start));
            start = i + 1;
        }
    }
    if (parts.size() < 2) {
        return false;
    }

    const bool grouped = separatorCount >= 1 &&
        std::all_of(parts.begin() + 1, parts.end(),
                    [](const std::string& part) { return part.size() == 3; }) &&
        (parts.size() > 2 || parts.front().size() <= 3);

    if (grouped) {
        std::string digits;
        for (const auto& part : parts) {
            digits += part;
        }
        const auto parsed = parseDigits(digits);
        if (!parsed.has_value()) {
            return false;
        }
        value = static_cast<double>(*parsed);
        return true;
    }

    if (parts.size() != 2 || parts[1].empty() || parts[1].size() > 3) {
        return false;
    }
    const auto integerPart = parseDigits(parts[0]);
    const auto fractionPart = parseDigits(parts[1]);
    if (!integerPart.has_value() || !fractionPart.has_value()) {
        return false;
    }
    long double fraction = *fractionPart;
    for (std::size_t i = 0; i < parts[1].size(); ++i) {
        fraction /= 10;
    }
    value = static_cast<double>(*integerPart + fraction);
    return true;
}

enum class NumberUnit {
    None,
    Cm,
    Meter,
    Kelvin,
    Thousand,
    Rub,
    Percent,
    Million
};

struct NumberMention {
    double Raw = 0;
    NumberUnit Unit = NumberUnit::None;
    std::size_t Begin = 0;
    std::size_t End = 0;
    bool UnitAttached = false;
    bool FromWords = false;
    bool HasScale = false;
    bool Consumed = false;
};

bool IsBoundaryAt(const std::string& text, std::size_t position) {
    return position >= text.size() ||
           !IsWordByte(static_cast<unsigned char>(text[position]));
}

std::pair<NumberUnit, std::size_t> ParseUnitAt(const std::string& text,
                                               std::size_t position,
                                               bool& attached) {
    std::size_t i = position;
    while (i < text.size() && text[i] == ' ') {
        ++i;
    }
    attached = (i == position);
    if (i >= text.size()) {
        return {NumberUnit::None, i};
    }
    if (text.compare(i, 3, "₽") == 0) {
        return {NumberUnit::Rub, i + 3};
    }
    if (StartsWith(std::string_view(text).substr(i), "руб")) {
        std::size_t end = i + 3;
        while (end < text.size() && IsWordByte(static_cast<unsigned char>(text[end]))) {
            ++end;
        }
        return {NumberUnit::Rub, end};
    }
    if (StartsWith(std::string_view(text).substr(i), "р.")) {
        return {NumberUnit::Rub, i + 4};
    }
    if (StartsWith(std::string_view(text).substr(i), "р") && IsBoundaryAt(text, i + 2)) {
        return {NumberUnit::Rub, i + 2};
    }
    if (StartsWith(std::string_view(text).substr(i), "тыс") ||
        StartsWith(std::string_view(text).substr(i), "тысяч")) {
        std::size_t end = i + 3;
        while (end < text.size() && IsWordByte(static_cast<unsigned char>(text[end]))) {
            ++end;
        }
        return {NumberUnit::Thousand, end};
    }
    if (StartsWith(std::string_view(text).substr(i), "кельвин")) {
        std::size_t end = i + 7;
        while (end < text.size() && IsWordByte(static_cast<unsigned char>(text[end]))) {
            ++end;
        }
        return {NumberUnit::Kelvin, end};
    }
    if (StartsWith(std::string_view(text).substr(i), "сантиметр")) {
        std::size_t end = i + 9;
        while (end < text.size() && IsWordByte(static_cast<unsigned char>(text[end]))) {
            ++end;
        }
        return {NumberUnit::Cm, end};
    }
    if (StartsWith(std::string_view(text).substr(i), "см")) {
        std::size_t end = i + 2;
        while (end < text.size() && IsWordByte(static_cast<unsigned char>(text[end]))) {
            ++end;
        }
        return {NumberUnit::Cm, end};
    }
    if (StartsWith(std::string_view(text).substr(i), "метр")) {
        std::size_t end = i + 4;
        while (end < text.size() && IsWordByte(static_cast<unsigned char>(text[end]))) {
            ++end;
        }
        return {NumberUnit::Meter, end};
    }
    if (StartsWith(std::string_view(text).substr(i), "м") && IsBoundaryAt(text, i + 2)) {
        return {NumberUnit::Meter, i + 2};
    }
    if (StartsWith(std::string_view(text).substr(i), "процент")) {
        std::size_t end = i + 7;
        while (end < text.size() && IsWordByte(static_cast<unsigned char>(text[end]))) {
            ++end;
        }
        return {NumberUnit::Percent, end};
    }
    if (StartsWith(std::string_view(text).substr(i), "к") && IsBoundaryAt(text, i + 2)) {
        return {NumberUnit::Kelvin, i + 2};
    }
    if (text[i] == 'k' && IsBoundaryAt(text, i + 1)) {
        return {NumberUnit::Kelvin, i + 1};
    }
    return {NumberUnit::None, i};
}

std::vector<NumberMention> ScanNumberMentions(const std::string& text) {
    std::vector<NumberMention> mentions;
    const std::size_t size = text.size();
    for (std::size_t i = 0; i < size;) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
            ++i;
            continue;
        }
        const std::size_t begin = i;
        std::size_t j = i;
        std::string chunk;
        while (j < size) {
            if (std::isdigit(static_cast<unsigned char>(text[j]))) {
                chunk.push_back(text[j]);
                ++j;
                continue;
            }
            if ((text[j] == '.' || text[j] == ',') && j + 1 < size &&
                std::isdigit(static_cast<unsigned char>(text[j + 1]))) {
                chunk.push_back(text[j]);
                ++j;
                continue;
            }
            if (text[j] == ' ' && j + 3 < size &&
                std::isdigit(static_cast<unsigned char>(text[j + 1])) &&
                std::isdigit(static_cast<unsigned char>(text[j + 2])) &&
                std::isdigit(static_cast<unsigned char>(text[j + 3])) &&
                (j + 4 >= size ||
                 !std::isdigit(static_cast<unsigned char>(text[j + 4])))) {
                chunk.push_back(' ');
                ++j;
                continue;
            }
            break;
        }
        double raw = 0;
        if (!ParseNumericChunk(chunk, raw)) {
            ++i;
            continue;
        }
        bool attached = false;
        const auto unit = ParseUnitAt(text, j, attached);
        NumberMention mention;
        mention.Raw = raw;
        mention.Unit = unit.first;
        if (mention.Unit == NumberUnit::Kelvin && attached && raw > 0 && raw < 1000) {
            // "20к" next to a budget means 20 thousand, not 20 kelvin.
            mention.Unit = NumberUnit::Thousand;
        }
        mention.Begin = begin;
        mention.End = unit.second;
        mention.UnitAttached = attached;
        mentions.push_back(mention);
        i = std::max(j, unit.second);
        if (i <= begin) {
            i = begin + 1;
        }
    }

    const auto tokens = Tokenize(text);
    for (std::size_t i = 0; i < tokens.size();) {
        const auto first = LookupNumberWord(tokens[i].Text);
        if (!first.has_value()) {
            ++i;
            continue;
        }
        double total = 0;
        double current = 0;
        bool hasScale = false;
        bool sawNumber = false;
        std::size_t last = i;
        std::size_t j = i;
        while (j < tokens.size()) {
            if (tokens[j].Text == "и") {
                if (j + 1 < tokens.size() && LookupNumberWord(tokens[j + 1].Text).has_value()) {
                    ++j;
                    continue;
                }
                break;
            }
            const auto word = LookupNumberWord(tokens[j].Text);
            if (!word.has_value()) {
                break;
            }
            sawNumber = true;
            if (word->Scale) {
                const double multiplier = word->Value;
                total += (current == 0 ? 1.0 : current) * multiplier;
                current = 0;
                hasScale = true;
            } else {
                current += word->Value;
            }
            last = j;
            ++j;
        }
        if (sawNumber && total + current <= MaxMoney) {
            bool attached = false;
            const auto unit = ParseUnitAt(text, tokens[last].End, attached);
            NumberMention mention;
            mention.Raw = total + current;
            mention.Unit = unit.first;
            mention.Begin = tokens[i].Begin;
            mention.End = unit.second;
            mention.UnitAttached = attached;
            mention.FromWords = true;
            mention.HasScale = hasScale;
            mentions.push_back(mention);
            i = last + 1;
        } else {
            ++i;
        }
    }

    std::sort(mentions.begin(), mentions.end(), [](const NumberMention& left,
                                                  const NumberMention& right) {
        if (left.Begin != right.Begin) {
            return left.Begin < right.Begin;
        }
        return left.End < right.End;
    });
    std::vector<NumberMention> filtered;
    for (const auto& mention : mentions) {
        if (!filtered.empty() && mention.Begin < filtered.back().End &&
            mention.Begin >= filtered.back().Begin) {
            if (!filtered.back().FromWords) {
                continue;
            }
            if (!mention.FromWords) {
                filtered.back() = mention;
            }
            continue;
        }
        filtered.push_back(mention);
    }
    return filtered;
}

// ---------- Context helpers ----------

bool WidthCueNear(const std::string& text, std::size_t begin, std::size_t end) {    return WindowContains(text, begin, end, 55,
                          {"ширин", "шире", "уже", "столешниц", "поперек", "габарит"});
}

bool LampCueNear(const std::string& text, std::size_t begin, std::size_t end) {
    return WindowContains(text, begin, end, 55,
                          {"температур", "цветов", "свет", "кельвин", "ламп", "светильник",
                           "торшер"});
}

bool BudgetKeywordNear(const std::string& text, std::size_t begin, std::size_t end) {
    return WindowContains(
        text, begin, end, 45,
        {"бюджет", "потрат", "улож", "рассчитыва", "располага", "потолок", "не дороже",
         "не более", "не больше", "максимум", "около", "примерно", "в пределах", "сумм",
         "итого", "за все", "на все", "всего", "цена комплекта", "стоимост",
         "могу позволить"});
}

// True when the mention is directly followed by a money unit ("... 4000 рублей"),
// as opposed to a temperature ("лампа 4000 К, бюджет 20000 рублей").
bool MoneyUnitFollows(const std::string& text, std::size_t end) {
    std::size_t i = end;
    while (i < text.size() && text[i] == ' ') {
        ++i;
    }
    if (i >= text.size()) {
        return false;
    }
    const auto rest = std::string_view(text).substr(i);
    if (StartsWith(rest, "₽") || StartsWith(rest, "руб") || StartsWith(rest, "тыс") ||
        StartsWith(rest, "р.")) {
        return true;
    }
    return StartsWith(rest, "р") && IsBoundaryAt(text, i + 2);
}

bool QuantityNear(const std::string& text, const NumberMention& mention) {
    if (mention.Raw < 1 || mention.Raw > 20) {
        return false;
    }
    return WindowContains(text, mention.Begin, mention.End, 18,
                          {"штук", "шт.", "штуки", "штука", "стол", "кресл", "стул",
                           "ламп", "светильник", "торшер"});
}

bool StartsWithComparison(std::string_view value,
                          const std::vector<std::string_view>& needles) {
    const auto trimmed = text::Trim(value);
    return std::any_of(needles.begin(), needles.end(), [&](std::string_view needle) {
        return StartsWith(trimmed, needle);
    });
}

bool EndsWithComparison(std::string_view value,
                        const std::vector<std::string_view>& needles) {
    const auto trimmed = text::Trim(value);
    return std::any_of(needles.begin(), needles.end(), [&](std::string_view needle) {
        return EndsWith(trimmed, needle);
    });
}

NumericConstraint ConstraintFromContext(const std::string& text,
                                        const NumberMention& mention) {
    const auto before = WindowBefore(text, mention.Begin, 55);
    const auto after = WindowAfter(text, mention.End, 40);

    auto make = [&](bool hasMin, bool minInclusive, bool hasMax, bool maxInclusive,
                    std::int64_t value) {
        NumericConstraint constraint;
        if (hasMin) {
            constraint.Min = NumericBound{value, minInclusive};
        }
        if (hasMax) {
            constraint.Max = NumericBound{value, maxInclusive};
        }
        return constraint;
    };

    if (WindowContains(text, mention.Begin, mention.End, 40,
                       {"не шире", "не более", "не больше", "не выше", "максимум"})) {
        return make(false, false, true, true, 0);
    }
    if (WindowContains(text, mention.Begin, mention.End, 40,
                       {"не уже", "не менее", "не меньше", "минимум"})) {
        return make(true, true, false, false, 0);
    }
    if (EndsWithComparison(before, {"до"})) {
        return make(false, false, true, true, 0);
    }
    if (StartsWithComparison(after, {"или меньше", "и меньше", "или уже", "и уже"})) {
        return make(false, false, true, true, 0);
    }
    if (StartsWithComparison(after, {"или больше", "и больше", "или более", "и более"})) {
        return make(true, true, false, false, 0);
    }
    if (EndsWithComparison(before, {"-", "—"})) {
        return make(false, false, true, true, 0);
    }
    if (StartsWithComparison(after, {"-", "—"})) {
        return make(true, true, false, false, 0);
    }
    if (EndsWithComparison(before, {"от"})) {
        return make(true, true, false, false, 0);
    }
    if (WindowContains(text, mention.Begin, mention.End, 25, {"шире", "выше", "больше"})) {
        return make(true, false, false, false, 0);
    }
    if (WindowContains(text, mention.Begin, mention.End, 25, {"уже", "ниже", "меньше"})) {
        return make(false, false, true, false, 0);
    }
    return make(true, true, true, true, 0);
}

void MergeConstraint(NumericConstraint& target, const NumericConstraint& addition,
                     std::int64_t value) {
    auto apply = [&](const NumericConstraint& constraint) {
        if (constraint.Min.has_value()) {
            target.Min = NumericBound{value, constraint.Min->Inclusive};
        }
        if (constraint.Max.has_value()) {
            target.Max = NumericBound{value, constraint.Max->Inclusive};
        }
    };
    apply(addition);
}

std::int64_t ConvertToCents(const NumberMention& mention) {
    const double value = mention.Unit == NumberUnit::Meter ? mention.Raw * 100.0 : mention.Raw;
    if (value <= 0 || value > 1.0e9) {
        return 0;
    }
    return static_cast<std::int64_t>(std::llround(value));
}

std::int64_t ConvertToKelvin(const NumberMention& mention) {
    if (mention.Raw <= 0 || mention.Raw > 1.0e7) {
        return 0;
    }
    return static_cast<std::int64_t>(std::llround(mention.Raw));
}

bool IsCategoryMentioned(const MessageFacts& facts, Category category) {
    return std::find(facts.MentionedCategories.begin(), facts.MentionedCategories.end(),
                     category) != facts.MentionedCategories.end();
}

bool IsSingleNumericAnswer(const std::string& text) {
    static const std::unordered_set<std::string> kAllowed = {
        "бюджет", "руб", "рублей", "рубля", "тыс", "тысяч", "тысячи", "тысяча",
        "на",     "за",  "все",     "всего", "до",   "не",     "более",   "около",
        "примерно", "потратить", "могу", "уложиться", "пределах", "сумма", "итого",
        "комплект", "комплекта", "пожалуйста", "где-то", "порядка"};
    const auto tokens = Tokenize(text);
    if (tokens.empty()) {
        return false;
    }
    bool hasNumber = false;
    for (const auto& token : tokens) {
        if (LookupNumberWord(token.Text).has_value()) {
            hasNumber = true;
            continue;
        }
        if (std::all_of(token.Text.begin(), token.Text.end(), [](unsigned char ch) {
                return std::isdigit(ch) != 0;
            })) {
            hasNumber = true;
            continue;
        }
        if (kAllowed.count(token.Text) == 0) {
            return false;
        }
    }
    return hasNumber;
}

// ---------- City lexicons ----------

const std::vector<std::string_view>& UnknownCityStems() {
    static const std::vector<std::string_view> kStems = {
        "москв",       "санкт-петербург", "петербург",   "спб",        "питер",
        "краснодар",   "сочи",            "волгоград",   "воронеж",    "самар",
        "казан",       "екатеринбург",    "новосибирск", "челябинск",  "омск",
        "красноярск",  "саратов",         "тюмен",       "владивосток", "хабаровск",
        "иркутск",     "барнаул",         "кемеров",     "ярославл",   "рязан",
        "липец",       "курск",           "ставропол",   "симферопол", "севастопол",
        "махачкал",    "грозн",           "белгород",    "калуг",      "оренбург",
        "пенз",        "киров",           "чебоксар",    "тольятти",   "ижевск",
        "брянск",      "курган",          "магнитогорск", "сургут",    "архангельск",
        "мурманск",    "петрозаводск",    "вологд",      "смоленск",   "псков",
        "астрахан",    "саранск",         "тамбов",      "ульяновск",  "калининград",
        "тверь",       "твери",           "тула",        "туле",       "пермь",
        "перми",       "уфа",             "уфе",         "подольск",   "химки",
        "балаших",     "мытищ",           "королев",     "люберц",     "красногорск",
        "новочеркасск", "шахты",          "каменск",     "батайск",    "новороссийск",
        "геленджик",   "анап",            "армавир",     "майкоп",     "элист",
        "владикавказ", "нальчик",         "черкесск",    "пятигорск",  "кисловодск",
        "ессентук",    "минеральные",     "derbent",     "дербент",    "каспийск",
        "магадан",     "якутск",          "петропавловск", "южно-сахалинск", "анадыр",
        "мурманск",    "норильск",        "новокузнецк", "томск",      "бийск",
        "прокопьевск", "кемерово",        "новосибирск", "оренбург",   "стерлитамак",
        "салават",     "нефтекамск",      "альметьевск", "набережные", "челны",
        "казань",      "зеленодольск",    "йошкар",      "cheboksary", "чебоксары",
        "саранск",     "пенза",           "кузнецк",     "самара",     "тольятти",
        "сызрань",     "саратов",         "энгельс",     "балаков",    "волжский",
        "волгоград",   "астрахань",       "элиста",      "ставрополь", "краснодар",
        "ростов",      "таганрог",        "азов"};
    return kStems;
}

bool IsKnownCityToken(std::string_view token, std::string_view stem) {
    return token.size() >= stem.size() && token.size() <= stem.size() + 3 &&
           token.compare(0, stem.size(), stem) == 0;
}

std::optional<Category> CategoryFromToken(std::string_view token) {
    if (StartsWith(token, "стол")) {
        if (StartsWith(token, "столешниц") || token == "столовая" || token == "столовой" ||
            token == "столовую" || token == "столовые") {
            return std::nullopt;
        }
        return Category::Table;
    }
    if (StartsWith(token, "кресл") || StartsWith(token, "стул")) {
        return Category::Chair;
    }
    if (StartsWith(token, "ламп") || StartsWith(token, "светильник") ||
        StartsWith(token, "торшер") || StartsWith(token, "подсветк")) {
        return Category::Lamp;
    }
    return std::nullopt;
}

bool UnsupportedToken(std::string_view token, const std::string& before) {
    static const std::vector<std::string_view> kUnsupported = {
        "диван",   "шкаф",     "кроват",  "комод",   "стеллаж", "табурет",
        "пуф",     "софа",     "гардероб", "шифоньер", "буфет",  "сервант",
        "трельяж", "этажерк",  "вешалк",  "зеркал",  "полк",    "тумб"};
    const bool matches = std::any_of(
        kUnsupported.begin(), kUnsupported.end(),
        [&](std::string_view stem) { return StartsWith(token, stem); });
    if (!matches) {
        return false;
    }
    const auto trimmed = text::Trim(before);
    if (EndsWith(trimmed, "с") || EndsWith(trimmed, "со")) {
        return false;
    }
    return true;
}

bool IsSentenceBoundary(char ch) {
    return ch == '.' || ch == '!' || ch == '?' || ch == ';' || ch == '\n';
}

bool IsClauseBoundary(char ch) {
    return IsSentenceBoundary(ch) || ch == ',' || ch == ':';
}

struct TextSpan {
    std::size_t Begin = 0;
    std::size_t End = 0;
};

TextSpan SentenceAround(const std::string& text, std::size_t begin, std::size_t end) {
    TextSpan span;
    span.Begin = begin;
    while (span.Begin > 0 && !IsSentenceBoundary(text[span.Begin - 1])) {
        --span.Begin;
    }
    span.End = end;
    while (span.End < text.size() && !IsSentenceBoundary(text[span.End])) {
        ++span.End;
    }
    return span;
}

std::size_t ClauseBegin(const std::string& text, std::size_t begin, std::size_t limit) {
    while (begin > limit && !IsClauseBoundary(text[begin - 1])) {
        --begin;
    }
    return begin;
}

std::size_t ClauseEnd(const std::string& text, std::size_t end, std::size_t limit) {
    while (end < limit && !IsClauseBoundary(text[end])) {
        ++end;
    }
    return end;
}

const std::vector<std::string_view>& NegationPhrases() {
    static const std::vector<std::string_view> kPhrases = {
        "не нужен",   "не нужна",   "не нужно",     "не нужны",   "не надо",
        "не берем",   "не берём",   "не интересует", "не рассматрива",
        "не требуется", "не актуально", "уберите",   "убери",      "убрать",
        "исключите",  "исключи",    "исключить",    "вместо",     "замени",
        "замена",     "откаж"};
    return kPhrases;
}

const std::vector<std::string_view>& PositiveCues() {
    static const std::vector<std::string_view> kCues = {
        "нужен",  "нужна", "нужно", "нужны", "надо",  "берем", "берём",
        "хочу",   "давай", "добав", "ещё",   "еще",   "также", "только",
        "лучше",  "плюс",  "однако"};
    return kCues;
}

bool IsBoundaryByte(std::string_view text, std::size_t position) {
    if (position == 0 || position >= text.size()) {
        return true;
    }
    const auto byte = static_cast<unsigned char>(text[position]);
    if (byte >= 0x80) {
        return true;
    }
    return std::isspace(byte) != 0 || std::ispunct(byte) != 0;
}

struct PolarityScan {
    long LastNegative = -1;
    long LastPositive = -1;
};

PolarityScan ScanPolarity(std::string_view segment) {
    PolarityScan scan;
    std::vector<bool> masked(segment.size(), false);
    auto mark = [&](std::size_t position, std::size_t length) {
        for (std::size_t i = position; i < position + length && i < masked.size(); ++i) {
            masked[i] = true;
        }
    };
    auto isMasked = [&](std::size_t position, std::size_t length) {
        for (std::size_t i = position; i < position + length && i < masked.size(); ++i) {
            if (masked[i]) {
                return true;
            }
        }
        return false;
    };

    for (const auto phrase : NegationPhrases()) {
        std::size_t position = 0;
        while ((position = segment.find(phrase, position)) != std::string_view::npos) {
            if (!isMasked(position, phrase.size())) {
                scan.LastNegative = std::max(scan.LastNegative,
                                             static_cast<long>(position));
                mark(position, phrase.size());
            }
            position += phrase.size();
        }
    }

    // The bare particle "не" is a negation on its own ("не стол"), except in
    // "не только". It must start a word so that "мне" does not count.
    std::size_t position = 0;
    while ((position = segment.find("не ", position)) != std::string_view::npos) {
        const bool startsWord = position == 0 ||
                                IsBoundaryByte(segment, position == 0 ? segment.size()
                                                                      : position - 1);
        const bool onlyMarker = segment.substr(position, 10) == "не только";
        if (startsWord && !onlyMarker && !isMasked(position, 3)) {
            scan.LastNegative = std::max(scan.LastNegative, static_cast<long>(position));
            mark(position, 3);
        }
        position += 3;
    }

    // "Без стола" negates the following list, but "без ограничений" does not.
    position = 0;
    while ((position = segment.find("без ", position)) != std::string_view::npos) {
        const bool startsWord = position == 0 ||
                                IsBoundaryByte(segment, position == 0 ? segment.size()
                                                                      : position - 1);
        const auto rest = segment.substr(position);
        const bool isLimitPhrase = text::ContainsAny(
            rest, {"ограничен", "важн", "принцип", "разниц", "проблем", "предпочтен"});
        if (startsWord && !isLimitPhrase && !isMasked(position, 4)) {
            scan.LastNegative = std::max(scan.LastNegative, static_cast<long>(position));
            mark(position, 4);
        }
        position += 4;
    }

    for (const auto cue : PositiveCues()) {
        position = 0;
        while ((position = segment.find(cue, position)) != std::string_view::npos) {
            if (!isMasked(position, cue.size())) {
                scan.LastPositive = std::max(scan.LastPositive,
                                             static_cast<long>(position));
            }
            position += cue.size();
        }
    }

    // Short adversative conjunctions that switch the polarity back to positive.
    for (const auto cue : {std::string_view(" а "), std::string_view(" но "),
                           std::string_view(" на ")}) {
        position = 0;
        while ((position = segment.find(cue, position)) != std::string_view::npos) {
            scan.LastPositive = std::max(scan.LastPositive, static_cast<long>(position));
            position += cue.size();
        }
    }

    return scan;
}

bool SegmentNegates(std::string_view segment) {
    const auto scan = ScanPolarity(segment);
    return scan.LastNegative >= 0 && scan.LastNegative > scan.LastPositive;
}

bool EndsWithNegation(std::string_view sentence) {
    const auto trimmed = text::Trim(sentence);
    for (const auto cue : {"не нужен", "не нужна", "не нужно", "не нужны", "не надо",
                           "не интересует", "не актуально", "не требуется"}) {
        if (EndsWith(trimmed, cue)) {
            return true;
        }
    }
    return false;
}

bool StartsWithNegation(std::string_view sentence) {
    const auto trimmed = text::Trim(sentence);
    for (const auto cue : {"не нужен", "не нужна", "не нужно", "не нужны", "не надо",
                           "не интересует", "не требуется", "без ", "уберите", "исключите"}) {
        if (StartsWith(trimmed, cue)) {
            return true;
        }
    }
    return false;
}

bool HasInternalNegation(std::string_view body) {
    const auto scan = ScanPolarity(body);
    return scan.LastNegative >= 0;
}

// Negation that applies to a whole comma-separated list:
// "Стол, кресло и лампа не нужны" or "Не нужны стол, кресло и лампа".
bool ListLevelNegation(std::string_view sentence) {
    const auto trimmed = text::Trim(sentence);
    const bool trailing = EndsWithNegation(trimmed);
    const bool leading = StartsWithNegation(trimmed);
    if (!trailing && !leading) {
        return false;
    }
    auto body = trimmed;
    if (trailing) {
        for (const auto cue : {"не нужен", "не нужна", "не нужно", "не нужны", "не надо",
                               "не интересует", "не актуально", "не требуется"}) {
            if (EndsWith(body, cue)) {
                body = text::Trim(body.substr(0, body.size() - std::string_view(cue).size()));
                break;
            }
        }
    }
    if (leading) {
        for (const auto cue : {"не нужен", "не нужна", "не нужно", "не нужны", "не надо",
                               "не интересует", "не требуется", "без "}) {
            if (StartsWith(body, cue)) {
                body = text::Trim(body.substr(std::string_view(cue).size()));
                break;
            }
        }
    }
    if (!body.empty() && !HasInternalNegation(body)) {
        const auto scan = ScanPolarity(body);
        if (scan.LastPositive < 0) {
            return true;
        }
    }
    return false;
}

// "Вместо стола кресло": negation covers only the replaced item, so a
// category mentioned after that item is the positive replacement.
bool CategoryFollowsReplacedItem(std::string_view before) {
    const auto instead = before.rfind("вместо");
    if (instead == std::string_view::npos) {
        return false;
    }
    const auto rest =
        std::string(before.substr(instead + std::string_view("вместо").size()));
    for (const auto& token : Tokenize(rest)) {
        if (CategoryFromToken(token.Text).has_value()) {
            return true;
        }
    }
    return false;
}

// "Стол не шире метра" constrains the table instead of excluding it.
bool IsAttributeComparison(std::string_view segment) {
    const auto trimmed = text::Trim(segment);
    for (const auto cue : {"не шире", "не уже", "не больше", "не меньше", "не более",
                           "не выше", "не ниже", "не дороже", "не дешевле", "не длиннее",
                           "не короче", "не важн", "не принципиальн", "не обязательно",
                           "не имеет значения", "не регулируется"}) {
        if (StartsWith(trimmed, cue)) {
            return true;
        }
    }
    // "Кресло без регулировки высоты" describes the chair rather than
    // excluding it.
    return StartsWith(trimmed, "без ");
}

bool CategoryNegated(const std::string& text, std::size_t begin, std::size_t end) {
    const auto sentence = SentenceAround(text, begin, end);
    const auto sentenceView =
        std::string_view(text).substr(sentence.Begin, sentence.End - sentence.Begin);
    if (ListLevelNegation(sentenceView)) {
        return true;
    }

    const auto clauseBegin = ClauseBegin(text, begin, sentence.Begin);
    const auto before =
        std::string_view(text).substr(clauseBegin, begin - clauseBegin);
    if (SegmentNegates(before) && !CategoryFollowsReplacedItem(before)) {
        return true;
    }

    const auto clauseEnd = ClauseEnd(text, end, sentence.End);
    const auto after = std::string_view(text).substr(end, clauseEnd - end);
    if (IsAttributeComparison(after)) {
        return false;
    }
    return SegmentNegates(after);
}

// City negation is clause-scoped: "Не в Таганроге, а в Азове" negates only
// Taganrog, while "Таганрог не нужен, нужен Азов" negates only the first city.
bool CityNegated(const std::string& text, std::size_t begin, std::size_t end) {
    const auto sentence = SentenceAround(text, begin, end);
    const auto clauseBegin = ClauseBegin(text, begin, sentence.Begin);
    const auto before =
        std::string_view(text).substr(clauseBegin, begin - clauseBegin);
    if (SegmentNegates(before)) {
        return true;
    }
    const auto clauseEnd = ClauseEnd(text, end, sentence.End);
    const auto after = std::string_view(text).substr(end, clauseEnd - end);
    return SegmentNegates(after);
}

bool ResetRequested(const std::string& text, const std::vector<std::string_view>& cues) {
    if (!HasAny(text, cues)) {
        return false;
    }
    return HasAny(text, {"не важн", "не принципиальн", "любой", "любая", "любые",
                         "без ограничен", "не имеет значения"});
}

} // namespace

MessageInterpreter::MessageInterpreter(const CityDirectory& cities,
                                       const CategoryDirectory& categories)
    : Cities_(&cities), Categories_(&categories) {
    for (const auto& city : cities.All()) {
        CityMatcher matcher;
        matcher.Id = city.Id;
        const auto normalizedName = Normalize(city.Name);
        matcher.Stems.push_back(normalizedName);
        const auto hyphen = normalizedName.find('-');
        if (hyphen != std::string::npos && hyphen >= 4) {
            matcher.Stems.push_back(normalizedName.substr(0, hyphen));
        }
        const auto normalizedId = ToLowerUtf8(city.Id);
        matcher.Stems.push_back(normalizedId);
        const auto idHyphen = normalizedId.find('-');
        if (idHyphen != std::string::npos && idHyphen >= 4) {
            matcher.Stems.push_back(normalizedId.substr(0, idHyphen));
        }
        std::sort(matcher.Stems.begin(), matcher.Stems.end());
        matcher.Stems.erase(std::unique(matcher.Stems.begin(), matcher.Stems.end()),
                            matcher.Stems.end());
        CityMatchers_.push_back(std::move(matcher));
    }
}

MessageFacts MessageInterpreter::Interpret(const std::string& text,
                                           const std::string& previousAssistantText) const {
    MessageFacts facts;
    const auto normalized = Normalize(text);
    const auto previous = Normalize(previousAssistantText);
    const auto tokens = Tokenize(normalized);

    facts.AnswersCategoriesQuestion =
        HasAny(previous, {"категор", "какие товары", "какие предметы", "что именно"});

    // --- Cities ---
    std::set<std::string> matchedCities;
    bool unknownCity = false;
    for (const auto& token : tokens) {
        for (const auto& matcher : CityMatchers_) {
            for (const auto& stem : matcher.Stems) {
                if (IsKnownCityToken(token.Text, stem)) {
                    if (!CityNegated(normalized, token.Begin, token.End)) {
                        matchedCities.insert(matcher.Id);
                    }
                    break;
                }
            }
        }
        for (const auto stem : UnknownCityStems()) {
            if (IsKnownCityToken(token.Text, stem)) {
                bool known = false;
                for (const auto& matcher : CityMatchers_) {
                    for (const auto& knownStem : matcher.Stems) {
                        if (IsKnownCityToken(token.Text, knownStem)) {
                            known = true;
                            break;
                        }
                    }
                    if (known) {
                        break;
                    }
                }
                if (!known && !CityNegated(normalized, token.Begin, token.End)) {
                    unknownCity = true;
                }
            }
        }
    }

    for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i].Text == "город" || tokens[i].Text == "городе" ||
            tokens[i].Text == "города" || tokens[i].Text == "городу" ||
            tokens[i].Text == "г") {
            const auto& next = tokens[i + 1].Text;
            bool known = false;
            for (const auto& matcher : CityMatchers_) {
                for (const auto& stem : matcher.Stems) {
                    if (IsKnownCityToken(next, stem)) {
                        known = true;
                    }
                }
            }
            if (!known && next.size() >= 6 &&
                !HasAny(next, {"не", "нужен", "нужна", "указан", "любой", "другой"}) &&
                std::none_of(next.begin(), next.end(),
                             [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
                unknownCity = true;
            }
        }
    }

    if (matchedCities.size() == 1 && !unknownCity) {
        facts.CityId = *matchedCities.begin();
    } else if (!matchedCities.empty() || unknownCity) {
        facts.CityInvalid = true;
    }

    // --- Categories ---
    std::set<Category> mentioned;
    std::set<Category> excluded;
    for (const auto& token : tokens) {
        const auto category = CategoryFromToken(token.Text);
        if (!category.has_value()) {
            continue;
        }
        const auto before = WindowBefore(normalized, token.Begin, 40);
        if (CategoryNegated(normalized, token.Begin, token.End)) {
            excluded.insert(*category);
        } else {
            mentioned.insert(*category);
        }
        (void)before;
    }

    std::vector<std::string> unsupported;
    for (const auto& token : tokens) {
        const auto before = WindowBefore(normalized, token.Begin, 12);
        if (!UnsupportedToken(token.Text, before)) {
            continue;
        }
        if (CategoryNegated(normalized, token.Begin, token.End)) {
            continue;
        }
        unsupported.push_back(token.Text);
    }

    for (const auto category : mentioned) {
        facts.MentionedCategories.push_back(category);
    }
    for (const auto category : excluded) {
        facts.ExcludedCategories.push_back(category);
    }
    std::sort(facts.MentionedCategories.begin(), facts.MentionedCategories.end(),
              [](Category left, Category right) {
                  return static_cast<int>(left) < static_cast<int>(right);
              });
    std::sort(facts.ExcludedCategories.begin(), facts.ExcludedCategories.end(),
              [](Category left, Category right) {
                  return static_cast<int>(left) < static_cast<int>(right);
              });
    facts.UnsupportedCategories = std::move(unsupported);
    facts.CategoriesInvalid = !facts.UnsupportedCategories.empty();

    const bool onlyMarker = normalized.find("не только") != std::string::npos;
    const bool restartMarker = !onlyMarker &&
        HasAny(normalized, {"только", "давайте", "передумал", "лучше", "вместо", "замени"});
    const bool additiveMarker = HasAny(normalized, {"еще", "ещё", "также", "добав", "плюс",
                                                    "вдобавок", "дополнительно", "тоже"}) ||
                                StartsWith(normalized, "и ");
    facts.CategoriesRestart = restartMarker && !facts.MentionedCategories.empty();
    facts.CategoriesAdditive = additiveMarker && !facts.CategoriesRestart;

    // --- Attribute resets ---
    facts.ResetTableWidth =
        ResetRequested(normalized, {"ширин", "столешниц", "габарит", "поперек"});
    facts.ResetLampColorTemperature =
        ResetRequested(normalized, {"температур", "цветов", "свет"});
    facts.ResetChairAdjustableHeight = ResetRequested(normalized, {"регулиров"});

    // --- Attribute constraints ---
    auto mentions = ScanNumberMentions(normalized);

    const bool tableMentioned = IsCategoryMentioned(facts, Category::Table);
    const bool chairMentioned = IsCategoryMentioned(facts, Category::Chair);
    const bool lampMentioned = IsCategoryMentioned(facts, Category::Lamp);

    // A metre word without a numeral: "не шире метра".
    for (const auto& token : tokens) {
        if (!StartsWith(token.Text, "метр")) {
            continue;
        }
        bool overlaps = std::any_of(mentions.begin(), mentions.end(),
                                    [&](const NumberMention& mention) {
                                        return token.Begin < mention.End &&
                                               token.End > mention.Begin;
                                    });
        if (overlaps) {
            continue;
        }
        const auto before = WindowBefore(normalized, token.Begin, 35);
        if (tableMentioned || WidthCueNear(normalized, token.Begin, token.End) ||
            HasAny(before, {"шире", "уже", "до", "от", "ширин"})) {
            NumberMention mention;
            mention.Raw = 1;
            mention.Unit = NumberUnit::Meter;
            mention.Begin = token.Begin;
            mention.End = token.End;
            mention.UnitAttached = true;
            mentions.push_back(mention);
        }
    }
    std::sort(mentions.begin(), mentions.end(), [](const NumberMention& left,
                                                  const NumberMention& right) {
        return left.Begin < right.Begin;
    });

    if (!facts.ResetTableWidth) {
        std::optional<NumericConstraint> width;
        for (auto& mention : mentions) {
            if (mention.Consumed || mention.Unit == NumberUnit::Rub ||
                mention.Unit == NumberUnit::Thousand ||
                mention.Unit == NumberUnit::Percent ||
                mention.Unit == NumberUnit::Kelvin) {
                continue;
            }
            const bool widthCue = WidthCueNear(normalized, mention.Begin, mention.End);
            bool candidate = false;
            if (mention.Unit == NumberUnit::Cm || mention.Unit == NumberUnit::Meter) {
                candidate = !lampMentioned || tableMentioned || widthCue;
                if (chairMentioned && !tableMentioned && !widthCue) {
                    candidate = false;
                }
            } else if (mention.Unit == NumberUnit::None) {
                candidate = tableMentioned && mention.Raw >= 20 && mention.Raw <= 500;
                candidate = candidate || (widthCue && mention.Raw >= 20 && mention.Raw <= 500);
            }
            if (!candidate) {
                continue;
            }
            const auto value = ConvertToCents(mention);
            if (value <= 0) {
                continue;
            }
            const auto piece = ConstraintFromContext(normalized, mention);
            if (!width.has_value()) {
                width = piece;
            }
            MergeConstraint(*width, piece, value);
            mention.Consumed = true;
        }
        facts.TableWidth = width;
    }

    if (!facts.ResetLampColorTemperature) {
        std::optional<NumericConstraint> temperature;
        for (auto& mention : mentions) {
            if (mention.Consumed) {
                continue;
            }
            if (mention.Unit == NumberUnit::Rub || mention.Unit == NumberUnit::Thousand ||
                mention.Unit == NumberUnit::Cm || mention.Unit == NumberUnit::Meter ||
                mention.Unit == NumberUnit::Percent) {
                continue;
            }
            const bool temperatureCue =
                WindowContains(normalized, mention.Begin, mention.End, 40,
                               {"температур", "цветов", "кельвин", "свет"});
            bool candidate = false;
            if (mention.Unit == NumberUnit::Kelvin) {
                candidate = mention.Raw >= 1000 && mention.Raw <= 20000;
                candidate = candidate || (temperatureCue && mention.Raw >= 1000);
            } else if (mention.Unit == NumberUnit::None) {
                const bool lampCue = WindowContains(normalized, mention.Begin, mention.End, 30,
                                                    {"ламп", "светильник", "торшер",
                                                     "температур", "цветов", "свет",
                                                     "кельвин"});
                candidate = (lampMentioned && lampCue && mention.Raw >= 2700 &&
                             mention.Raw <= 7000 && !MoneyUnitFollows(normalized, mention.End)) ||
                            (temperatureCue && mention.Raw >= 1000 && mention.Raw <= 20000);
            }
            if (!candidate) {
                continue;
            }
            const auto value = ConvertToKelvin(mention);
            if (value <= 0) {
                continue;
            }
            const auto piece = ConstraintFromContext(normalized, mention);
            if (!temperature.has_value()) {
                temperature = piece;
            }
            MergeConstraint(*temperature, piece, value);
            mention.Consumed = true;
        }
        facts.LampColorTemperature = temperature;
    }

    // --- Chair height adjustment ---
    if (!facts.ResetChairAdjustableHeight) {
        const bool chairContext =
            chairMentioned || HasAny(normalized, {"кресл", "стул"}) ||
            (HasAny(normalized, {"высот"}) && HasAny(normalized, {"регулир"}));
        const bool without = HasAny(normalized,
                                    {"без регулиров", "нерегулируем", "не регулируем",
                                     "фиксированная высота", "высота фиксирована",
                                     "фиксированной высотой", "сиденье фиксировано",
                                     "не поднимается", "высота не регулируется",
                                     "не нужна регулировка", "не требуется регулировка",
                                     "без изменения высоты"});
        const bool with = HasAny(normalized,
                                 {"с регулировкой", "регулировка высоты",
                                  "регулируемая высота", "регулируемое кресло",
                                  "высота регулируется", "регулировать высоту",
                                  "менять высоту", "изменять высоту",
                                  "поднимается и опускается", "газлифт",
                                  "механизм регулировки"});
        if (chairContext && without) {
            facts.ChairAdjustableHeight = false;
        } else if (chairContext && with) {
            facts.ChairAdjustableHeight = true;
        }
    }

    // --- Budget ---
    std::vector<std::size_t> strong;
    std::vector<std::size_t> weak;
    const bool pureAnswer = IsSingleNumericAnswer(normalized);
    for (std::size_t i = 0; i < mentions.size(); ++i) {
        auto& mention = mentions[i];
        if (mention.Consumed) {
            continue;
        }
        if (mention.Unit == NumberUnit::None && QuantityNear(normalized, mention)) {
            continue;
        }
        double amount = 0;
        bool strongCandidate = false;
        switch (mention.Unit) {
        case NumberUnit::Rub:
            amount = mention.Raw;
            strongCandidate = true;
            break;
        case NumberUnit::Thousand:
            amount = mention.Raw * 1000.0;
            strongCandidate = true;
            break;
        case NumberUnit::Kelvin:
            if (mention.Raw > 0 && mention.Raw < 1000) {
                amount = mention.Raw * 1000.0;
                strongCandidate = true;
            }
            break;
        case NumberUnit::None:
            amount = mention.Raw;
            strongCandidate = (mention.FromWords && mention.HasScale) ||
                              BudgetKeywordNear(normalized, mention.Begin, mention.End) ||
                              pureAnswer;
            break;
        case NumberUnit::Cm:
        case NumberUnit::Meter:
        case NumberUnit::Percent:
            break;
        }
        if (strongCandidate) {
            if (amount >= 0 && amount <= MaxMoney) {
                strong.push_back(i);
            }
            continue;
        }
        if (mention.Unit == NumberUnit::None && amount >= 1 && amount <= MaxMoney) {
            const bool quantity = amount <= 20 &&
                WindowContains(normalized, mention.Begin, mention.End, 18,
                               {"стол", "кресл", "стул", "ламп", "светильник", "торшер"});
            if (!quantity) {
                weak.push_back(i);
            }
        }
    }

    auto amountOf = [&](std::size_t index) {
        const auto& mention = mentions[index];
        if (mention.Unit == NumberUnit::Thousand) {
            return static_cast<std::int64_t>(std::llround(mention.Raw * 1000.0));
        }
        if (mention.Unit == NumberUnit::Kelvin && mention.Raw < 1000) {
            return static_cast<std::int64_t>(std::llround(mention.Raw * 1000.0));
        }
        return static_cast<std::int64_t>(std::llround(mention.Raw));
    };

    auto looksLikeRange = [&](std::size_t left, std::size_t right) {
        if (mentions[left].Begin > mentions[right].Begin) {
            std::swap(left, right);
        }
        const auto between = text::Trim(std::string_view(normalized).substr(
            mentions[left].End, mentions[right].Begin - mentions[left].End));
        if (between == "-" || between == "до" || between == "..") {
            return true;
        }
        const auto beforeFirst = WindowBefore(normalized, mentions[left].Begin, 12);
        return between == "до" && HasAny(beforeFirst, {"от"});
    };

    if (!strong.empty()) {
        facts.BudgetMentioned = true;
        std::vector<std::int64_t> amounts;
        for (const auto index : strong) {
            amounts.push_back(amountOf(index));
        }
        std::sort(amounts.begin(), amounts.end());
        amounts.erase(std::unique(amounts.begin(), amounts.end()), amounts.end());
        if (amounts.size() == 1) {
            facts.BudgetRub = amounts.front();
        } else if (amounts.size() == 2 && strong.size() == 2 &&
                   looksLikeRange(strong[0], strong[1])) {
            facts.BudgetRub = amounts.back();
        } else {
            facts.BudgetAmbiguous = true;
        }
    } else if (!weak.empty()) {
        facts.BudgetMentioned = true;
        std::vector<std::int64_t> amounts;
        for (const auto index : weak) {
            amounts.push_back(amountOf(index));
        }
        std::sort(amounts.begin(), amounts.end());
        amounts.erase(std::unique(amounts.begin(), amounts.end()), amounts.end());
        if (amounts.size() == 1) {
            facts.BudgetRub = amounts.front();
        } else {
            facts.BudgetAmbiguous = true;
        }
    }

    return facts;
}

} // namespace ba
