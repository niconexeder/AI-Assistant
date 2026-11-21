#include "ba/models/Response.hpp"

#include <stdexcept>
#include <utility>

namespace ba {

Response Response::MakeOk(std::vector<BundleOption> options) {
    if (options.empty() || options.size() > 3) {
        throw std::logic_error("ok response must contain 1..3 options");
    }
    return Response(OkPayload{std::move(options)});
}

Response Response::MakeClarify(std::vector<ClarifyQuestion> questions) {
    if (questions.empty() || questions.size() > 3) {
        throw std::logic_error("clarify response must contain 1..3 questions");
    }
    return Response(ClarifyPayload{std::move(questions)});
}

Response Response::MakeNoMatch(std::string message) {
    if (message.empty()) {
        throw std::logic_error("no_match message must not be empty");
    }
    return Response(NoMatchPayload{std::move(message)});
}

nlohmann::json Response::ToJson() const {
    nlohmann::json result;
    if (const auto* ok = std::get_if<OkPayload>(&Payload_)) {
        result["status"] = "ok";
        result["options"] = nlohmann::json::array();
        for (const auto& option : ok->Options) {
            nlohmann::json item;
            item["listing_ids"] = option.ListingIds;
            item["total_rub"] = option.TotalRub;
            item["explanation"] = option.Explanation;
            result["options"].push_back(std::move(item));
        }
        return result;
    }
    if (const auto* clarify = std::get_if<ClarifyPayload>(&Payload_)) {
        result["status"] = "clarify";
        result["questions"] = nlohmann::json::array();
        for (const auto& question : clarify->Questions) {
            result["questions"].push_back(
                nlohmann::json{{"field", question.Field}, {"text", question.Text}});
        }
        return result;
    }
    const auto& noMatch = std::get<NoMatchPayload>(Payload_);
    result["status"] = "no_match";
    result["message"] = noMatch.Message;
    return result;
}

} // namespace ba
