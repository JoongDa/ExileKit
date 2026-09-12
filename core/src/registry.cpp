#include "poetoolbox/registry.h"
#include "json_internal.h"
#include "poetoolbox/manifest.h"
#include <algorithm>
#include <sstream>
namespace poetoolbox {
namespace {
std::string Fold(std::string_view s) {
    std::string r(s);
    for (auto &c : r)
        if (c >= 'A' && c <= 'Z')
            c += ('a' - 'A');
    return r;
}
} // namespace
Status ToolRegistry::Load(const std::filesystem::path &directory) {
    tools_.clear();
    diagnostics_.clear();
    byId_.clear();
    byCategory_.clear();
    std::error_code ec;
    std::filesystem::directory_iterator it(directory, ec);
    if (ec)
        return std::unexpected(
            Error{ErrorCode::IoError, "Manifest directory unavailable.", static_cast<uint32_t>(ec.value())});
    std::vector<std::filesystem::path> files;
    for (; it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (ec)
            break;
        if (it->path().extension() == L".json")
            files.push_back(it->path());
        if (files.size() > 512)
            return std::unexpected(Error{ErrorCode::IoError, "Manifest limit exceeded (512)."});
    }
    if (ec)
        return std::unexpected(
            Error{ErrorCode::IoError, "Manifest directory read failed.", static_cast<uint32_t>(ec.value())});
    std::sort(files.begin(), files.end());
    for (const auto &file : files) {
        const auto text = detail::ReadJsonFile(file);
        auto parsed = text ? ParseManifest(*text) : Result<ToolManifest>(std::unexpected(text.error()));
        if (!parsed) {
            auto error = parsed.error();
            const auto filename = file.filename().u8string();
            error.message = std::string(filename.begin(), filename.end()) + ": " + error.message;
            diagnostics_.push_back(std::move(error));
            continue;
        }
        if (byId_.contains(parsed->id)) {
            diagnostics_.push_back({ErrorCode::DuplicateId, parsed->id});
            continue;
        }
        Tool tool{std::move(*parsed), {}};
        tool.searchText = tool.manifest.name + " " + std::string(CategoryKey(tool.manifest.category));
        for (const auto &[lang, description] : tool.manifest.description)
            tool.searchText += " " + description;
        for (const auto &tag : tool.manifest.tags)
            tool.searchText += " " + tag;
        tool.searchText = Fold(tool.searchText);
        byId_[tool.manifest.id] = tools_.size();
        byCategory_[tool.manifest.category].push_back(tools_.size());
        tools_.push_back(std::move(tool));
    }
    return {};
}
const Tool *ToolRegistry::FindTool(std::string_view id) const {
    const auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : &tools_[it->second];
}
std::vector<size_t> ToolRegistry::Search(std::string_view query, GameFilter game, std::optional<ToolCategory> category,
                                         std::string_view language) const {
    std::vector<std::string> words;
    std::istringstream input(Fold(query));
    std::string word;
    while (input >> word)
        words.push_back(word);
    std::vector<size_t> result;
    auto consider = [&](size_t i) {
        const auto &tool = tools_[i];
        if (game != GameFilter::All &&
            std::find(tool.manifest.games.begin(), tool.manifest.games.end(),
                      game == GameFilter::POE1 ? GameType::POE1 : GameType::POE2) == tool.manifest.games.end())
            return;
        std::string search = tool.searchText;
        for (const auto lang : {language, std::string_view("en-US")})
            if (const auto a = tool.manifest.aliases.find(lang); a != tool.manifest.aliases.end())
                for (const auto &alias : a->second)
                    search += " " + Fold(alias);
        if (std::all_of(words.begin(), words.end(), [&](const auto &term) { return search.find(term) != search.npos; }))
            result.push_back(i);
    };
    if (category) {
        if (const auto found = byCategory_.find(*category); found != byCategory_.end())
            for (auto i : found->second)
                consider(i);
    } else
        for (size_t i = 0; i < tools_.size(); ++i)
            consider(i);
    return result;
}
} // namespace poetoolbox
