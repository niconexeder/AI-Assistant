#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ba {

struct HttpRequest {
    std::string Url;
    std::string Method = "POST";
    std::string Body;
    std::string UserAgent = "buyer-assistant/1.0";
    std::string Authorization;
    std::vector<std::pair<std::string, std::string>> Headers;
    int TimeoutSeconds = 20;
};

struct HttpResponse {
    long StatusCode = 0;
    std::string Body;
};

class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;
    virtual HttpResponse Send(const HttpRequest& request) = 0;
};

std::unique_ptr<IHttpTransport> CreateHttpTransport();

} // namespace ba
