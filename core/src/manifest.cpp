#include "poetoolbox/manifest.h"
#include "json_internal.h"
#include <algorithm>

namespace poetoolbox {
namespace {
using Json = nlohmann::json;
std::string String(const Json &j, const char *key, bool required = false, size_t limit = 8192) {
    if (!j.contains(key)) {
        if (required)
            throw Error{ErrorCode::InvalidManifest, std::string("Missing field: ") + key};
        return {};
    }
    if (!j.at(key).is_string())
        throw Error{ErrorCode::InvalidManifest, std::string("Expected string: ") + key};
    auto result = j.at(key).get<std::string>();
    if ((required && result.empty()) || result.size() > limit || result.find('\0') != result.npos)
        throw Error{ErrorCode::InvalidManifest, std::string("Invalid text: ") + key};
    return result;
}
template <class E> E Enum(std::string_view value, std::initializer_list<std::string_view> names) {
    size_t i = 0;
    for (auto name : names) {
        if (value == name)
            return static_cast<E>(i);
        ++i;
    }
    throw Error{ErrorCode::InvalidManifest, "Unknown enum value: " + std::string(value)};
}
std::vector<std::string> Strings(const Json &value) {
    if (!value.is_array() || value.size() > 128)
        throw Error{ErrorCode::InvalidManifest, "Expected bounded string array."};
    std::vector<std::string> result;
    for (const auto &entry : value) {
        if (!entry.is_string())
            throw Error{ErrorCode::InvalidManifest, "Expected string array item."};
        const auto text = entry.get<std::string>();
        if (text.size() > 8192 || text.find('\0') != text.npos)
            throw Error{ErrorCode::InvalidManifest, "Array string too long or contains NUL."};
        result.push_back(text);
    }
    return result;
}
bool Boolean(const Json &j, const char *key, bool fallback) {
    if (!j.contains(key))
        return fallback;
    if (!j.at(key).is_boolean())
        throw Error{ErrorCode::InvalidManifest, std::string("Expected boolean: ") + key};
    return j.at(key).get<bool>();
}
void Object(const Json &j) {
    if (!j.is_object() || j.size() > 256)
        throw Error{ErrorCode::InvalidManifest, "Expected bounded object."};
}
} // namespace
Result<ToolManifest> ParseManifest(std::string_view text) {
    try {
        const auto j = detail::ParseJson(text, ErrorCode::InvalidManifest);
        Object(j);
        if (!j.contains("schemaVersion") || !j.at("schemaVersion").is_number_integer())
            throw Error{ErrorCode::InvalidManifest, "schemaVersion must be an integer."};
        if (j.at("schemaVersion") != 1)
            throw Error{ErrorCode::UnsupportedSchema, "Unsupported manifest schemaVersion."};
        ToolManifest m;
        m.id = String(j, "id", true, 64);
        if (!IsValidToolId(m.id))
            throw Error{ErrorCode::InvalidManifest, "Invalid stable tool id."};
        m.name = String(j, "name", true, 160);
        Object(j.at("description"));
        for (auto it = j.at("description").begin(); it != j.at("description").end(); ++it)
            m.description[it.key()] = String(j.at("description"), it.key().c_str(), false, 2048);
        if (!m.description.contains("en-US") || m.description.at("en-US").empty())
            throw Error{ErrorCode::InvalidManifest, "description.en-US required."};
        m.category = Enum<ToolCategory>(String(j, "category", true),
                                        {"official", "market", "craft", "database", "regex", "build", "hideout",
                                         "calculator", "price-check", "trade", "filter", "wiki", "utility"});
        m.type = Enum<ToolType>(String(j, "type", true), {"web", "application", "builtin", "external-link"});
        m.distribution =
            Enum<DistributionType>(String(j, "distribution", true), {"none", "external", "managed", "bundled"});
        m.riskLevel = Enum<RiskLevel>(String(j, "riskLevel", true), {"normal", "elevated", "game_modifying"});
        if (!j.contains("official"))
            throw Error{ErrorCode::InvalidManifest, "Missing field: official"};
        m.official = Boolean(j, "official", false);
        m.gameSupportVerified = Boolean(j, "gameSupportVerified", true);
        for (const auto &game : Strings(j.at("games"))) {
            const auto g = Enum<GameType>(game, {"poe1", "poe2"});
            if (std::find(m.games.begin(), m.games.end(), g) != m.games.end())
                throw Error{ErrorCode::InvalidManifest, "Duplicate game."};
            m.games.push_back(g);
        }
        if (m.games.empty() && m.gameSupportVerified)
            throw Error{ErrorCode::InvalidManifest, "Verified games must not be empty."};
        m.tags = Strings(j.at("tags"));
        if (j.contains("aliases")) {
            Object(j.at("aliases"));
            for (auto it = j.at("aliases").begin(); it != j.at("aliases").end(); ++it)
                m.aliases[it.key()] = Strings(it.value());
        }
        m.homepage = String(j, "homepage");
        m.repository = String(j, "repository");
        m.icon = String(j, "icon");
        m.author = String(j, "author");
        m.license = String(j, "license");
        m.licenseUrl = String(j, "licenseUrl");
        m.downloadPage = String(j, "downloadPage");
        for (const auto &url : {m.homepage, m.repository, m.licenseUrl, m.downloadPage})
            if (!url.empty() && !IsSafeWebUrl(url))
                throw Error{ErrorCode::InvalidManifest, "Invalid metadata URL."};
        if (!m.icon.empty()) {
            const auto path = std::filesystem::path(std::u8string(m.icon.begin(), m.icon.end()));
            if (path.is_absolute() || path.has_root_name() || m.icon.find(':') != m.icon.npos)
                throw Error{ErrorCode::InvalidManifest, "Icon must be bundled relative path."};
            for (const auto &part : path)
                if (part == "..")
                    throw Error{ErrorCode::InvalidManifest, "Icon path traversal."};
        }
        const auto &launch = j.at("launch");
        Object(launch);
        m.launch.url = String(launch, "url");
        m.launch.workingDirectory = String(launch, "workingDirectory");
        if (launch.contains("executableNames")) {
            m.launch.executableNames = Strings(launch.at("executableNames"));
            for (const auto &name : m.launch.executableNames)
                if (name.empty() || name.find_first_of("/\\:") != name.npos)
                    throw Error{ErrorCode::InvalidManifest, "Expected executable basename."};
        }
        if (launch.contains("arguments"))
            m.launch.arguments = Strings(launch.at("arguments"));
        if (launch.contains("environment")) {
            Object(launch.at("environment"));
            for (auto it = launch.at("environment").begin(); it != launch.at("environment").end(); ++it) {
                if (it.key().empty() || it.key().find_first_of("=\0", 0, 2) != it.key().npos)
                    throw Error{ErrorCode::InvalidManifest, "Invalid environment name."};
                m.launch.environment[it.key()] = String(launch.at("environment"), it.key().c_str());
            }
        }
        if ((m.type == ToolType::Web || m.type == ToolType::ExternalLink) && !IsSafeWebUrl(m.launch.url))
            throw Error{ErrorCode::InvalidURL, "Invalid launch URL."};
        if (m.type == ToolType::Application && !launch.contains("executableNames"))
            throw Error{ErrorCode::InvalidManifest,
                        "Application requires executableNames (may be empty if unverified)."};
        if (j.contains("permissions")) {
            Object(j.at("permissions"));
            if (j.at("permissions").contains("network")) {
                const auto &n = j.at("permissions").at("network");
                Object(n);
                m.outbound = Boolean(n, "outbound", false);
                m.inbound = Boolean(n, "inbound", false);
            }
        }
        for (const auto key : {"install", "update"})
            if (j.contains(key))
                Object(j.at(key));
        if (j.contains("install")) {
            m.provider = String(j.at("install"), "provider");
            m.expectedSha256 = String(j.at("install"), "sha256");
        }
        if (!m.expectedSha256.empty() &&
            (m.expectedSha256.size() != 64 ||
             m.expectedSha256.find_first_not_of("0123456789abcdefABCDEF") != m.expectedSha256.npos))
            throw Error{ErrorCode::InvalidManifest, "Invalid SHA-256."};
        return m;
    } catch (const Error &error) {
        return std::unexpected(error);
    } catch (const std::exception &error) {
        return std::unexpected(Error{ErrorCode::InvalidManifest, error.what()});
    }
}
} // namespace poetoolbox
