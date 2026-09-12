#pragma once
#include <cstdint>
#include <expected>
#include <string>
namespace poetoolbox {
enum class ErrorCode {
    IoError,
    InvalidManifest,
    UnsupportedSchema,
    DuplicateId,
    InvalidConfig,
    InvalidPath,
    ToolNotFound,
    ExecutableNotFound,
    InvalidURL,
    AccessDenied,
    ProcessCreationFailed,
    UnsupportedOperation,
    Cancelled,
    InvalidShortcut
};
struct Error {
    ErrorCode code;
    std::string message;
    std::uint32_t nativeCode = 0;
};
template <class T> using Result = std::expected<T, Error>;
using Status = Result<void>;
} // namespace poetoolbox
