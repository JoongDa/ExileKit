#include "poetoolbox/localization.h"
#include "poetoolbox/manifest.h"
#include "poetoolbox/registry.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
using namespace poetoolbox;
using Json = nlohmann::json;
namespace {
void Check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void Write(const std::filesystem::path &p, const std::string &s) {
    std::ofstream f(p, std::ios::binary);
    f << s;
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    if (argc != 3)
        return 1;
    try {
        const std::filesystem::path source(argv[1]), temp(argv[2]);
        std::filesystem::create_directories(temp);
        Json valid = {{"schemaVersion", 1},
                      {"id", "test-tool"},
                      {"name", "Test Tool"},
                      {"description", {{"en-US", "Price and market"}, {"zh-CN", "价格与市场"}}},
                      {"category", "market"},
                      {"tags", {"trade"}},
                      {"aliases", {{"zh-CN", {"查价"}}}},
                      {"games", {"poe1"}},
                      {"type", "web"},
                      {"distribution", "none"},
                      {"riskLevel", "normal"},
                      {"official", false},
                      {"launch", {{"url", "https://example.com/"}}}};
        auto parsed = ParseManifest(valid.dump());
        Check(parsed.has_value(), "Valid manifest rejected");
        auto broken = valid;
        broken.erase("name");
        Check(!ParseManifest(broken.dump()), "Missing required field accepted");
        broken = valid;
        broken["schemaVersion"] = 2;
        auto result = ParseManifest(broken.dump());
        Check(!result && result.error().code == ErrorCode::UnsupportedSchema, "Unsupported schema not identified");
        broken = valid;
        broken["futureField"] = {{"nested", true}};
        Check(ParseManifest(broken.dump()).has_value(), "Unknown field compatibility failed");
        for (const auto *field : {"official", "games", "tags", "description", "launch"}) {
            broken = valid;
            broken[field] = 12;
            Check(!ParseManifest(broken.dump()), "Wrong known-field type accepted");
        }
        broken = valid;
        broken["icon"] = "../outside.png";
        Check(!ParseManifest(broken.dump()), "Icon traversal accepted");
        broken = valid;
        broken["games"] = Json::array();
        Check(!ParseManifest(broken.dump()), "Empty verified games accepted");
        broken["gameSupportVerified"] = false;
        Check(ParseManifest(broken.dump()).has_value(), "Explicit unverified games rejected");
        for (const auto *id : {"../bad", "CON", "con", "com1", "bad/id", "-bad", "bad-"}) {
            broken = valid;
            broken["id"] = id;
            Check(!ParseManifest(broken.dump()), "Unsafe stable ID accepted");
        }
        for (const auto *url : {"javascript:alert(1)", "file:///C:/Windows/notepad.exe", "https://good.com@evil.com",
                                "https://good.com\\bad", "https://good.com:99999", "https://"})
            Check(!IsSafeWebUrl(url), "Unsafe URL accepted");
        Check(IsSafeWebUrl("https://example.com/path?q=two%20words"), "Normal HTTPS URL rejected");
        Write(temp / L"good.json", valid.dump());
        Write(temp / L"bad.json", "{ broken");
        Write(temp / L"损坏 清单.json", "{ broken");
        Write(temp / L"duplicate.json", valid.dump());
        ToolRegistry sample;
        Check(sample.Load(temp).has_value(), "Bad file crashed registry");
        Check(sample.GetTools().size() == 1 && sample.Diagnostics().size() == 3,
              "Bad/duplicate manifest isolation failed");
        Check(sample.Search("查价", GameFilter::All, {}, "zh-CN").size() == 1, "Chinese alias not searched");
        Check(sample.Search("查价", GameFilter::All, {}, "en-US").empty(), "Language-specific alias leaked");
        Check(sample.Search("PRICE trade", GameFilter::POE1).size() == 1, "Case fold / AND search failed");
        Check(sample.Search("", GameFilter::POE2).empty(), "Game filter failed");
        ToolRegistry registry;
        Check(registry.Load(source / L"tools" / L"manifests").has_value(), "Catalog load failed");
        Check(registry.Diagnostics().empty(), "Catalog has invalid files");
        Check(registry.GetTools().size() == 15, "Expected 15 catalog tools");
        for (const auto *query : {"ninja", "trade", "craft", "build", "价格", "市场", "交易", "BD"})
            Check(!registry.Search(query, GameFilter::All, {}, "zh-CN").empty(), "Expected search has no results");
        Check(registry.FindTool("poeredux")->manifest.riskLevel == RiskLevel::GameModifying, "Risk metadata lost");
        std::filesystem::create_directories(temp / L"locales");
        Write(temp / L"locales" / L"en-US.json", R"({"shared":"English","fallback":"Fallback"})");
        Write(temp / L"locales" / L"zh-CN.json", R"({"shared":"中文"})");
        LocalizationManager locale;
        Check(locale.Load(temp / L"locales").has_value(), "Locale load failed");
        Check(locale.SetLanguage("zh-CN"), "Cannot select Chinese");
        Check(locale.GetString("shared") == "中文", "Selected locale ignored");
        Check(locale.GetString("fallback") == "Fallback", "English fallback failed");
        Check(locale.GetString("unknown") == "unknown" && locale.GetString("unknown", "default") == "default",
              "Key/default fallback failed");
        Check(!locale.SetLanguage("xx"), "Unsupported language accepted");
        LocalizationManager real;
        Check(real.Load(source / L"resources" / L"locales").has_value(), "Real locales failed");
        std::cout
            << "Manifest, compatibility, isolation, registry, search, aliases, games and locale fallback passed.\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
