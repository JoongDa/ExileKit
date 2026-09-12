#include "poetoolbox/config.h"
#include "json_internal.h"
#include "utf.h"
#include <algorithm>
#include <cctype>
#include <windows.h>
namespace poetoolbox {
namespace {
using Json = nlohmann::json;
void Require(bool condition, const char *message) {
    if (!condition)
        throw Error{ErrorCode::InvalidConfig, message};
}
std::filesystem::path Path(const Json &value) {
    Require(value.is_string(), "Expected path string.");
    auto s = value.get<std::string>();
    Require(s.size() < 32000 && s.find('\0') == s.npos, "Invalid path text.");
    auto wide = Utf16(s);
    Require(s.empty() || !wide.empty(), "Invalid UTF8 path.");
    std::filesystem::path p(wide);
    Require(p.empty() || p.is_absolute(), "Path must be absolute.");
    return p;
}
void Text(std::string_view value, std::size_t maximum, const char *message) {
    Require(!value.empty() && value.size() <= maximum && value.find('\0') == value.npos && !Utf16(value).empty(),
            message);
    Require(std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; }), message);
}
const char *KindKey(ShortcutKind kind) {
    switch (kind) {
    case ShortcutKind::Url:
        return "url";
    case ShortcutKind::Executable:
        return "executable";
    case ShortcutKind::WindowsShortcut:
        return "windows-shortcut";
    }
    throw Error{ErrorCode::InvalidConfig, "Invalid custom shortcut kind."};
}
ShortcutKind ParseKind(const Json &value) {
    Require(value.is_string(), "Invalid custom shortcut kind.");
    const auto kind = value.get<std::string>();
    if (kind == "url")
        return ShortcutKind::Url;
    if (kind == "executable")
        return ShortcutKind::Executable;
    Require(kind == "windows-shortcut", "Unknown custom shortcut kind.");
    return ShortcutKind::WindowsShortcut;
}
void ValidateCustom(const std::string &id, const CustomTool &tool) {
    Require(IsValidToolId(id) && tool.id == id, "Invalid custom shortcut id.");
    Text(tool.name, 512, "Invalid custom shortcut name.");
    Text(tool.target, 32000, "Invalid custom shortcut target.");
    (void)KindKey(tool.kind);
    if (tool.kind == ShortcutKind::Url) {
        Require(IsSafeWebUrl(tool.target), "Invalid custom shortcut URL.");
    } else {
        const auto path = Path(Json(tool.target));
        auto ext = path.extension().native();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](wchar_t c) { return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + (L'a' - L'A')) : c; });
        Require(ext == (tool.kind == ShortcutKind::Executable ? L".exe" : L".lnk"),
                "Invalid custom shortcut extension.");
    }
}
Json Encode(const UserConfig &c) {
    Require(c.language.empty() || c.language == "en-US" || c.language == "zh-CN", "Unsupported language.");
    Require(c.managedToolsDirectory.empty() || c.managedToolsDirectory.is_absolute(),
            "Managed directory must be absolute.");
    Require(c.homeAutoRemoveDays == 0 || c.homeAutoRemoveDays == 7 || c.homeAutoRemoveDays == 30 ||
                c.homeAutoRemoveDays == 90,
            "Unsupported Home removal policy.");
    Json j = {{"schemaVersion", 1},
              {"language", c.language},
              {"selectedGame", GameFilterKey(c.selectedGame)},
              {"managedToolsDirectory", Utf8(c.managedToolsDirectory.native())},
              {"executablePaths", Json::object()},
              {"favorites", c.favorites},
              {"recentTools", Json::object()},
              {"home", Json::object()},
              {"homeAutoRemoveDays", c.homeAutoRemoveDays},
              {"customTools", Json::object()}};
    for (const auto &[id, path] : c.executablePaths) {
        Require(IsValidToolId(id) && path.is_absolute(), "Invalid executable mapping.");
        j["executablePaths"][id] = Utf8(path.native());
    }
    for (const auto &id : c.favorites)
        Require(IsValidToolId(id), "Invalid favorite id.");
    for (const auto &[id, recent] : c.recentTools) {
        Require(IsValidToolId(id) && recent.lastLaunchTime >= 0, "Invalid recent tool.");
        j["recentTools"][id] = {{"lastLaunchTime", recent.lastLaunchTime}, {"launchCount", recent.launchCount}};
    }
    for (const auto &[id, entry] : c.home) {
        Require(IsValidToolId(id) && entry.addedAt >= 0, "Invalid Home entry.");
        Require(!entry.pinned || (entry.added && !entry.hiddenFromHome), "Inconsistent pinned Home entry.");
        Require(!entry.hiddenFromHome || !entry.added, "Inconsistent hidden Home entry.");
        j["home"][id] = {{"added", entry.added},
                         {"pinned", entry.pinned},
                         {"hiddenFromHome", entry.hiddenFromHome},
                         {"addedAt", entry.addedAt}};
    }
    for (const auto &[id, tool] : c.customTools) {
        ValidateCustom(id, tool);
        j["customTools"][id] = {{"name", tool.name}, {"kind", KindKey(tool.kind)}, {"target", tool.target}};
    }
    return j;
}
std::string EncodeBounded(const UserConfig &config) {
    auto bytes = Encode(config).dump(2);
    Require(bytes.size() <= detail::MaxJsonBytes, "Config exceeds size limit.");
    return bytes;
}
} // namespace
Result<UserConfig> ConfigManager::Load() const {
    std::error_code ec;
    if (!std::filesystem::exists(file_, ec) && !ec)
        return UserConfig{};
    const auto text = detail::ReadJsonFile(file_);
    if (!text)
        return std::unexpected(text.error());
    try {
        const auto j = detail::ParseJson(*text, ErrorCode::InvalidConfig);
        Require(j.is_object(), "Expected config object.");
        Require(j.contains("schemaVersion") && j.at("schemaVersion").is_number_integer() && j.at("schemaVersion") == 1,
                "Unsupported config schema.");
        UserConfig c;
        if (j.contains("language")) {
            Require(j.at("language").is_string(), "Invalid language type.");
            c.language = j.at("language").get<std::string>();
        }
        if (j.contains("selectedGame")) {
            Require(j.at("selectedGame").is_string(), "Invalid game filter.");
            auto game = j.at("selectedGame").get<std::string>();
            Require(game == "all" || game == "poe1" || game == "poe2", "Unknown game filter.");
            c.selectedGame = game == "poe1" ? GameFilter::POE1 : game == "poe2" ? GameFilter::POE2 : GameFilter::All;
        }
        if (j.contains("managedToolsDirectory"))
            c.managedToolsDirectory = Path(j.at("managedToolsDirectory"));
        if (j.contains("executablePaths")) {
            Require(j.at("executablePaths").is_object(), "Invalid executable map.");
            for (auto it = j.at("executablePaths").begin(); it != j.at("executablePaths").end(); ++it)
                c.executablePaths[it.key()] = Path(it.value());
        }
        if (j.contains("favorites")) {
            Require(j.at("favorites").is_array(), "Invalid favorites.");
            for (const auto &id : j.at("favorites")) {
                Require(id.is_string(), "Invalid favorite.");
                c.favorites.insert(id.get<std::string>());
            }
        }
        if (j.contains("recentTools")) {
            Require(j.at("recentTools").is_object(), "Invalid recent map.");
            for (auto it = j.at("recentTools").begin(); it != j.at("recentTools").end(); ++it) {
                const auto &r = it.value();
                Require(r.is_object() && r.at("lastLaunchTime").is_number_integer() &&
                            r.at("launchCount").is_number_unsigned(),
                        "Invalid recent entry.");
                c.recentTools[it.key()] = {r.at("lastLaunchTime").get<int64_t>(), r.at("launchCount").get<uint64_t>()};
            }
        }
        if (j.contains("homeAutoRemoveDays")) {
            const auto &days = j.at("homeAutoRemoveDays");
            Require(days.is_number_integer() && (days == 0 || days == 7 || days == 30 || days == 90),
                    "Unsupported Home removal policy.");
            c.homeAutoRemoveDays = days.get<int>();
        }
        if (j.contains("home")) {
            Require(j.at("home").is_object(), "Invalid Home map.");
            for (auto it = j.at("home").begin(); it != j.at("home").end(); ++it) {
                const auto &entry = it.value();
                Require(entry.is_object() && entry.at("added").is_boolean() && entry.at("pinned").is_boolean() &&
                            entry.at("hiddenFromHome").is_boolean() && entry.at("addedAt").is_number_integer(),
                        "Invalid Home entry.");
                c.home[it.key()] = {entry.at("added").get<bool>(), entry.at("pinned").get<bool>(),
                                    entry.at("hiddenFromHome").get<bool>(), entry.at("addedAt").get<std::int64_t>()};
            }
        }
        if (j.contains("customTools")) {
            Require(j.at("customTools").is_object(), "Invalid custom shortcut map.");
            for (auto it = j.at("customTools").begin(); it != j.at("customTools").end(); ++it) {
                const auto &tool = it.value();
                Require(tool.is_object() && tool.at("name").is_string() && tool.at("target").is_string(),
                        "Invalid custom shortcut entry.");
                c.customTools[it.key()] = {it.key(), tool.at("name").get<std::string>(), ParseKind(tool.at("kind")),
                                           tool.at("target").get<std::string>()};
            }
        }
        (void)Encode(c);
        return c;
    } catch (const Error &e) {
        return std::unexpected(e);
    } catch (const std::exception &e) {
        return std::unexpected(Error{ErrorCode::InvalidConfig, e.what()});
    }
}
Status ConfigManager::Validate(const UserConfig &config) {
    try {
        (void)EncodeBounded(config);
        return {};
    } catch (const Error &e) {
        return std::unexpected(e);
    } catch (const std::exception &e) {
        return std::unexpected(Error{ErrorCode::IoError, e.what()});
    }
}
Status ConfigManager::Save(const UserConfig &config) const {
    try {
        const auto bytes = EncodeBounded(config);
        std::error_code ec;
        std::filesystem::create_directories(file_.parent_path(), ec);
        if (ec)
            return std::unexpected(
                Error{ErrorCode::IoError, "Cannot create config folder.", static_cast<uint32_t>(ec.value())});
        const auto temp =
            file_.parent_path() / (file_.filename().native() + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp");
        HANDLE handle =
            CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return std::unexpected(Error{ErrorCode::IoError, "Cannot write config.", GetLastError()});
        DWORD written = 0;
        const bool ok = WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                        written == bytes.size() && FlushFileBuffers(handle);
        DWORD error = ok ? 0 : GetLastError();
        CloseHandle(handle);
        if (ok && MoveFileExW(temp.c_str(), file_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return {};
        if (ok)
            error = GetLastError();
        DeleteFileW(temp.c_str());
        return std::unexpected(
            Error{ErrorCode::IoError, "Cannot commit configuration; previous config retained.", error});
    } catch (const Error &e) {
        return std::unexpected(e);
    } catch (const std::exception &e) {
        return std::unexpected(Error{ErrorCode::IoError, e.what()});
    }
}
} // namespace poetoolbox
