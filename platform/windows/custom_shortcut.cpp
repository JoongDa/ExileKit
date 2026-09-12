#include "custom_shortcut.h"
#include "utf.h"
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cwctype>
#include <poetoolbox/tool.h>

namespace poetoolbox {
namespace {
constexpr std::size_t kWindowsTextLimit = 32768;

class ComScope {
  public:
    ComScope() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(result_))
            CoUninitialize();
    }
    bool Ready() const { return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE; }
    HRESULT ResultCode() const { return result_; }

  private:
    HRESULT result_;
};

Error ShortcutError(HRESULT result, std::string message) {
    return {ErrorCode::InvalidPath, std::move(message), static_cast<std::uint32_t>(result)};
}

std::string_view Trim(std::string_view text) {
    constexpr std::string_view whitespace = " \t\r\n";
    const auto first = text.find_first_not_of(whitespace);
    if (first == text.npos)
        return {};
    return text.substr(first, text.find_last_not_of(whitespace) - first + 1);
}

bool SafeAbsolutePath(const std::filesystem::path &path) {
    const auto &native = path.native();
    if (!path.is_absolute() || native.size() >= kWindowsTextLimit || native.find_first_of(L"<>\"|?*") != native.npos ||
        native.starts_with(L"\\\\.\\") || native.starts_with(L"\\\\?\\"))
        return false;
    for (std::size_t i = 0; i < native.size(); ++i)
        if (native[i] < 32 || native[i] == 127 || (native[i] == L':' && i != 1))
            return false;
    return true;
}

Status CheckFile(const std::filesystem::path &path, const wchar_t *extension) {
    if (!SafeAbsolutePath(path) || _wcsicmp(path.extension().c_str(), extension) != 0)
        return std::unexpected(Error{ErrorCode::InvalidPath, "Choose an absolute application or shortcut path."});
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
        return std::unexpected(Error{ErrorCode::ExecutableNotFound, "The selected file does not exist.",
                                     static_cast<std::uint32_t>(error.value())});
    return {};
}

std::filesystem::path ExpandPath(std::wstring_view text) {
    if (text.empty())
        return {};
    const std::wstring value(text);
    const DWORD required = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (!required || required > kWindowsTextLimit)
        return {};
    std::vector<wchar_t> expanded(required);
    const DWORD written = ExpandEnvironmentStringsW(value.c_str(), expanded.data(), required);
    return written && written <= required ? std::filesystem::path(expanded.data()).lexically_normal()
                                          : std::filesystem::path{};
}

std::string DisplayName(const std::filesystem::path &path) {
    DWORD ignored = 0;
    const DWORD bytes = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    // Version metadata is optional; oversized or malformed resources use the filename.
    if (bytes && bytes <= 2 * 1024 * 1024) {
        std::vector<std::byte> data(bytes);
        if (GetFileVersionInfoW(path.c_str(), 0, bytes, data.data())) {
            struct Translation {
                WORD language;
                WORD codePage;
            };
            Translation *translations = nullptr;
            UINT translationBytes = 0;
            if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void **>(&translations),
                               &translationBytes)) {
                const auto count = std::min<std::size_t>(translationBytes / sizeof(Translation), 128);
                for (const auto *property : {L"ProductName", L"FileDescription"}) {
                    for (std::size_t i = 0; i < count; ++i) {
                        wchar_t key[128]{};
                        swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\%s", translations[i].language,
                                   translations[i].codePage, property);
                        wchar_t *name = nullptr;
                        UINT characters = 0;
                        if (VerQueryValueW(data.data(), key, reinterpret_cast<void **>(&name), &characters) &&
                            characters > 1 && characters <= 257) {
                            std::wstring_view value(name, characters - 1);
                            if (std::none_of(value.begin(), value.end(), [](wchar_t c) { return c < 32; })) {
                                const auto utf8 = Utf8(value);
                                const auto trimmed = Trim(utf8);
                                if (!trimmed.empty())
                                    return std::string(trimmed);
                            }
                        }
                    }
                }
            }
        }
    }
    return Utf8(path.stem().native());
}

std::string HostName(std::string_view url) {
    const auto begin = url.find("://") + 3;
    auto authority = url.substr(begin, url.find_first_of("/?#", begin) - begin);
    if (authority.starts_with('['))
        return std::string(authority.substr(0, authority.find(']') + 1));
    const auto colon = authority.find(':');
    return std::string(authority.substr(0, colon));
}
std::string BoundedDisplayName(std::string name, std::string_view fallback) {
    // ConfigManager stores names as UTF-8 and limits bytes, not Windows UTF-16 code units.
    if (name.empty() || Utf16(name).empty())
        name = fallback;
    std::erase_if(name, [](unsigned char c) { return c < 32 || c == 127; });
    name = std::string(Trim(name));
    if (name.empty())
        name = fallback;
    if (name.size() > 512) {
        std::size_t boundary = 512;
        while (boundary && (static_cast<unsigned char>(name[boundary]) & 0xc0) == 0x80)
            --boundary;
        name.resize(boundary);
    }
    return name;
}
} // namespace

Result<ShortcutDetails> InspectWindowsShortcut(const std::filesystem::path &path) {
    if (const auto valid = CheckFile(path, L".lnk"); !valid)
        return std::unexpected(valid.error());
    // A shortcut should be small. Bound parsing input before passing it to the OS parser.
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > 2 * 1024 * 1024)
        return std::unexpected(Error{ErrorCode::InvalidPath, "The shortcut file is unreadable or too large."});
    ComScope com;
    if (!com.Ready())
        return std::unexpected(ShortcutError(com.ResultCode(), "Windows shortcut services are unavailable."));
    Microsoft::WRL::ComPtr<IShellLinkW> link;
    auto result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    if (FAILED(result))
        return std::unexpected(ShortcutError(result, "Windows could not read the shortcut."));
    Microsoft::WRL::ComPtr<IPersistFile> file;
    result = link.As(&file);
    if (FAILED(result))
        return std::unexpected(ShortcutError(result, "Windows could not read the shortcut."));
    result = file->Load(path.c_str(), STGM_READ);
    if (FAILED(result))
        return std::unexpected(ShortcutError(result, "The shortcut file is invalid."));
    // Deliberately do not call Resolve: that can search drives, query the network, or show UI.
    std::array<wchar_t, kWindowsTextLimit> target{}, arguments{}, directory{}, icon{};
    result = link->GetPath(target.data(), static_cast<int>(target.size()), nullptr, SLGP_RAWPATH);
    if (FAILED(result) || !target.front())
        return std::unexpected(ShortcutError(result, "The shortcut has no application target."));
    ShortcutDetails details;
    details.executable = ExpandPath(target.data());
    if (const auto valid = CheckFile(details.executable, L".exe"); !valid)
        return std::unexpected(valid.error());
    result = link->GetArguments(arguments.data(), static_cast<int>(arguments.size()));
    if (FAILED(result))
        return std::unexpected(ShortcutError(result, "Windows could not read shortcut arguments."));
    if (arguments.front()) {
        // Parse Windows argv once, then the launcher quotes each argument for CreateProcessW.
        const std::wstring command = std::wstring(L"shortcut-target.exe ") + arguments.data();
        int count = 0;
        auto parsed = CommandLineToArgvW(command.c_str(), &count);
        if (!parsed)
            return std::unexpected(
                Error{ErrorCode::InvalidPath, "The shortcut arguments are invalid.", GetLastError()});
        bool validUnicode = true;
        for (int i = 1; i < count; ++i) {
            auto argument = Utf8(parsed[i]);
            if (parsed[i][0] && argument.empty()) {
                validUnicode = false;
                break;
            }
            details.arguments.push_back(std::move(argument));
        }
        LocalFree(parsed);
        if (!validUnicode)
            return std::unexpected(Error{ErrorCode::InvalidPath, "The shortcut contains invalid Unicode arguments."});
    }
    result = link->GetWorkingDirectory(directory.data(), static_cast<int>(directory.size()));
    if (FAILED(result))
        return std::unexpected(ShortcutError(result, "Windows could not read the shortcut working directory."));
    details.workingDirectory = ExpandPath(directory.data());
    if (!details.workingDirectory.empty() && !details.workingDirectory.is_absolute())
        details.workingDirectory = (details.executable.parent_path() / details.workingDirectory).lexically_normal();
    if (SUCCEEDED(link->GetIconLocation(icon.data(), static_cast<int>(icon.size()), &details.iconIndex))) {
        details.iconPath = ExpandPath(icon.data());
        if (!details.iconPath.empty() && !details.iconPath.is_absolute())
            details.iconPath = (path.parent_path() / details.iconPath).lexically_normal();
        if (!details.iconPath.empty() && !SafeAbsolutePath(details.iconPath))
            details.iconPath.clear();
    }
    return details;
}

static Result<CustomTool> ParseCustomShortcutInput(std::string_view input, std::string id) {
    if (!IsValidToolId(id))
        return std::unexpected(Error{ErrorCode::InvalidConfig, "The shortcut identifier is invalid."});
    input = Trim(input);
    if (input.empty() || input.size() >= 32000 || input.find('\0') != input.npos)
        return std::unexpected(Error{ErrorCode::InvalidPath, "Enter a website or an application path."});
    if (input.size() > 1 && input.front() == '"' && input.back() == '"')
        input = input.substr(1, input.size() - 2);
    const auto wide = Utf16(input);
    if (wide.empty())
        return std::unexpected(Error{ErrorCode::InvalidPath, "The shortcut text is not valid UTF-8."});
    const std::filesystem::path path(wide);
    if (path.is_absolute()) {
        if (!SafeAbsolutePath(path))
            return std::unexpected(Error{ErrorCode::InvalidPath, "The application or shortcut path is invalid."});
        const auto normalized = path.lexically_normal();
        if (_wcsicmp(path.extension().c_str(), L".exe") == 0) {
            if (const auto valid = CheckFile(normalized, L".exe"); !valid)
                return std::unexpected(valid.error());
            return CustomTool{std::move(id), DisplayName(normalized), ShortcutKind::Executable,
                              Utf8(normalized.native())};
        }
        if (_wcsicmp(path.extension().c_str(), L".lnk") == 0) {
            if (const auto details = InspectWindowsShortcut(normalized); !details)
                return std::unexpected(details.error());
            return CustomTool{std::move(id), Utf8(normalized.stem().native()), ShortcutKind::WindowsShortcut,
                              Utf8(normalized.native())};
        }
        return std::unexpected(Error{ErrorCode::InvalidPath, "Choose an application (.exe) or shortcut (.lnk)."});
    }
    std::string url(input);
    if (url.size() >= 8 && _strnicmp(url.c_str(), "https://", 8) == 0)
        url.replace(0, 8, "https://");
    else if (url.find("://") == url.npos && url.find_first_of("\\ \t\r\n") == url.npos) {
        // A dotted hostname is recognizable without a scheme; arbitrary text is not.
        const auto host = std::string_view(url).substr(0, url.find_first_of("/:?#"));
        if (host.find('.') != host.npos && host.front() != '.' && host.back() != '.')
            url.insert(0, "https://");
    }
    if (!IsSafeWebUrl(url))
        return std::unexpected(Error{ErrorCode::InvalidURL, "Enter a valid HTTPS website address."});
    return CustomTool{std::move(id), HostName(url), ShortcutKind::Url, std::move(url)};
}
Result<CustomTool> ParseCustomShortcut(std::string_view input, std::string id) {
    auto result = ParseCustomShortcutInput(input, std::move(id));
    if (!result)
        result.error().code = ErrorCode::InvalidShortcut;
    else
        result->name = BoundedDisplayName(std::move(result->name), result->id);
    return result;
}
} // namespace poetoolbox
