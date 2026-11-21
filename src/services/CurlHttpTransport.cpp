#include "ba/services/HttpTransport.hpp"

#ifndef _WIN32

#include <curl/curl.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <string>

#include "ba/utils/Errors.hpp"

namespace ba {

namespace {

constexpr std::size_t MaxResponseBytes = 4U * 1024U * 1024U;

std::once_flag InitFlag;

void EnsureCurlInitialized() {
    std::call_once(InitFlag, [] {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            throw AppError(ErrorKind::ExternalService,
                           "Не удалось инициализировать libcurl");
        }
    });
}

size_t WriteCallback(char* data, size_t size, size_t count, void* userData) {
    auto* body = static_cast<std::string*>(userData);
    const std::size_t bytes = size * count;
    if (body->size() + bytes > MaxResponseBytes) {
        return 0;
    }
    body->append(data, bytes);
    return bytes;
}

void RequireHeaderSafe(const std::string& value) {
    for (const unsigned char ch : value) {
        if (ch == '\r' || ch == '\n' || ch == 0) {
            throw AppError(ErrorKind::Configuration,
                           "Заголовок HTTP содержит недопустимые символы");
        }
    }
}

} // namespace

class CurlHttpTransport final : public IHttpTransport {
public:
    HttpResponse Send(const HttpRequest& request) override {
        EnsureCurlInitialized();

        CURL* raw = curl_easy_init();
        if (raw == nullptr) {
            throw AppError(ErrorKind::ExternalService, "curl_easy_init завершился ошибкой");
        }
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(raw, &curl_easy_cleanup);

        curl_slist* rawHeaders = nullptr;
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(rawHeaders,
                                                                            &curl_slist_free_all);
        auto appendHeader = [&](const std::string& line) {
            RequireHeaderSafe(line);
            rawHeaders = curl_slist_append(rawHeaders, line.c_str());
            if (rawHeaders == nullptr) {
                throw AppError(ErrorKind::ExternalService,
                               "Не удалось сформировать HTTP-заголовки");
            }
            headers.reset(rawHeaders);
        };

        appendHeader("Accept: application/json");
        if (!request.Body.empty()) {
            appendHeader("Content-Type: application/json; charset=utf-8");
        }
        if (!request.Authorization.empty()) {
            appendHeader("Authorization: " + request.Authorization);
        }
        for (const auto& header : request.Headers) {
            appendHeader(header.first + ": " + header.second);
        }

        HttpResponse response;
        char errorBuffer[CURL_ERROR_SIZE] = {0};

        curl_easy_setopt(handle.get(), CURLOPT_URL, request.Url.c_str());
        curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, request.UserAgent.c_str());
        curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, headers.get());
        curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, static_cast<long>(request.TimeoutSeconds));
        curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT,
                         static_cast<long>(request.TimeoutSeconds));
        curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, &WriteCallback);
        curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &response.Body);
        curl_easy_setopt(handle.get(), CURLOPT_ERRORBUFFER, errorBuffer);
        curl_easy_setopt(handle.get(), CURLOPT_MAXFILESIZE_LARGE,
                         static_cast<curl_off_t>(MaxResponseBytes));

        if (request.Method == "POST") {
            curl_easy_setopt(handle.get(), CURLOPT_POST, 1L);
            curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, request.Body.data());
            curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                             static_cast<curl_off_t>(request.Body.size()));
        } else {
            curl_easy_setopt(handle.get(), CURLOPT_CUSTOMREQUEST, request.Method.c_str());
        }

        const auto code = curl_easy_perform(handle.get());
        if (code != CURLE_OK) {
            const std::string detail =
                errorBuffer[0] != '\0' ? std::string(errorBuffer) : curl_easy_strerror(code);
            throw AppError(ErrorKind::ExternalService, "HTTP-запрос не выполнен: " + detail);
        }

        long status = 0;
        curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
        response.StatusCode = status;
        return response;
    }
};

std::unique_ptr<IHttpTransport> CreateHttpTransport() {
    return std::make_unique<CurlHttpTransport>();
}

} // namespace ba

#endif // !_WIN32
