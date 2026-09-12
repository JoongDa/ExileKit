#include "poetoolbox/localization.h"
#include "json_internal.h"
namespace poetoolbox {
Status LocalizationManager::Load(const std::filesystem::path &directory) {
    translations_.clear();
    std::optional<Error> error;
    for (const auto language : GetAvailableLanguages()) {
        auto text = detail::ReadJsonFile(directory / (std::string(language) + ".json"));
        if (!text) {
            if (language == "en-US")
                error = text.error();
            continue;
        }
        try {
            const auto j = detail::ParseJson(*text, ErrorCode::InvalidConfig);
            if (!j.is_object())
                throw Error{ErrorCode::InvalidConfig, "Locale must be a string dictionary."};
            Strings strings;
            for (auto it = j.begin(); it != j.end(); ++it) {
                if (!it.value().is_string())
                    throw Error{ErrorCode::InvalidConfig, "Locale value must be a string."};
                strings[it.key()] = it.value().get<std::string>();
            }
            translations_[std::string(language)] = std::move(strings);
        } catch (const Error &e) {
            error = e;
        } catch (const std::exception &e) {
            error = Error{ErrorCode::InvalidConfig, e.what()};
        }
    }
    if (error)
        return std::unexpected(*error);
    return {};
}
bool LocalizationManager::SetLanguage(std::string_view language) {
    if (language != "en-US" && language != "zh-CN")
        return false;
    language_ = language;
    return true;
}
std::string LocalizationManager::GetString(std::string_view key, std::string_view fallback) const {
    for (const auto language : {std::string_view(language_), std::string_view("en-US")}) {
        const auto dict = translations_.find(language);
        if (dict == translations_.end())
            continue;
        const auto text = dict->second.find(key);
        if (text != dict->second.end() && !text->second.empty())
            return text->second;
    }
    return std::string(fallback.empty() ? key : fallback);
}
} // namespace poetoolbox
