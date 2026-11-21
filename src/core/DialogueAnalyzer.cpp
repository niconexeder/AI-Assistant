#include "ba/core/DialogueAnalyzer.hpp"

#include <algorithm>
#include <set>

#include "ba/utils/RussianText.hpp"

namespace ba {

namespace {

void ReplaceCategories(Requirements& requirements, const std::vector<Category>& categories) {
    requirements.Categories = categories;
    std::sort(requirements.Categories.begin(), requirements.Categories.end(),
              [](Category left, Category right) {
                  return static_cast<int>(left) < static_cast<int>(right);
              });
    requirements.Categories.erase(
        std::unique(requirements.Categories.begin(), requirements.Categories.end()),
        requirements.Categories.end());
}

bool HasNegationMarker(const std::string& normalizedText) {
    return text::ContainsAny(normalizedText,
                             {"не нужен", "не нужна", "не нужно", "не нужны", "не надо",
                              "без ", "убери", "уберите", "исключи", "исключите"});
}

} // namespace

DialogueAnalyzer::DialogueAnalyzer(const CityDirectory& cities,
                                   const CategoryDirectory& categories)
    : Interpreter_(cities, categories) {}

Requirements DialogueAnalyzer::Analyze(const std::vector<Message>& messages) const {
    Requirements requirements;
    std::string lastAssistantText;

    for (const auto& message : messages) {
        if (message.Speaker == Role::Assistant) {
            lastAssistantText = message.Text;
            continue;
        }

        const auto normalized = text::Normalize(message.Text);
        const auto facts = Interpreter_.Interpret(message.Text, lastAssistantText);

        if (facts.CityId.has_value()) {
            requirements.CityId = facts.CityId;
            requirements.CityInvalid = false;
        } else if (facts.CityInvalid) {
            requirements.CityId.reset();
            requirements.CityInvalid = true;
        }

        if (facts.BudgetRub.has_value()) {
            requirements.BudgetRub = facts.BudgetRub;
            requirements.BudgetAmbiguous = false;
        } else if (facts.BudgetAmbiguous) {
            requirements.BudgetRub.reset();
            requirements.BudgetAmbiguous = true;
        }

        if (!facts.UnsupportedCategories.empty()) {
            for (const auto& unsupported : facts.UnsupportedCategories) {
                if (std::find(requirements.UnsupportedCategories.begin(),
                              requirements.UnsupportedCategories.end(),
                              unsupported) == requirements.UnsupportedCategories.end()) {
                    requirements.UnsupportedCategories.push_back(unsupported);
                }
            }
            requirements.CategoriesInvalid = true;
        }

        if (!facts.MentionedCategories.empty()) {
            const bool replace = facts.CategoriesRestart ||
                                 facts.AnswersCategoriesQuestion ||
                                 !facts.CategoriesAdditive || requirements.Categories.empty();
            if (replace) {
                ReplaceCategories(requirements, facts.MentionedCategories);
            } else {
                for (const auto category : facts.MentionedCategories) {
                    requirements.AddCategory(category);
                }
            }
        }

        for (const auto category : facts.ExcludedCategories) {
            requirements.RemoveCategory(category);
        }

        const bool clearsCategoryProblem =
            !facts.MentionedCategories.empty() && facts.UnsupportedCategories.empty() &&
            (facts.CategoriesRestart || facts.AnswersCategoriesQuestion ||
             HasNegationMarker(normalized));
        if (clearsCategoryProblem) {
            requirements.CategoriesInvalid = false;
            requirements.UnsupportedCategories.clear();
        }

        if (facts.ResetTableWidth) {
            requirements.TableWidth.reset();
        } else if (facts.TableWidth.has_value()) {
            requirements.TableWidth = facts.TableWidth;
        }

        if (facts.ResetLampColorTemperature) {
            requirements.LampColorTemperature.reset();
        } else if (facts.LampColorTemperature.has_value()) {
            requirements.LampColorTemperature = facts.LampColorTemperature;
        }

        if (facts.ResetChairAdjustableHeight) {
            requirements.ChairAdjustableHeight.reset();
        } else if (facts.ChairAdjustableHeight.has_value()) {
            requirements.ChairAdjustableHeight = facts.ChairAdjustableHeight;
        }
    }

    return requirements;
}

} // namespace ba
