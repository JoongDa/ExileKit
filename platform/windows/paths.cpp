#include "paths.h"
#include <windows.h>
#include <atomic>
#include <shlobj.h>
namespace poetoolbox {
namespace {
bool ValidAbsoluteDirectory(const std::filesystem::path &path) {
    const auto &text = path.native();
    if (text.empty() || !path.is_absolute() || text.size() > 32000 || text.find(L'\0') != text.npos)
        return false;
    // Do not accept device namespaces, alternate streams, wildcards, or ambiguous Win32 names.
    if (text.starts_with(L"\\\\?\\") || text.starts_with(L"\\\\.\\") || text.find_first_of(L"<>\"|?*") != text.npos)
        return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] < 32 || (text[i] == L':' && i != 1))
            return false;
    }
    for (const auto &part : path.relative_path()) {
        const auto &name = part.native();
        if (name == L".." || (!name.empty() && name != L"." && (name.back() == L'.' || name.back() == L' ')))
            return false;
    }
    return true;
}
} // namespace
Result<PathManager> PathManager::Create(std::filesystem::path dataRoot) {
    if (!dataRoot.empty()) {
        if (!ValidAbsoluteDirectory(dataRoot))
            return std::unexpected(Error{ErrorCode::InvalidPath, "Invalid application data root."});
        return PathManager(std::move(dataRoot));
    }
    PWSTR local = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local);
    if (FAILED(hr))
        return std::unexpected(Error{ErrorCode::IoError, "Cannot find LocalAppData.", static_cast<std::uint32_t>(hr)});
    std::filesystem::path root(local);
    CoTaskMemFree(local);
    PathManager paths(root / L"ExileKit");
    paths.legacyConfig_ = root / L"POEToolbox" / L"Config" / L"settings.json";
    return paths;
}
Status PathManager::EnsureDirectories() const {
    for (const auto &directory : {ConfigDirectory(), LogsDirectory(), DownloadsDirectory(), IconCacheDirectory(),
                                  DefaultManagedToolsDirectory()}) {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error || !std::filesystem::is_directory(directory, error))
            return std::unexpected(Error{ErrorCode::IoError, "Cannot create application data directory.",
                                         static_cast<std::uint32_t>(error.value())});
    }
    return {};
}
Status PathManager::ValidateManagedDirectory(const std::filesystem::path &directory) {
    if (!ValidAbsoluteDirectory(directory))
        return std::unexpected(Error{ErrorCode::InvalidPath, "Choose a valid absolute folder path."});
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error || !std::filesystem::is_directory(directory, error))
        return std::unexpected(Error{ErrorCode::InvalidPath, "The folder does not exist and cannot be created.",
                                     static_cast<std::uint32_t>(error.value())});
    static std::atomic<unsigned long> counter{0};
    const auto probe = directory / (L".poetoolbox-write-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                    std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(counter++));
    const HANDLE handle = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                      FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return std::unexpected(Error{ErrorCode::AccessDenied, "The selected folder is not writable.", GetLastError()});
    const char byte = 'x';
    DWORD written = 0;
    const BOOL ok = WriteFile(handle, &byte, 1, &written, nullptr);
    const DWORD nativeError = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(handle);
    if (!ok || written != 1)
        return std::unexpected(Error{ErrorCode::AccessDenied, "The selected folder is not writable.", nativeError});
    return {};
}
std::string SystemDefaultLanguage() {
    const LANGID language = GetUserDefaultUILanguage();
    if (PRIMARYLANGID(language) == LANG_CHINESE &&
        (SUBLANGID(language) == SUBLANG_CHINESE_SIMPLIFIED || SUBLANGID(language) == SUBLANG_CHINESE_SINGAPORE))
        return "zh-CN";
    return "en-US";
}
} // namespace poetoolbox
