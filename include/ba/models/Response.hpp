#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba {

struct BundleOption {
    std::vector<std::string> ListingIds;
    std::int64_t TotalRub = 0;
    std::string Explanation;
};

struct ClarifyQuestion {
    std::string Field;
    std::string Text;
};

struct OkPayload {
    std::vector<BundleOption> Options;
};

struct ClarifyPayload {
    std::vector<ClarifyQuestion> Questions;
};

struct NoMatchPayload {
    std::string Message;
};

class Response {
public:
    using Payload = std::variant<OkPayload, ClarifyPayload, NoMatchPayload>;

    static Response MakeOk(std::vector<BundleOption> options);
    static Response MakeClarify(std::vector<ClarifyQuestion> questions);
    static Response MakeNoMatch(std::string message);

    const Payload& GetPayload() const noexcept { return Payload_; }
    nlohmann::json ToJson() const;

private:
    explicit Response(Payload payload) : Payload_(std::move(payload)) {}

    Payload Payload_;
};

} // namespace ba
