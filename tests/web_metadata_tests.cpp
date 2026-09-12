#include "web_metadata.h"
#include "icon_provider.h"
#include "utf.h"
#include <windows.h>
#include <objbase.h>
#include <array>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

using namespace poetoolbox;
namespace {
void Check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
HttpResponse Html(std::string_view text) {
    return {200, {}, "text/html; charset=utf-8", {text.begin(), text.end()}};
}
struct FakeHttp final : HttpClient {
    std::map<std::string, HttpResponse> replies;
    std::vector<HttpRequest> calls;
    Result<HttpResponse> Get(const HttpRequest &request) override {
        calls.push_back(request);
        if (auto it = replies.find(request.url); it != replies.end())
            return it->second;
        return std::unexpected(Error{ErrorCode::IoError, "Simulated offline/timeout.", 12002});
    }
};
} // namespace

int wmain(int argc, wchar_t **argv) {
    if (argc != 3 && argc != 5)
        return 2;
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 0;
    try {
        const std::filesystem::path source(argv[1]);
        const auto output = std::filesystem::absolute(argv[2]) / (L"run-" + std::to_wstring(GetCurrentProcessId()) +
                                                                  L"-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(output);
        std::ifstream file(source / L"resources/icons/generic-web.png", std::ios::binary);
        std::vector<uint8_t> png((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        Check(!png.empty() && IconProvider::DecodeBytes(png).has_value(), "Fixture decode failed");
        const HttpResponse image{200, {}, "image/png", png};
        FakeHttp client;
        client.replies["https://example.com/tools/"] =
            Html("<!-- <title>Ignore me</title> --><script>var x='<title>Bad</title>';</script>"
                 "<TITLE>  中文 &amp; Tools &#x1F600;  </TITLE><link rel='shortcut icon' href='../mark.png?v=12:30'>");
        client.replies["https://example.com/tools/../mark.png?v=12:30"] = image;
        WebMetadataProvider provider(output / L"good", &client);
        auto first = provider.Fetch("example", "https://example.com/tools/", true);
        Check(first && first->title == "中文 & Tools 😀" && first->icon && client.calls.size() == 2,
              "HTML title/entity/relative link metadata failed");
        const auto attempts = client.calls.size();
        auto cached = provider.Fetch("example", "https://example.com/tools/", false);
        Check(cached && cached->icon && client.calls.size() == attempts, "Valid cache caused a new request");
        CustomTool custom{"example", "Example", ShortcutKind::Url, "https://example.com/tools/"};
        Check(IconProvider(output / L"good").LoadCustom(custom).has_value(), "Custom URL cache load failed");
        auto titleAgain = provider.Fetch("example", "https://example.com/tools/", true);
        Check(titleAgain && titleAgain->icon && client.calls.size() == attempts + 1,
              "Explicit title refresh should request HTML but reuse cached icon");
        for (const auto &call : client.calls)
            Check(call.maxBytes <= 2 * 1024 * 1024 &&
                      call.deadline <= std::chrono::steady_clock::now() + std::chrono::seconds(6),
                  "HTTP boundary was not bounded");

        FakeHttp fallback;
        fallback.replies["https://fallback.example/favicon.ico"] = image;
        WebMetadataProvider fallbackProvider(output / L"fallback", &fallback);
        auto direct = fallbackProvider.Fetch("fallback", "https://fallback.example/path", false);
        Check(direct && direct->icon && fallback.calls.size() == 1, "Default favicon path should avoid HTML request");

        FakeHttp upper;
        upper.replies["https://upper.example/"] = Html("<link rel='icon' href='HTTPS://cdn.example/icon.png'>");
        upper.replies["https://cdn.example/icon.png"] = image;
        auto upperIcon = WebMetadataProvider(output / L"upper", &upper).Fetch("upper", "https://upper.example/", true);
        Check(upperIcon && upperIcon->icon && upper.calls.size() == 2,
              "Uppercase HTTPS icon scheme was not normalized");

        FakeHttp meta;
        meta.replies["https://meta.example/"] = Html("<meta content='A &amp; B' property='og:site_name'>");
        auto site = WebMetadataProvider(output / L"meta", &meta).Fetch("site", "https://meta.example/", true);
        Check(site && site->title == "A & B" && !site->icon, "Site metadata must survive failed icon download");
        std::string longName;
        for (unsigned i = 0; i < 220; ++i)
            longName += "测";
        meta.replies["https://meta.example/"] = Html("<title>" + longName + "</title>");
        auto bounded = WebMetadataProvider(output / L"long", &meta).Fetch("long", "https://meta.example/", true);
        Check(bounded && !bounded->title.empty() && bounded->title.size() <= 512 && !Utf16(bounded->title).empty(),
              "Metadata title truncation broke UTF-8 or config bounds");

        FakeHttp offline;
        WebMetadataProvider offlineProvider(output / L"offline", &offline);
        auto failed = offlineProvider.Fetch("offline", "https://offline.example/", false);
        Check(!failed && offline.calls.size() == 2, "Offline fallback should stop without retries");
        const auto failureCount = offline.calls.size();
        auto remembered = offlineProvider.Fetch("offline", "https://offline.example/", false);
        Check(remembered && !remembered->icon && offline.calls.size() == failureCount,
              "Failed icon was retried inside its persisted TTL");
        const auto failedMarker = output / L"offline/offline.failed";
        std::filesystem::last_write_time(failedMarker,
                                         std::filesystem::file_time_type::clock::now() - std::chrono::hours(25));
        (void)offlineProvider.Fetch("offline", "https://offline.example/", false);
        Check(offline.calls.size() == failureCount + 2, "Expired failure TTL never allowed a fresh attempt");

        FakeHttp redirects;
        redirects.replies["https://redirect.example/"] = {302, "http://unsafe.example/", {}, {}};
        redirects.replies["https://redirect.example/favicon.ico"] = {302, "file:///C:/tool.exe", {}, {}};
        auto rejected =
            WebMetadataProvider(output / L"redirect", &redirects).Fetch("redirect", "https://redirect.example/", true);
        Check(!rejected && redirects.calls.size() == 2, "Unsafe redirects reached HTTP boundary");
        FakeHttp relocated;
        relocated.replies["https://old.example/"] = {302, "https://new.example/home", {}, {}};
        relocated.replies["https://new.example/home"] = Html("<title>New home</title>");
        relocated.replies["https://new.example/favicon.ico"] = image;
        auto moved = WebMetadataProvider(output / L"moved", &relocated).Fetch("moved", "https://old.example/", false);
        Check(moved && moved->icon && relocated.calls.size() == 4 &&
                  relocated.calls.back().url == "https://new.example/favicon.ico",
              "Redirected website used the original origin for its fallback icon");
        FakeHttp loop;
        loop.replies["https://loop.example/"] = {302, "/1", {}, {}};
        loop.replies["https://loop.example/1"] = {302, "/2", {}, {}};
        loop.replies["https://loop.example/2"] = {302, "/3", {}, {}};
        loop.replies["https://loop.example/3"] = {302, "/4", {}, {}};
        auto limited = WebMetadataProvider(output / L"loop", &loop).Fetch("loop", "https://loop.example/", true);
        Check(!limited && loop.calls.size() == 5, "Redirect chain exceeded three hops");

        FakeHttp oversized;
        auto giantHtml = Html("<title>Must reject</title>");
        giantHtml.body.resize(256 * 1024 + 1, 'x');
        oversized.replies["https://large.example/"] = std::move(giantHtml);
        auto giantIcon = image;
        giantIcon.body.resize(2 * 1024 * 1024 + 1);
        oversized.replies["https://large.example/favicon.ico"] = std::move(giantIcon);
        auto tooLarge =
            WebMetadataProvider(output / L"large", &oversized).Fetch("large", "https://large.example/", true);
        Check(!tooLarge && oversized.calls.size() == 2, "Oversized injected response was trusted");
        Check(!IconProvider::DecodeBytes({}) && !IconProvider::DecodeBytes(std::array<uint8_t, 3>{1, 2, 3}),
              "Malformed/empty image accepted");

        std::stop_source stop;
        stop.request_stop();
        const auto beforeStop = client.calls.size();
        auto cancelled = provider.Fetch("cancelled", "https://example.com/", true, stop.get_token());
        Check(!cancelled && cancelled.error().code == ErrorCode::Cancelled && client.calls.size() == beforeStop,
              "Cancelled metadata request reached HTTP boundary");
        Check(!provider.Fetch("../escape", "https://example.com/", true) &&
                  !provider.Fetch("unsafe", "https://user:password@example.com/", true) &&
                  !provider.Fetch("unsafe", "http://example.com/", true),
              "Unsafe key or URL accepted");
        WinHttpClient actual;
        const auto deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        Check(!actual.Get({"https://example.com/", 1024, deadline, {}}), "Expired request was started");
        Check(!actual.Get({"file:///C:/bad", 1024, deadline, {}}), "Non-HTTPS request was started");
        std::cout << "Metadata/title, icon decode, no-request cache, failure TTL, offline/timeout, redirects, response "
                     "bounds, cancellation passed.\n";
        // Optional explicit live verification; never runs under CTest.
        if (argc == 5 && std::wstring_view(argv[3]) == L"--live") {
            auto live = WebMetadataProvider(output / L"live").Fetch("live-check", Utf8(argv[4]), true);
            if (!live)
                throw std::runtime_error(live.error().message +
                                         " NativeCode=" + std::to_string(live.error().nativeCode));
            std::cout << "Live title: " << live->title << ", icon: " << (live->icon ? "decoded" : "fallback") << '\n';
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    if (SUCCEEDED(com))
        CoUninitialize();
    return result;
}
