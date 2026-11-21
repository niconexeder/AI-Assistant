#pragma once

#include <stdexcept>
#include <string>

namespace ba {

enum class ErrorKind {
    Usage,
    Input,
    Configuration,
    Catalog,
    ExternalService,
    Internal
};

class AppError : public std::runtime_error {
public:
    AppError(ErrorKind kind, const std::string& message)
        : std::runtime_error(message), Kind_(kind) {}

    ErrorKind Kind() const noexcept { return Kind_; }

private:
    ErrorKind Kind_;
};

inline int ExitCodeFor(ErrorKind kind) {
    switch (kind) {
    case ErrorKind::Usage:
        return 2;
    case ErrorKind::Input:
    case ErrorKind::Configuration:
    case ErrorKind::Catalog:
        return 3;
    case ErrorKind::ExternalService:
        return 4;
    case ErrorKind::Internal:
        return 1;
    }
    return 1;
}

} // namespace ba
