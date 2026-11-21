#include "ba/services/HttpTransport.hpp"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

#include "ba/utils/Errors.hpp"

namespace ba {

namespace {

constexpr std::size_t MaxResponseBytes = 4U * 1024U * 1024U;

class WinHttpHandle {
public:
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) : Handle_(handle) {}
    ~WinHttpHandle() {
        if (Handle_ != nullptr) {
            WinHttpCloseHandle(Handle_);
        }
    }

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    WinHttpHandle(WinHttpHandle&& other) noexcept : Handle_(other.Handle_) {
        other.Handle_ = nullptr;
    }
    WinHttpHandle& operator=(WinHttpHandle&& other) noexcept {
        if (this != &other) {
            if (Handle_ != nullptr) {
                WinHttpCloseHandle(Handle_);
            }
            Handle_ = other.Handle_;
            other.Handle_ = nullptr;
        }
        return *this;
    }

    HINTERNET Get() const noexcept { return Handle_; }
    explicit operator bool() const noexcept { return Handle_ != nullptr; }

private:
    HINTERNET Handle_ = nullptr;
};

std::wstring ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        throw AppError(ErrorKind::ExternalService, "Некорректная UTF-8 строка в HTTP-запросе");
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string DescribeWinHttpError(DWORD code) {
    if (code == ERROR_WINHTTP_TIMEOUT) {
        return "истекло время ожидания HTTP-запроса";
    }
    if (code == ERROR_WINHTTP_NAME_NOT_RESOLVED) {
        return "не удалось разрешить имя хоста";
    }
    if (code == ERROR_WINHTTP_CANNOT_CONNECT) {
        return "не удалось подключиться к серверу";
    }
    if (code == ERROR_WINHTTP_SECURE_FAILURE) {
        return "ошибка защищённого соединения TLS";
    }
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS,
        GetModuleHandleW(L"winhttp.dll"), code, 0, reinterpret_cast<wchar_t*>(&buffer), 0,
        nullptr);
    std::string message;
    if (length > 0 && buffer != nullptr) {
        std::wstring wide(buffer, length);
        while (!wide.empty() && (wide.back() == L'\r' || wide.back() == L'\n' ||
                                 wide.back() == L' ' || wide.back() == L'.')) {
            wide.pop_back();
        }
        const int utf8Size = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                                 static_cast<int>(wide.size()), nullptr, 0,
                                                 nullptr, nullptr);
        if (utf8Size > 0) {
            message.resize(static_cast<std::size_t>(utf8Size));
            WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                message.data(), utf8Size, nullptr, nullptr);
        }
    }
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    if (message.empty()) {
        message = "код ошибки " + std::to_string(code);
    }
    return message;
}

struct ParsedUrl {
    bool Secure = true;
    std::wstring Host;
    INTERNET_PORT Port = 443;
    std::wstring Path = L"/";
};

ParsedUrl ParseUrl(const std::string& url) {
    ParsedUrl parsed;
    std::string rest;
    if (url.rfind("https://", 0) == 0) {
        parsed.Secure = true;
        rest = url.substr(8);
        parsed.Port = 443;
    } else if (url.rfind("http://", 0) == 0) {
        parsed.Secure = false;
        rest = url.substr(7);
        parsed.Port = 80;
    } else {
        throw AppError(ErrorKind::Configuration,
                       "URL должен начинаться с http:// или https://");
    }

    const auto slash = rest.find('/');
    const auto authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    const auto path = slash == std::string::npos ? std::string("/") : rest.substr(slash);
    if (authority.empty()) {
        throw AppError(ErrorKind::Configuration, "В URL отсутствует имя хоста");
    }

    std::string host = authority;
    const auto colon = authority.rfind(':');
    if (colon != std::string::npos) {
        const auto portText = authority.substr(colon + 1);
        if (portText.empty() ||
            portText.find_first_not_of("0123456789") != std::string::npos) {
            throw AppError(ErrorKind::Configuration, "Некорректный порт в URL");
        }
        const auto portValue = std::stoul(portText);
        if (portValue == 0 || portValue > 65535) {
            throw AppError(ErrorKind::Configuration, "Порт в URL вне диапазона");
        }
        parsed.Port = static_cast<INTERNET_PORT>(portValue);
        host = authority.substr(0, colon);
    }
    if (host.empty()) {
        throw AppError(ErrorKind::Configuration, "В URL отсутствует имя хоста");
    }

    parsed.Host = ToWide(host);
    parsed.Path = ToWide(path);
    return parsed;
}

std::wstring BuildHeaders(const HttpRequest& request) {
    std::wstring headers;
    auto add = [&](const std::string& line) {
        for (const unsigned char ch : line) {
            if (ch == '\r' || ch == '\n' || ch == 0) {
                throw AppError(ErrorKind::Configuration,
                               "Заголовок HTTP содержит недопустимые символы");
            }
        }
        headers += ToWide(line);
        headers += L"\r\n";
    };

    add("Accept: application/json");
    if (!request.Body.empty()) {
        add("Content-Type: application/json; charset=utf-8");
    }
    if (!request.Authorization.empty()) {
        add("Authorization: " + request.Authorization);
    }
    for (const auto& header : request.Headers) {
        add(header.first + ": " + header.second);
    }
    return headers;
}

} // namespace

class WinHttpTransport final : public IHttpTransport {
public:
    HttpResponse Send(const HttpRequest& request) override {
        const auto url = ParseUrl(request.Url);
        const auto userAgent = ToWide(request.UserAgent);

        WinHttpHandle session(WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session) {
            throw AppError(ErrorKind::ExternalService,
                           "WinHttpOpen: " + DescribeWinHttpError(GetLastError()));
        }

        const int timeoutMs = request.TimeoutSeconds * 1000;
        if (!WinHttpSetTimeouts(session.Get(), timeoutMs, timeoutMs, timeoutMs, timeoutMs)) {
            throw AppError(ErrorKind::ExternalService,
                           "WinHttpSetTimeouts: " + DescribeWinHttpError(GetLastError()));
        }

        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
        protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
        WinHttpSetOption(session.Get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols,
                         sizeof(protocols));

        WinHttpHandle connection(
            WinHttpConnect(session.Get(), url.Host.c_str(), url.Port, 0));
        if (!connection) {
            throw AppError(ErrorKind::ExternalService,
                           "WinHttpConnect: " + DescribeWinHttpError(GetLastError()));
        }

        const auto method = ToWide(request.Method);
        DWORD flags = url.Secure ? WINHTTP_FLAG_SECURE : 0;
        WinHttpHandle handle(WinHttpOpenRequest(connection.Get(), method.c_str(),
                                                url.Path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
        if (!handle) {
            throw AppError(ErrorKind::ExternalService,
                           "WinHttpOpenRequest: " + DescribeWinHttpError(GetLastError()));
        }

        DWORD disableRedirects = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption(handle.Get(), WINHTTP_OPTION_DISABLE_FEATURE, &disableRedirects,
                         sizeof(disableRedirects));

        const auto headers = BuildHeaders(request);
        const DWORD headersLength =
            headers.empty() ? 0 : static_cast<DWORD>(-1);
        const void* body = request.Body.empty() ? nullptr : request.Body.data();
        const DWORD bodyLength = static_cast<DWORD>(request.Body.size());
        if (!WinHttpSendRequest(handle.Get(),
                                headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS
                                                : headers.c_str(),
                                headersLength, const_cast<void*>(body), bodyLength,
                                bodyLength, 0)) {
            throw AppError(ErrorKind::ExternalService,
                           "WinHttpSendRequest: " + DescribeWinHttpError(GetLastError()));
        }
        if (!WinHttpReceiveResponse(handle.Get(), nullptr)) {
            throw AppError(ErrorKind::ExternalService,
                           "WinHttpReceiveResponse: " + DescribeWinHttpError(GetLastError()));
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        if (!WinHttpQueryHeaders(handle.Get(),
                                 WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                                 WINHTTP_NO_HEADER_INDEX)) {
            throw AppError(ErrorKind::ExternalService,
                           "WinHttpQueryHeaders: " + DescribeWinHttpError(GetLastError()));
        }

        HttpResponse response;
        response.StatusCode = static_cast<long>(statusCode);
        char buffer[8192];
        while (true) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(handle.Get(), &available)) {
                throw AppError(ErrorKind::ExternalService,
                               "WinHttpQueryDataAvailable: " +
                                   DescribeWinHttpError(GetLastError()));
            }
            if (available == 0) {
                break;
            }
            if (response.Body.size() + available > MaxResponseBytes) {
                throw AppError(ErrorKind::ExternalService,
                               "Ответ модели превышает допустимый размер");
            }
            DWORD read = 0;
            const DWORD toRead = static_cast<DWORD>(
                std::min<std::size_t>(available, sizeof(buffer)));
            if (!WinHttpReadData(handle.Get(), buffer, toRead, &read)) {
                throw AppError(ErrorKind::ExternalService,
                               "WinHttpReadData: " + DescribeWinHttpError(GetLastError()));
            }
            if (read == 0) {
                break;
            }
            response.Body.append(buffer, read);
        }
        return response;
    }
};

std::unique_ptr<IHttpTransport> CreateHttpTransport() {
    return std::make_unique<WinHttpTransport>();
}

} // namespace ba

#endif // _WIN32
