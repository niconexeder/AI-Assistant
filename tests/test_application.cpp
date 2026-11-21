#include "TestHarness.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "TestData.hpp"
#include "ba/app/Application.hpp"
#include "ba/services/HttpTransport.hpp"
#include "ba/utils/Errors.hpp"
#include "ba/utils/JsonIO.hpp"

namespace {

std::string SourcePath(const std::string& relative) {
    return std::string(BA_SOURCE_DIR) + "/" + relative;
}

nlohmann::json ReadJsonFile(const std::string& path) {
    return nlohmann::json::parse(ba::jsonio::ReadFile(path, 4U * 1024U * 1024U));
}

ba::Config OffConfig() {
    auto config = ba::DefaultConfig();
    config.Llm.Mode = ba::LlmMode::Off;
    config.Llm.ApiKey.clear();
    return config;
}

class FakeTransport : public ba::IHttpTransport {
public:
    ba::HttpResponse Send(const ba::HttpRequest& request) override {
        Requests.push_back(request);
        if (Responses.empty()) {
            throw ba::AppError(ba::ErrorKind::ExternalService, "no queued response");
        }
        auto response = Responses.front();
        Responses.pop_front();
        return response;
    }

    std::vector<ba::HttpRequest> Requests;
    std::deque<ba::HttpResponse> Responses;
};

ba::HttpResponse ChatContent(const nlohmann::json& content) {
    nlohmann::json envelope;
    envelope["choices"] = nlohmann::json::array();
    envelope["choices"].push_back(
        {{"message", {{"content", content.dump()}}}, {"finish_reason", "stop"}});
    return ba::HttpResponse{200, envelope.dump()};
}

nlohmann::json MiniCatalog() {
    return nlohmann::json::array(
        {testdata::ListingJson("t1", "table", 3000, "taganrog", {{"width_cm", 80}},
                               "СЕКРЕТНЫЙ ТОВАР", "Не показывать это описание"),
         testdata::ListingJson("c1", "chair", 2000, "taganrog",
                               {{"adjustable_height", true}}),
         testdata::ListingJson("l1", "lamp", 1000, "taganrog",
                               {{"color_temperature_k", 4000}})});
}

nlohmann::json LlmFactsResponse() {
    return nlohmann::json{{"city_id", "taganrog"},
                          {"budget_rub", 6000},
                          {"categories", {"table", "chair", "lamp"}},
                          {"table_width_max_cm", 80},
                          {"chair_adjustable_height", "yes"},
                          {"lamp_color_temperature_k", 4000}};
}

struct ApplicationFixture {
    ApplicationFixture()
        : CatalogFile("catalog.json", MiniCatalog().dump(2)),
          App(CatalogFile.String(), OffConfig()) {}

    testdata::TempFile CatalogFile;
    ba::Application App;
};

} // namespace

TEST_CASE(ApplicationParsesValidMessages) {
    auto messages = ba::Application::ParseMessages(
        testdata::UserRequest("Нужен стол"), 1024U * 1024U);
    CHECK_EQ(std::size_t(1), messages.size());
    CHECK(messages[0].Speaker == ba::Role::User);
    CHECK_EQ(std::string("Нужен стол"), messages[0].Text);

    const nlohmann::json dialogue = testdata::Dialogue(
        {{ba::Role::User, "Нужен стол"}, {ba::Role::Assistant, "Какой город?"},
         {ba::Role::User, "Таганрог"}});
    messages = ba::Application::ParseMessages(dialogue, 1024U * 1024U);
    CHECK_EQ(std::size_t(3), messages.size());
    CHECK(messages[1].Speaker == ba::Role::Assistant);
}

TEST_CASE(ApplicationRejectsInvalidMessageLists) {
    const auto reject = [](const nlohmann::json& request, std::size_t limit = 1024U * 1024U) {
        try {
            (void)ba::Application::ParseMessages(request, limit);
            return false;
        } catch (const ba::AppError& error) {
            return error.Kind() == ba::ErrorKind::Input;
        }
    };

    CHECK(reject(nlohmann::json::array()));
    CHECK(reject(nlohmann::json::object()));
    CHECK(reject(nlohmann::json{{"messages", "user"}}));
    CHECK(reject(nlohmann::json{{"messages", nlohmann::json::array()}}));
    CHECK(reject(nlohmann::json{{"messages", nlohmann::json::array({1})}}));
    CHECK(reject(nlohmann::json{{"messages", nlohmann::json::array(
                                    {{{"role", "assistant"}, {"text", "hi"}}})}}));
    CHECK(reject(nlohmann::json{
        {"messages", nlohmann::json::array({{{"role", "user"}, {"text", "a"}},
                                            {{"role", "user"}, {"text", "b"}}})}}));
    CHECK(reject(nlohmann::json{
        {"messages", nlohmann::json::array({{{"role", "user"}, {"text", "a"}},
                                            {{"role", "assistant"}, {"text", "b"}}})}}));
    CHECK(reject(nlohmann::json{{"messages",
                                 nlohmann::json::array(
                                     {{{"role", "user"}, {"text", "a"}, {"extra", 1}}})}}));
    CHECK(reject(nlohmann::json{{"messages", nlohmann::json::array({{{"text", "a"}}})}}));
    CHECK(reject(nlohmann::json{{"messages", nlohmann::json::array({{{"role", "user"}}})}}));
    CHECK(reject(nlohmann::json{
        {"messages", nlohmann::json::array({{{"role", 1}, {"text", "a"}}})}}));
    CHECK(reject(nlohmann::json{
        {"messages", nlohmann::json::array({{{"role", "system"}, {"text", "a"}}})}}));
    CHECK(reject(nlohmann::json{
        {"messages", nlohmann::json::array({{{"role", "user"}, {"text", ""}}})}}));
    CHECK(reject(nlohmann::json{{"messages", "not-an-array"}, {"extra", true}}));
    CHECK(reject(testdata::UserRequest("Нужен стол"), 10));
}

TEST_CASE(ApplicationEnforcesMessageLimit) {
    const auto makeDialogue = [](int count) {
        nlohmann::json messages = nlohmann::json::array();
        for (int i = 0; i < count; ++i) {
            messages.push_back(
                {{"role", i % 2 == 0 ? "user" : "assistant"}, {"text", std::to_string(i)}});
        }
        return nlohmann::json{{"messages", messages}};
    };

    CHECK_EQ(std::size_t(19),
             ba::Application::ParseMessages(makeDialogue(19), 1024U * 1024U).size());
    CHECK_THROWS_AS(ba::Application::ParseMessages(makeDialogue(20), 1024U * 1024U),
                    ba::AppError);
    CHECK_THROWS_AS(ba::Application::ParseMessages(makeDialogue(21), 1024U * 1024U),
                    ba::AppError);
}

TEST_CASE(ApplicationSelectsValidBundleForPublicExample) {
    ba::Application app(SourcePath("data/catalog.json"), OffConfig());
    const auto request = ReadJsonFile(SourcePath("examples/01-selection/request.json"));
    const auto response = app.Handle(request);
    const auto json = response.ToJson();

    CHECK_EQ(std::size_t(2), json.size());
    CHECK_EQ(std::string("ok"), json["status"].get<std::string>());
    CHECK(!json["options"].empty());
    CHECK(json["options"].size() <= 3);

    std::int64_t previousTotal = 0;
    for (const auto& option : json["options"]) {
        CHECK_EQ(std::size_t(3), option.size());
        CHECK_EQ(std::size_t(3), option["listing_ids"].size());
        CHECK(option["total_rub"].get<std::int64_t>() <= 15000);
        CHECK(option["total_rub"].get<std::int64_t>() >= previousTotal);
        previousTotal = option["total_rub"].get<std::int64_t>();

        std::int64_t sum = 0;
        std::vector<ba::Category> categories;
        for (const auto& id : option["listing_ids"]) {
            const auto* listing = app.CatalogData().FindById(id.get<std::string>());
            CHECK(listing != nullptr);
            CHECK_EQ(std::string("taganrog"), listing->CityId);
            CHECK(std::find(categories.begin(), categories.end(), listing->CategoryValue) ==
                  categories.end());
            categories.push_back(listing->CategoryValue);
            if (listing->CategoryValue == ba::Category::Table) {
                CHECK(listing->WidthCm.has_value());
                CHECK(*listing->WidthCm <= 100);
            }
            sum += listing->PriceRub;
        }
        CHECK_EQ(sum, option["total_rub"].get<std::int64_t>());
        CHECK_EQ(std::size_t(3), categories.size());
        CHECK(option["explanation"].get<std::string>().find("Итого") != std::string::npos);
    }
}

TEST_CASE(ApplicationAsksBudgetForPublicExample) {
    ba::Application app(SourcePath("data/catalog.json"), OffConfig());
    const auto request = ReadJsonFile(SourcePath("examples/02-clarification/request.json"));
    const auto json = app.Handle(request).ToJson();

    CHECK_EQ(std::size_t(2), json.size());
    CHECK_EQ(std::string("clarify"), json["status"].get<std::string>());
    CHECK_EQ(std::size_t(1), json["questions"].size());
    CHECK_EQ(std::string("budget_rub"), json["questions"][0]["field"].get<std::string>());
}

TEST_CASE(ApplicationClarifiesMissingCity) {
    ba::Application app(SourcePath("data/catalog.json"), OffConfig());
    const auto json = app.Handle(testdata::UserRequest(
                                     "Нужны стол, кресло и лампа. До 15000 рублей."))
                          .ToJson();
    CHECK_EQ(std::string("clarify"), json["status"].get<std::string>());
    CHECK_EQ(std::size_t(1), json["questions"].size());
    CHECK_EQ(std::string("city_id"), json["questions"][0]["field"].get<std::string>());
}

TEST_CASE(ApplicationClarifiesUnknownCity) {
    ba::Application app(SourcePath("data/catalog.json"), OffConfig());
    const auto json =
        app.Handle(testdata::UserRequest("Нужен стол в Москве до 15000 рублей")).ToJson();
    CHECK_EQ(std::string("clarify"), json["status"].get<std::string>());
    CHECK_EQ(std::string("city_id"), json["questions"][0]["field"].get<std::string>());
}

TEST_CASE(ApplicationClarifiesMissingCategories) {
    ba::Application app(SourcePath("data/catalog.json"), OffConfig());
    const auto json =
        app.Handle(testdata::UserRequest("Таганрог, до 15000 рублей")).ToJson();
    CHECK_EQ(std::string("clarify"), json["status"].get<std::string>());
    CHECK_EQ(std::string("categories"), json["questions"][0]["field"].get<std::string>());
}

TEST_CASE(ApplicationReportsNoMatchWithoutIdentifiers) {
    ba::Application app(SourcePath("data/catalog.json"), OffConfig());
    const auto json =
        app.Handle(testdata::UserRequest(
                       "Стол, кресло и лампа. Таганрог. До 100 рублей."))
            .ToJson();
    CHECK_EQ(std::size_t(2), json.size());
    CHECK_EQ(std::string("no_match"), json["status"].get<std::string>());
    CHECK(!json.contains("options"));
    CHECK(!json.contains("questions"));
    CHECK(json["message"].get<std::string>().find("100 ₽") != std::string::npos);
}

TEST_CASE(ApplicationLoadsDirectoriesBesideCatalog) {
    testdata::TempDir directory("buyer-assistant-app");
    const auto catalog = directory.WriteJson(
        "catalog.json",
        nlohmann::json::array({testdata::ListingJson("x1", "table", 4000, "test-city",
                                                     {{"width_cm", 80}})}));
    directory.WriteJson("cities.json",
                        nlohmann::json::array(
                            {nlohmann::json{{"id", "test-city"}, {"name", "Тестоград"}}}));
    directory.WriteJson(
        "categories.json",
        nlohmann::json::array(
            {nlohmann::json{{"id", "table"}, {"name", "Стол"}, {"attributes", nlohmann::json::object({{"width_cm", "см"}})}},
             nlohmann::json{{"id", "chair"}, {"name", "Кресло"}, {"attributes", nlohmann::json::object()}},
             nlohmann::json{{"id", "lamp"}, {"name", "Лампа"}, {"attributes", nlohmann::json::object()}}}));

    ba::Application app(catalog, OffConfig());
    const auto json =
        app.Handle(testdata::UserRequest("Нужен стол в Тестограде до 5000 рублей")).ToJson();
    CHECK_EQ(std::string("ok"), json["status"].get<std::string>());
    CHECK_EQ(std::string("x1"),
             json["options"][0]["listing_ids"][0].get<std::string>());
    CHECK_EQ(std::int64_t(4000), json["options"][0]["total_rub"].get<std::int64_t>());
}

TEST_CASE(ApplicationUsesLlmToFillMissingBudget) {
    ApplicationFixture fixture;
    auto config = OffConfig();
    config.Llm.Mode = ba::LlmMode::Always;
    config.Llm.ApiKey = "test-key";
    config.Llm.MaxRetries = 0;
    auto transport = std::make_shared<FakeTransport>();
    transport->Responses.push_back(ChatContent(LlmFactsResponse()));

    ba::Application app(fixture.CatalogFile.String(), config, false, false, transport);
    const auto json =
        app.Handle(testdata::UserRequest("Нужны стол, кресло и лампа в Таганроге")).ToJson();

    CHECK_EQ(std::string("ok"), json["status"].get<std::string>());
    CHECK_EQ(std::int64_t(6000), json["options"][0]["total_rub"].get<std::int64_t>());
    CHECK_EQ(std::size_t(1), transport->Requests.size());
    CHECK(transport->Requests[0].Body.find("СЕКРЕТНЫЙ ТОВАР") == std::string::npos);
    CHECK(transport->Requests[0].Body.find("Не показывать") == std::string::npos);
}

TEST_CASE(ApplicationDeterministicBudgetBeatsLlm) {
    ApplicationFixture fixture;
    auto config = OffConfig();
    config.Llm.Mode = ba::LlmMode::Always;
    config.Llm.ApiKey = "test-key";
    config.Llm.MaxRetries = 0;
    auto transport = std::make_shared<FakeTransport>();
    auto facts = LlmFactsResponse();
    facts["budget_rub"] = 1000;
    transport->Responses.push_back(ChatContent(facts));

    ba::Application app(fixture.CatalogFile.String(), config, false, false, transport);
    const auto json = app.Handle(testdata::UserRequest(
                                     "Нужны стол, кресло и лампа в Таганроге. До 7000 рублей"))
                          .ToJson();

    CHECK_EQ(std::string("ok"), json["status"].get<std::string>());
    CHECK_EQ(std::int64_t(6000), json["options"][0]["total_rub"].get<std::int64_t>());
}

TEST_CASE(ApplicationDeterministicCityBeatsLlm) {
    ApplicationFixture fixture;
    auto config = OffConfig();
    config.Llm.Mode = ba::LlmMode::Always;
    config.Llm.ApiKey = "test-key";
    config.Llm.MaxRetries = 0;
    auto transport = std::make_shared<FakeTransport>();
    auto facts = LlmFactsResponse();
    facts["city_id"] = "azov";
    transport->Responses.push_back(ChatContent(facts));

    ba::Application app(fixture.CatalogFile.String(), config, false, false, transport);
    const auto json = app.Handle(testdata::UserRequest(
                                     "Нужны стол, кресло и лампа в Таганроге. До 7000 рублей"))
                          .ToJson();
    CHECK_EQ(std::string("ok"), json["status"].get<std::string>());
    for (const auto& id : json["options"][0]["listing_ids"]) {
        CHECK(app.CatalogData().FindById(id.get<std::string>())->CityId == "taganrog");
    }
}

TEST_CASE(ApplicationAutoModeWithoutKeyStaysDeterministic) {
    ApplicationFixture fixture;
    auto config = OffConfig();
    config.Llm.Mode = ba::LlmMode::Auto;
    config.Llm.ApiKey.clear();
    auto transport = std::make_shared<FakeTransport>();

    ba::Application app(fixture.CatalogFile.String(), config, false, false, transport);
    const auto json =
        app.Handle(testdata::UserRequest("Нужны стол, кресло и лампа в Таганроге")).ToJson();
    CHECK_EQ(std::string("clarify"), json["status"].get<std::string>());
    CHECK(transport->Requests.empty());
}

TEST_CASE(ApplicationRejectsAlwaysModeWithoutKey) {
    ApplicationFixture fixture;
    auto config = OffConfig();
    config.Llm.Mode = ba::LlmMode::Always;
    config.Llm.ApiKey.clear();
    try {
        ba::Application app(fixture.CatalogFile.String(), config);
        CHECK(false);
    } catch (const ba::AppError& error) {
        CHECK(error.Kind() == ba::ErrorKind::Configuration);
    }
}

TEST_CASE(ApplicationRejectsRequiredLlmWithoutKey) {
    ApplicationFixture fixture;
    auto config = OffConfig();
    config.Llm.Mode = ba::LlmMode::Auto;
    config.Llm.Required = true;
    config.Llm.ApiKey.clear();
    try {
        ba::Application app(fixture.CatalogFile.String(), config);
        CHECK(false);
    } catch (const ba::AppError& error) {
        CHECK(error.Kind() == ba::ErrorKind::Configuration);
    }
}

TEST_CASE(ApplicationPropagatesLlmFailures) {
    ApplicationFixture fixture;
    auto config = OffConfig();
    config.Llm.Mode = ba::LlmMode::Always;
    config.Llm.ApiKey = "test-key";
    config.Llm.MaxRetries = 0;
    auto transport = std::make_shared<FakeTransport>();
    transport->Responses.push_back(ba::HttpResponse{500, "server error"});

    ba::Application app(fixture.CatalogFile.String(), config, false, false, transport);
    try {
        (void)app.Handle(
            testdata::UserRequest("Нужны стол, кресло и лампа в Таганроге"));
        CHECK(false);
    } catch (const ba::AppError& error) {
        CHECK(error.Kind() == ba::ErrorKind::ExternalService);
    }
}

TEST_CASE(ApplicationHonoursConfiguredOptionLimit) {
    auto config = OffConfig();
    config.App.MaxOptions = 1;
    ba::Application app(SourcePath("data/catalog.json"), config);
    const auto request = ReadJsonFile(SourcePath("examples/01-selection/request.json"));
    const auto json = app.Handle(request).ToJson();
    CHECK_EQ(std::string("ok"), json["status"].get<std::string>());
    CHECK_EQ(std::size_t(1), json["options"].size());
}

TEST_CASE(ApplicationPropagatesCatalogErrors) {
    try {
        ba::Application app("no-such-catalog.json", OffConfig());
        CHECK(false);
    } catch (const ba::AppError& error) {
        CHECK(error.Kind() == ba::ErrorKind::Catalog);
    }
}
