#include "launcher.h"
#include "custom_shortcut.h"
#include "utf.h"
#include <windows.h>
#include <algorithm>
#include <map>
#include <shellapi.h>
namespace poetoolbox {
namespace {
Error NativeLaunchError(DWORD error, std::string message) {
    const auto code =
        error == ERROR_ACCESS_DENIED || error == ERROR_ELEVATION_REQUIRED
            ? ErrorCode::AccessDenied
            : (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? ErrorCode::ExecutableNotFound
                                                                              : ErrorCode::ProcessCreationFailed);
    return {code, std::move(message), error};
}
class WindowsLaunchSystem final : public LaunchSystem {
    Status OpenUrl(std::wstring_view url) override {
        const std::wstring ownedUrl(url);
        SHELLEXECUTEINFOW info{sizeof(info)};
        info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
        info.lpVerb = L"open";
        info.lpFile = ownedUrl.c_str();
        info.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&info))
            return std::unexpected(NativeLaunchError(GetLastError(), "Windows could not open the URL."));
        return {};
    }
    Status StartProcess(const LaunchRequest &request) override {
        std::wstring commandLine = request.commandLine;
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(request.executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                            CREATE_UNICODE_ENVIRONMENT,
                            request.environment.empty() ? nullptr : const_cast<wchar_t *>(request.environment.data()),
                            request.workingDirectory.c_str(), &startup, &process))
            return std::unexpected(NativeLaunchError(GetLastError(), "Windows could not start the application."));
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return {};
    }
};
struct EnvironmentLess {
    bool operator()(const std::wstring &left, const std::wstring &right) const {
        return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()), right.c_str(),
                                    static_cast<int>(right.size()), TRUE) == CSTR_LESS_THAN;
    }
};
bool HasNull(std::string_view value) {
    return value.find('\0') != value.npos;
}
bool SafeExePath(const std::filesystem::path &path) {
    const auto &native = path.native();
    if (!path.is_absolute() || native.find(L'\0') != native.npos || native.find_first_of(L"<>\"|?*") != native.npos)
        return false;
    if (native.starts_with(L"\\\\.\\") || native.starts_with(L"\\\\?\\"))
        return false;
    for (std::size_t i = 0; i < native.size(); ++i)
        if (native[i] < 32 || (native[i] == L':' && i != 1))
            return false;
    return _wcsicmp(path.extension().c_str(), L".exe") == 0;
}
} // namespace
std::wstring QuoteWindowsArgument(std::wstring_view argument) {
    // Microsoft CRT argv grammar: backslashes double only before quotes and the final quote.
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'\"')
            result.append(slashes * 2 + 1, L'\\');
        else
            result.append(slashes, L'\\');
        result.push_back(c);
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}
Result<LaunchRequest> ToolLauncher::PrepareApplication(const ToolManifest &tool,
                                                       const std::filesystem::path &configuredExe) {
    if (tool.type != ToolType::Application)
        return std::unexpected(Error{ErrorCode::InvalidManifest, "The tool is not an application."});
    if (configuredExe.empty())
        return std::unexpected(Error{ErrorCode::ExecutableNotFound, "Select the application executable first."});
    if (!SafeExePath(configuredExe))
        return std::unexpected(Error{ErrorCode::InvalidPath, "Select an absolute .exe application path."});
    std::error_code error;
    if (!std::filesystem::is_regular_file(configuredExe, error))
        return std::unexpected(Error{ErrorCode::ExecutableNotFound, "The configured executable no longer exists.",
                                     static_cast<std::uint32_t>(error.value())});
    LaunchRequest request;
    request.executable = configuredExe.lexically_normal();
    request.workingDirectory = request.executable.parent_path();
    if (!tool.launch.workingDirectory.empty()) {
        const std::wstring directory = Utf16(tool.launch.workingDirectory);
        if (directory.empty() || HasNull(tool.launch.workingDirectory))
            return std::unexpected(Error{ErrorCode::InvalidManifest, "The working directory is not valid UTF-8."});
        const std::filesystem::path specified(directory);
        request.workingDirectory =
            (specified.is_absolute() ? specified : request.workingDirectory / specified).lexically_normal();
    }
    if (!std::filesystem::is_directory(request.workingDirectory, error))
        return std::unexpected(Error{ErrorCode::InvalidPath, "The working directory does not exist.",
                                     static_cast<std::uint32_t>(error.value())});
    request.commandLine = QuoteWindowsArgument(request.executable.native());
    for (const auto &argument : tool.launch.arguments) {
        const auto wide = Utf16(argument);
        if (HasNull(argument) || (!argument.empty() && wide.empty()))
            return std::unexpected(Error{ErrorCode::InvalidManifest, "An application argument is not valid UTF-8."});
        request.commandLine += L" " + QuoteWindowsArgument(wide);
    }
    if (request.commandLine.size() >= 32767)
        return std::unexpected(
            Error{ErrorCode::InvalidManifest, "The application command line exceeds the Windows limit."});
    if (!tool.launch.environment.empty()) {
        std::map<std::wstring, std::wstring, EnvironmentLess> variables;
        LPWCH block = GetEnvironmentStringsW();
        if (!block)
            return std::unexpected(Error{ErrorCode::IoError, "Cannot read the current environment.", GetLastError()});
        for (const wchar_t *entry = block; *entry; entry += wcslen(entry) + 1) {
            const std::wstring value(entry);
            // Windows includes drive-current-directory entries whose names start with '='.
            const auto equal = value.find(L'=', value.front() == L'=' ? 1 : 0);
            if (equal != value.npos)
                variables.insert_or_assign(value.substr(0, equal), value.substr(equal + 1));
        }
        FreeEnvironmentStringsW(block);
        for (const auto &[name, value] : tool.launch.environment) {
            const auto wideName = Utf16(name), wideValue = Utf16(value);
            if (name.empty() || wideName.empty() || name.find('=') != name.npos || HasNull(name) || HasNull(value) ||
                (!value.empty() && wideValue.empty()))
                return std::unexpected(Error{ErrorCode::InvalidManifest, "An environment variable is invalid."});
            variables.insert_or_assign(wideName, wideValue);
        }
        for (const auto &[name, value] : variables) {
            request.environment.insert(request.environment.end(), name.begin(), name.end());
            request.environment.push_back(L'=');
            request.environment.insert(request.environment.end(), value.begin(), value.end());
            request.environment.push_back(L'\0');
        }
        request.environment.push_back(L'\0');
        if (request.environment.size() > 32767)
            return std::unexpected(
                Error{ErrorCode::InvalidManifest, "The application environment exceeds the supported limit."});
    }
    return request;
}
Status ToolLauncher::Launch(const ToolManifest &tool, const std::filesystem::path &configuredExe,
                            bool riskAccepted) const {
    if (tool.schemaVersion != 1 || !IsValidToolId(tool.id))
        return std::unexpected(Error{ErrorCode::InvalidManifest, "The tool manifest is invalid."});
    if (tool.riskLevel == RiskLevel::GameModifying && !riskAccepted)
        return std::unexpected(
            Error{ErrorCode::AccessDenied,
                  "This tool modifies Path of Exile game files. Explicit confirmation is required."});
    WindowsLaunchSystem windows;
    auto *system = system_ ? system_ : &windows;
    if (tool.type == ToolType::Web || tool.type == ToolType::ExternalLink) {
        if (!IsSafeWebUrl(tool.launch.url))
            return std::unexpected(Error{ErrorCode::InvalidURL, "Only a valid HTTPS URL can be opened."});
        const auto url = Utf16(tool.launch.url);
        if (url.empty())
            return std::unexpected(Error{ErrorCode::InvalidURL, "The URL is not valid UTF-8."});
        return system->OpenUrl(url);
    }
    if (tool.type == ToolType::Application) {
        auto request = PrepareApplication(tool, configuredExe);
        if (!request)
            return std::unexpected(request.error());
        return system->StartProcess(*request);
    }
    return std::unexpected(Error{ErrorCode::UnsupportedOperation, "This tool cannot be launched in this version."});
}
Status ToolLauncher::LaunchCustom(const CustomTool &tool) const {
    if (!IsValidToolId(tool.id) || tool.target.empty() || HasNull(tool.target))
        return std::unexpected(Error{ErrorCode::InvalidConfig, "The custom shortcut is invalid."});
    ToolManifest launch;
    launch.id = tool.id;
    if (tool.kind == ShortcutKind::Url) {
        launch.type = ToolType::Web;
        launch.launch.url = tool.target;
        return Launch(launch);
    }
    const auto wide = Utf16(tool.target);
    if (wide.empty())
        return std::unexpected(Error{ErrorCode::InvalidPath, "The application path is not valid UTF-8."});
    const std::filesystem::path path(wide);
    launch.type = ToolType::Application;
    if (tool.kind == ShortcutKind::Executable)
        return Launch(launch, path);
    if (tool.kind == ShortcutKind::WindowsShortcut) {
        const auto details = InspectWindowsShortcut(path);
        if (!details)
            return std::unexpected(details.error());
        launch.launch.arguments = details->arguments;
        launch.launch.workingDirectory = Utf8(details->workingDirectory.native());
        return Launch(launch, details->executable);
    }
    return std::unexpected(Error{ErrorCode::UnsupportedOperation, "The custom shortcut type is not supported."});
}
} // namespace poetoolbox
