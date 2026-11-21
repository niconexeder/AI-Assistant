#include "ba/core/DecisionEngine.hpp"

#include <algorithm>
#include <sstream>
#include <string_view>

#include "ba/utils/RussianText.hpp"

namespace ba {

namespace {

std::string CityTitle(const CityDirectory& cities, const std::string& cityId) {
    if (const auto* city = cities.FindById(cityId)) {
        return city->Name;
    }
    return cityId;
}

std::string CityList(const CityDirectory& cities) {
    std::vector<std::string> names;
    names.reserve(cities.All().size());
    for (const auto& city : cities.All()) {
        names.push_back(city.Name);
    }
    return text::JoinWithComma(names);
}

std::string CategoryListLower() {
    return "стол, кресло и лампа";
}

std::string CategoryListAccusative() {
    return "стол, кресло и лампу";
}

std::string CategoryGenitivePlural(Category category) {
    switch (category) {
    case Category::Table:
        return "столов";
    case Category::Chair:
        return "кресел";
    case Category::Lamp:
        return "ламп";
    }
    return "товаров";
}

std::string DescribeConstraint(Category category, const Requirements& requirements) {
    switch (category) {
    case Category::Table:
        if (requirements.TableWidth.has_value()) {
            return "ширина стола " + requirements.TableWidth->Describe() + " см";
        }
        return "ширина стола";
    case Category::Chair:
        if (requirements.ChairAdjustableHeight.has_value()) {
            return *requirements.ChairAdjustableHeight
                       ? "кресло с регулировкой высоты"
                       : "кресло без регулировки высоты";
        }
        return "характеристики кресла";
    case Category::Lamp:
        if (requirements.LampColorTemperature.has_value()) {
            return "цветовая температура лампы " +
                   requirements.LampColorTemperature->Describe() + " К";
        }
        return "характеристики лампы";
    }
    return "характеристики";
}

std::string DescribeListing(const Listing& listing) {
    switch (listing.CategoryValue) {
    case Category::Table: {
        std::string result = "стол";
        if (listing.WidthCm.has_value()) {
            result += " шириной " + text::FormatInteger(*listing.WidthCm) + " см";
        }
        return result;
    }
    case Category::Chair: {
        std::string result = "кресло";
        if (listing.AdjustableHeight.has_value()) {
            result += *listing.AdjustableHeight ? " с регулировкой высоты"
                                                : " без регулировки высоты";
        }
        return result;
    }
    case Category::Lamp: {
        std::string result = "лампа";
        if (listing.ColorTemperatureK.has_value()) {
            result += " " + text::FormatInteger(*listing.ColorTemperatureK) + " К";
        }
        return result;
    }
    }
    return "товар";
}

std::string JoinDescriptions(const std::vector<std::string>& items) {
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

} // namespace

DecisionEngine::DecisionEngine(const BundleSearch& search,
                               const CityDirectory& cities,
                               std::size_t maxOptions)
    : Search_(&search),
      Cities_(&cities),
      MaxOptions_(std::max<std::size_t>(1, std::min<std::size_t>(maxOptions, 3))) {}

Response DecisionEngine::Decide(const Requirements& requirements) const {
    auto questions = BuildQuestions(requirements);
    if (!questions.empty()) {
        return Response::MakeClarify(std::move(questions));
    }

    const auto bundles = Search_->Find(requirements, MaxOptions_);
    if (bundles.empty()) {
        return Response::MakeNoMatch(BuildNoMatchMessage(requirements));
    }

    std::vector<BundleOption> options;
    options.reserve(bundles.size());
    for (const auto& bundle : bundles) {
        BundleOption option;
        option.ListingIds.reserve(bundle.Listings.size());
        for (const auto* listing : bundle.Listings) {
            option.ListingIds.push_back(listing->Id);
        }
        option.TotalRub = bundle.TotalRub;
        option.Explanation = BuildExplanation(bundle);
        options.push_back(std::move(option));
    }
    return Response::MakeOk(std::move(options));
}

std::vector<ClarifyQuestion> DecisionEngine::BuildQuestions(
    const Requirements& requirements) const {
    std::vector<ClarifyQuestion> questions;

    const bool cityMissing = !requirements.CityId.has_value();
    if (cityMissing || requirements.CityInvalid) {
        ClarifyQuestion question;
        question.Field = "city_id";
        if (requirements.CityInvalid) {
            question.Text = "Я подбираю комплекты только в городах: " + CityList(*Cities_) +
                            ". В каком из них искать?";
        } else {
            question.Text = "В каком городе искать? Доступны: " + CityList(*Cities_) + ".";
        }
        questions.push_back(std::move(question));
    }

    if (requirements.Categories.empty() || requirements.CategoriesInvalid) {
        ClarifyQuestion question;
        question.Field = "categories";
        if (requirements.CategoriesInvalid) {
            question.Text = "Я могу подобрать только " + CategoryListAccusative() +
                            ". Какие из этих товаров нужны?";
        } else {
            question.Text = "Какие товары нужны? Доступны: " + CategoryListLower() + ".";
        }
        questions.push_back(std::move(question));
    }

    if (!requirements.BudgetRub.has_value() || requirements.BudgetAmbiguous) {
        ClarifyQuestion question;
        question.Field = "budget_rub";
        question.Text = requirements.BudgetAmbiguous
                            ? "Уточните, пожалуйста, бюджет на весь комплект одной суммой в рублях."
                            : "Какой у вас бюджет на весь комплект?";
        questions.push_back(std::move(question));
    }

    return questions;
}

std::string DecisionEngine::BuildNoMatchMessage(const Requirements& requirements) const {
    const std::string city = CityTitle(*Cities_, *requirements.CityId);
    const auto diagnostics = Search_->Diagnose(requirements);

    std::vector<std::string> missingCategories;
    std::vector<std::string> blockedConditions;
    for (const auto& availability : diagnostics.Categories) {
        if (availability.InCity == 0) {
            missingCategories.push_back(CategoryGenitivePlural(availability.CategoryValue));
        } else if (availability.Matching == 0) {
            blockedConditions.push_back(
                DescribeConstraint(availability.CategoryValue, requirements));
        }
    }

    std::ostringstream message;
    message << "В городе " << city << " ";

    if (!missingCategories.empty() && !blockedConditions.empty()) {
        message << "нет " << JoinDescriptions(missingCategories)
                << ", а подходящих вариантов с условиями (" << JoinDescriptions(blockedConditions)
                << ") не нашлось.";
        return message.str();
    }
    if (!missingCategories.empty()) {
        message << "нет " << JoinDescriptions(missingCategories) << ".";
        return message.str();
    }
    if (!blockedConditions.empty()) {
        message << "нет вариантов, подходящих под условия: " << JoinDescriptions(blockedConditions)
                << ".";
        return message.str();
    }

    if (diagnostics.CheapestComplete.has_value() && requirements.BudgetRub.has_value() &&
        *diagnostics.CheapestComplete > *requirements.BudgetRub) {
        message << "самый доступный подходящий комплект стоит "
                << text::FormatRub(*diagnostics.CheapestComplete)
                << ", а бюджет — " << text::FormatRub(*requirements.BudgetRub) << ".";
        return message.str();
    }

    message << "не нашлось подходящего комплекта. Попробуйте изменить бюджет или условия.";
    return message.str();
}

std::string DecisionEngine::BuildExplanation(const Bundle& bundle) const {
    std::vector<std::string> parts;
    parts.reserve(bundle.Listings.size());
    for (const auto* listing : bundle.Listings) {
        parts.push_back(DescribeListing(*listing));
    }
    return "Комплект: " + JoinDescriptions(parts) + ". Итого " +
           text::FormatRub(bundle.TotalRub) + ".";
}

} // namespace ba
