#include "web_metadata.h"
#include "icon_provider.h"
#include "poetoolbox/tool.h"
#include "utf.h"
#include "json_internal.h"
#include <sstream>
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <fstream>
#include <map>

namespace poetoolbox {
namespace {
constexpr size_t HtmlLimit = 256 * 1024;
constexpr size_t IconLimit = 2 * 1024 * 1024;
using Clock = std::chrono::steady_clock;
Error NetworkError(std::string message, DWORD native = 0) {
    return {ErrorCode::IoError, std::move(message), native};
}
Status CheckTime(const HttpRequest &request) {
    if (request.stop.stop_requested())
        return std::unexpected(Error{ErrorCode::Cancelled, "Website metadata request cancelled."});
    if (Clock::now() >= request.deadline)
        return std::unexpected(NetworkError("Website metadata deadline expired.", ERROR_WINHTTP_TIMEOUT));
    return {};
}
struct InternetHandle {
    HINTERNET value = nullptr;
    ~InternetHandle() {
        if (value)
            WinHttpCloseHandle(value);
    }
    InternetHandle(const InternetHandle &) = delete;
    InternetHandle &operator=(const InternetHandle &) = delete;
    explicit InternetHandle(HINTERNET handle) : value(handle) {}
};
// A cancelled async read may complete after Get returns. Both its read buffer and callback
// context live until HANDLE_CLOSING, the last notification, and are never owned by the caller's stack.
struct AsyncState {
    std::atomic<unsigned> references{1};
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::atomic<DWORD> error{0};
    std::atomic<DWORD> length{0};
    std::array<uint8_t, 8192> buffer{};
    ~AsyncState() {
        if (event)
            CloseHandle(event);
    }
    void Release() {
        if (references.fetch_sub(1) == 1)
            delete this;
    }
};
void CALLBACK HttpCallback(HINTERNET, DWORD_PTR context, DWORD status, void *information, DWORD length) {
    auto *state = reinterpret_cast<AsyncState *>(context);
    if (!state)
        return;
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
        state->Release();
        return;
    }
    if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
        const auto *result = static_cast<WINHTTP_ASYNC_RESULT *>(information);
        state->error.store(result ? result->dwError : ERROR_WINHTTP_INTERNAL_ERROR);
    } else if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
        state->length.store(length);
    } else if (status != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE &&
               status != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE) {
        return;
    }
    SetEvent(state->event);
}
struct AsyncRequest {
    HINTERNET handle = nullptr;
    AsyncState *state = new AsyncState;
    ~AsyncRequest() {
        if (handle)
            WinHttpCloseHandle(handle);
        state->Release();
    }
};
Status Wait(AsyncState &state, const HttpRequest &request) {
    for (;;) {
        auto time = CheckTime(request);
        if (!time)
            return time;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(request.deadline - Clock::now());
        const auto waitMs = static_cast<DWORD>(std::clamp<int64_t>(remaining.count(), 1, 50));
        const auto wait = WaitForSingleObject(state.event, waitMs);
        if (wait == WAIT_OBJECT_0) {
            if (const auto error = state.error.load(); error)
                return std::unexpected(NetworkError("Website request failed.", error));
            return CheckTime(request);
        }
        if (wait == WAIT_FAILED)
            return std::unexpected(NetworkError("Cannot wait for website request.", GetLastError()));
    }
}
std::string Header(HINTERNET request, DWORD info) {
    DWORD bytes = 0;
    WinHttpQueryHeaders(request, info, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &bytes, WINHTTP_NO_HEADER_INDEX);
    if (!bytes || bytes > 16 * 1024 || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return {};
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, info, WINHTTP_HEADER_NAME_BY_INDEX, value.data(), &bytes,
                             WINHTTP_NO_HEADER_INDEX))
        return {};
    value.resize(bytes / sizeof(wchar_t));
    while (!value.empty() && !value.back())
        value.pop_back();
    return Utf8(value);
}
std::string Lower(std::string_view text) {
    std::string out(text);
    for (auto &c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
    return out;
}
bool Space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}
std::string_view Trim(std::string_view text) {
    while (!text.empty() && Space(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && Space(text.back()))
        text.remove_suffix(1);
    return text;
}
std::string Origin(std::string_view url) {
    return std::string(url.substr(0, url.find_first_of("/?#", 8)));
}
std::string Resolve(std::string_view base, std::string_view reference) {
    reference = Trim(reference);
    if (reference.empty() || reference.size() > 8192)
        return {};
    const auto colon = reference.find(':');
    const auto delimiter = reference.find_first_of("/?#");
    std::string result;
    if (reference.starts_with("//"))
        result = "https:" + std::string(reference);
    else if (reference.starts_with('/'))
        result = Origin(base) + std::string(reference);
    else if (colon != std::string_view::npos && (delimiter == std::string_view::npos || colon < delimiter))
        result = std::string(reference);
    else {
        auto clean = base.substr(0, base.find_first_of("?#"));
        if (reference.starts_with('?') || reference.starts_with('#'))
            result = std::string(clean) + std::string(reference);
        else {
            const auto slash = clean.find_last_of('/');
            result =
                (slash < 8 ? Origin(clean) + "/" : std::string(clean.substr(0, slash + 1))) + std::string(reference);
        }
    }
    if (const auto fragment = result.find('#'); fragment != std::string::npos)
        result.resize(fragment);
    if (result.size() >= 8 && Lower(std::string_view(result).substr(0, 8)) == "https://")
        result.replace(0, 8, "https://");
    return IsSafeWebUrl(result) ? result : std::string{};
}
struct Resource {
    std::string url;
    HttpResponse response;
};
Result<Resource> Download(HttpClient &client, std::string url, size_t limit, Clock::time_point deadline,
                          std::stop_token stop) {
    for (int redirects = 0; redirects <= 3; ++redirects) {
        HttpRequest request{url, limit, deadline, stop};
        if (auto time = CheckTime(request); !time)
            return std::unexpected(time.error());
        if (!IsSafeWebUrl(url))
            return std::unexpected(Error{ErrorCode::InvalidURL, "Website requests require a valid HTTPS URL."});
        auto response = client.Get(request);
        if (!response)
            return std::unexpected(response.error());
        if (auto time = CheckTime(request); !time)
            return std::unexpected(time.error());
        if (response->body.size() > limit)
            return std::unexpected(NetworkError("Website response exceeds its size limit."));
        if (response->status == 200)
            return Resource{std::move(url), std::move(*response)};
        if (response->status != 301 && response->status != 302 && response->status != 303 && response->status != 307 &&
            response->status != 308)
            return std::unexpected(NetworkError("Website did not return a usable resource.", response->status));
        auto next = Resolve(url, response->location);
        if (redirects == 3 || next.empty() || next == url)
            return std::unexpected(NetworkError("Website redirect rejected or limit exceeded."));
        url = std::move(next);
    }
    return std::unexpected(NetworkError("Website redirect limit exceeded."));
}
void AppendScalar(std::string &out, unsigned value) {
    if (value == 0 || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
        return;
    if (value < 0x80)
        out += static_cast<char>(value);
    else if (value < 0x800) {
        out += static_cast<char>(0xc0 | (value >> 6));
        out += static_cast<char>(0x80 | (value & 0x3f));
    } else if (value < 0x10000) {
        out += static_cast<char>(0xe0 | (value >> 12));
        out += static_cast<char>(0x80 | ((value >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (value & 0x3f));
    } else {
        out += static_cast<char>(0xf0 | (value >> 18));
        out += static_cast<char>(0x80 | ((value >> 12) & 0x3f));
        out += static_cast<char>(0x80 | ((value >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (value & 0x3f));
    }
}
std::string Entities(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] != '&') {
            out += input[i];
            continue;
        }
        const auto end = input.find(';', i + 1);
        if (end == std::string_view::npos || end - i > 12) {
            out += '&';
            continue;
        }
        auto entity = input.substr(i + 1, end - i - 1);
        unsigned value = 0;
        if (entity == "amp")
            value = '&';
        else if (entity == "quot")
            value = '"';
        else if (entity == "apos" || entity == "#39")
            value = '\'';
        else if (entity == "lt")
            value = '<';
        else if (entity == "gt")
            value = '>';
        else if (entity == "nbsp")
            value = ' ';
        else if (entity.starts_with('#')) {
            entity.remove_prefix(1);
            int radix = 10;
            if (entity.starts_with('x') || entity.starts_with('X')) {
                radix = 16;
                entity.remove_prefix(1);
            }
            const auto conversion = std::from_chars(entity.data(), entity.data() + entity.size(), value, radix);
            if (conversion.ec != std::errc{} || conversion.ptr != entity.data() + entity.size())
                value = 0;
        }
        if (value) {
            AppendScalar(out, value);
            i = end;
        } else {
            out += '&';
        }
    }
    return out;
}
std::string CleanTitle(std::string_view text) {
    // Large/unbounded title text has no value in a shortcut name. Invalid UTF-8 falls back to the domain.
    if (text.size() > 4096)
        return {};
    auto wide = Utf16(Entities(text));
    std::wstring clean;
    bool space = false;
    for (const auto c : wide) {
        if (c <= L' ' || c == 0x7f || c == 0xa0) {
            space = !clean.empty();
            continue;
        }
        // Do not allow metadata to inject directional formatting controls into a tool name.
        if ((c >= 0x202a && c <= 0x202e) || (c >= 0x2066 && c <= 0x2069))
            continue;
        if (space)
            clean += L' ';
        space = false;
        clean += c;
        if (clean.size() >= 200)
            break;
    }
    if (!clean.empty() && clean.back() >= 0xd800 && clean.back() <= 0xdbff)
        clean.pop_back();
    auto result = Utf8(clean);
    if (result.size() > 512) {
        size_t length = 512;
        while (length && (static_cast<unsigned char>(result[length]) & 0xc0) == 0x80)
            --length;
        result.resize(length);
    }
    return result;
}
using Attributes = std::map<std::string, std::string>;
Attributes ParseAttributes(std::string_view tag) {
    Attributes attributes;
    size_t pos = 0;
    while (pos < tag.size() && attributes.size() < 32) {
        while (pos < tag.size() && (Space(tag[pos]) || tag[pos] == '/'))
            ++pos;
        const auto begin = pos;
        while (pos < tag.size() && !Space(tag[pos]) && tag[pos] != '=' && tag[pos] != '/')
            ++pos;
        if (begin == pos)
            break;
        auto key = Lower(tag.substr(begin, pos - begin));
        while (pos < tag.size() && Space(tag[pos]))
            ++pos;
        if (pos == tag.size() || tag[pos] != '=')
            continue;
        ++pos;
        while (pos < tag.size() && Space(tag[pos]))
            ++pos;
        const char quote = pos < tag.size() && (tag[pos] == '\'' || tag[pos] == '"') ? tag[pos++] : 0;
        const auto valueStart = pos;
        while (pos < tag.size() && (quote ? tag[pos] != quote : !Space(tag[pos])))
            ++pos;
        attributes.emplace(std::move(key), Entities(tag.substr(valueStart, pos - valueStart)));
        if (quote && pos < tag.size())
            ++pos;
    }
    return attributes;
}
struct IconCandidate {
    std::string url, type, sizes;
    int priority = 4;
    unsigned size = 0;
    bool svg = false;
};
IconCandidate Candidate(std::string url, std::string type, std::string sizes, int priority) {
    IconCandidate result{std::move(url), Lower(type), Lower(sizes), priority};
    const auto path = Lower(result.url.substr(0, result.url.find_first_of("?#")));
    result.svg = result.type == "image/svg+xml" || path.ends_with(".svg");
    std::istringstream tokens(result.sizes);
    std::string token;
    while (tokens >> token) {
        unsigned w = 0, h = 0;
        const auto x = token.find('x');
        if (x == std::string::npos)
            continue;
        auto a = std::from_chars(token.data(), token.data() + x, w);
        auto b = std::from_chars(token.data() + x + 1, token.data() + token.size(), h);
        if (a.ec == std::errc{} && a.ptr == token.data() + x && b.ec == std::errc{} &&
            b.ptr == token.data() + token.size() && w <= 4096 && h <= 4096)
            result.size = std::max(result.size, std::min(w, h));
    }
    if (result.svg)
        result.priority = 0;
    else if (priority == 4 && (result.type == "image/png" || path.ends_with(".png")))
        result.priority = 1;
    return result;
}
struct HtmlMetadata {
    std::string title, manifest;
    std::vector<IconCandidate> icons;
};
HtmlMetadata ParseHtml(std::span<const uint8_t> bytes, std::string_view url) {
    HtmlMetadata metadata;
    const std::string_view html(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    const auto lower = Lower(html);
    std::string siteName;
    size_t pos = 0;
    for (unsigned tags = 0; tags < 4096 && pos < html.size(); ++tags) {
        const auto start = lower.find('<', pos);
        if (start == std::string::npos)
            break;
        if (lower.compare(start, 4, "<!--") == 0) {
            const auto end = lower.find("-->", start + 4);
            pos = end == std::string::npos ? html.size() : end + 3;
            continue;
        }
        size_t nameEnd = start + 1;
        while (nameEnd < html.size() && lower[nameEnd] >= 'a' && lower[nameEnd] <= 'z')
            ++nameEnd;
        const auto name = std::string_view(lower).substr(start + 1, nameEnd - start - 1);
        size_t end = nameEnd;
        char quote = 0;
        for (; end < html.size() && end - start <= 8192; ++end) {
            const char c = html[end];
            if (quote) {
                if (c == quote)
                    quote = 0;
            } else if (c == '\'' || c == '"')
                quote = c;
            else if (c == '>')
                break;
        }
        if (end >= html.size() || end - start > 8192)
            break;
        pos = end + 1;
        if (name == "script" || name == "style") {
            const auto close = lower.find("</" + std::string(name), pos);
            pos = close == std::string::npos ? html.size() : close;
        } else if (name == "title" && metadata.title.empty()) {
            const auto close = lower.find("</title", pos);
            if (close != std::string::npos)
                metadata.title = CleanTitle(html.substr(pos, close - pos));
        } else if (name == "meta" || name == "link") {
            const auto attributes = ParseAttributes(html.substr(nameEnd, end - nameEnd));
            const auto get = [&attributes](const char *key) -> std::string {
                const auto it = attributes.find(key);
                return it == attributes.end() ? std::string{} : it->second;
            };
            if (name == "meta" &&
                (Lower(get("property")) == "og:site_name" || Lower(get("name")) == "application-name"))
                siteName = CleanTitle(get("content"));
            if (name == "link") {
                auto rel = Lower(get("rel"));
                for (auto &c : rel)
                    if (Space(c))
                        c = ' ';
                rel = " " + rel + " ";
                const auto href = Resolve(url, get("href"));
                if (href.empty())
                    continue;
                if (rel.find(" manifest ") != std::string::npos && metadata.manifest.empty())
                    metadata.manifest = href;
                const bool apple = rel.find(" apple-touch-icon ") != std::string::npos ||
                                   rel.find(" apple-touch-icon-precomposed ") != std::string::npos;
                if ((rel.find(" icon ") != std::string::npos || apple) && metadata.icons.size() < 32)
                    metadata.icons.push_back(Candidate(href, get("type"), get("sizes"), apple ? 2 : 4));
            }
        }
    }
    if (metadata.title.empty())
        metadata.title = std::move(siteName);
    return metadata;
}
bool FailureCached(const std::filesystem::path &file) {
    std::error_code ec;
    const auto written = std::filesystem::last_write_time(file, ec);
    if (ec)
        return false;
    const auto age = std::filesystem::file_time_type::clock::now() - written;
    return age >= decltype(age)::zero() && age < std::chrono::hours(24);
}
Status CacheBytes(const std::filesystem::path &file, std::span<const uint8_t> bytes) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    if (ec)
        return std::unexpected(NetworkError("Cannot create icon cache.", ec.value()));
    static std::atomic<unsigned> sequence{0};
    auto temporary = file;
    temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++sequence) + L".tmp";
    HANDLE handle =
        CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return std::unexpected(NetworkError("Cannot create icon cache entry.", GetLastError()));
    DWORD written = 0;
    const bool saved = WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                       written == bytes.size() && FlushFileBuffers(handle);
    CloseHandle(handle);
    if (!saved || !MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto native = GetLastError();
        std::filesystem::remove(temporary, ec);
        return std::unexpected(NetworkError("Cannot save icon cache entry.", native));
    }
    return {};
}
} // namespace

Result<HttpResponse> WinHttpClient::Get(const HttpRequest &request) {
    if (!IsSafeWebUrl(request.url) || request.maxBytes == 0 || request.maxBytes > IconLimit)
        return std::unexpected(Error{ErrorCode::InvalidURL, "Invalid bounded HTTPS request."});
    if (auto time = CheckTime(request); !time)
        return std::unexpected(time.error());
    const auto url = Utf16(request.url);
    URL_COMPONENTS parts{sizeof(URL_COMPONENTS)};
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS)
        return std::unexpected(Error{ErrorCode::InvalidURL, "Cannot parse HTTPS request."});
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring object;
    if (parts.dwUrlPathLength)
        object.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (object.empty())
        object = L"/";
    if (parts.dwExtraInfoLength)
        object.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (const auto fragment = object.find(L'#'); fragment != std::wstring::npos)
        object.resize(fragment);
    InternetHandle session(WinHttpOpen(L"ExileKit/0.3", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
    if (!session.value || !WinHttpSetTimeouts(session.value, 1000, 1000, 1000, 1000))
        return std::unexpected(NetworkError("Cannot initialize website request.", GetLastError()));
    // This WinHTTP option counts initial connection attempts, despite its name.
    DWORD retries = 1;
    if (!WinHttpSetOption(session.value, WINHTTP_OPTION_CONNECT_RETRIES, &retries, sizeof(retries)))
        return std::unexpected(NetworkError("Cannot bound website connection attempts.", GetLastError()));
    InternetHandle connection(WinHttpConnect(session.value, host.c_str(), parts.nPort, 0));
    if (!connection.value)
        return std::unexpected(NetworkError("Cannot initialize website connection.", GetLastError()));
    AsyncRequest pending;
    pending.handle = WinHttpOpenRequest(connection.value, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!pending.handle || !pending.state->event)
        return std::unexpected(NetworkError("Cannot initialize asynchronous request.", GetLastError()));
    DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_AUTHENTICATION;
    DWORD logon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    DWORD headerLimit = 16 * 1024;
    DWORD_PTR context = reinterpret_cast<DWORD_PTR>(pending.state);
    if (!WinHttpSetOption(pending.handle, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) ||
        !WinHttpSetOption(pending.handle, WINHTTP_OPTION_AUTOLOGON_POLICY, &logon, sizeof(logon)) ||
        !WinHttpSetOption(pending.handle, WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE, &headerLimit, sizeof(headerLimit)) ||
        !WinHttpSetOption(pending.handle, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)))
        return std::unexpected(NetworkError("Cannot apply website request policy.", GetLastError()));
    const auto previous = WinHttpSetStatusCallback(
        pending.handle, HttpCallback,
        WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE | WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE |
            WINHTTP_CALLBACK_FLAG_READ_COMPLETE | WINHTTP_CALLBACK_FLAG_REQUEST_ERROR | WINHTTP_CALLBACK_FLAG_HANDLES,
        0);
    if (previous == WINHTTP_INVALID_STATUS_CALLBACK)
        return std::unexpected(NetworkError("Cannot set website request callback.", GetLastError()));
    pending.state->references.fetch_add(1); // Released by HANDLE_CLOSING, including all cancellation paths.
    if (!WinHttpSendRequest(pending.handle, L"Accept: text/html,image/*;q=0.9\r\n", static_cast<DWORD>(-1),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, context))
        return std::unexpected(NetworkError("Cannot send website request.", GetLastError()));
    if (auto ready = Wait(*pending.state, request); !ready)
        return std::unexpected(ready.error());
    ResetEvent(pending.state->event);
    if (!WinHttpReceiveResponse(pending.handle, nullptr))
        return std::unexpected(NetworkError("Cannot receive website response.", GetLastError()));
    if (auto ready = Wait(*pending.state, request); !ready)
        return std::unexpected(ready.error());
    HttpResponse response;
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(pending.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
        return std::unexpected(NetworkError("Cannot read website status.", GetLastError()));
    response.status = status;
    response.location = Header(pending.handle, WINHTTP_QUERY_LOCATION);
    response.contentType = Header(pending.handle, WINHTTP_QUERY_CONTENT_TYPE);
    if (status != 200)
        return response;
    const auto length = Header(pending.handle, WINHTTP_QUERY_CONTENT_LENGTH);
    uint64_t advertised = 0;
    if (!length.empty()) {
        const auto parsed = std::from_chars(length.data(), length.data() + length.size(), advertised);
        if (parsed.ec != std::errc{} || parsed.ptr != length.data() + length.size() || advertised > request.maxBytes)
            return std::unexpected(NetworkError("Website response exceeds its size limit."));
    }
    for (;;) {
        if (auto time = CheckTime(request); !time)
            return std::unexpected(time.error());
        ResetEvent(pending.state->event);
        if (!WinHttpReadData(pending.handle, pending.state->buffer.data(),
                             static_cast<DWORD>(pending.state->buffer.size()), nullptr))
            return std::unexpected(NetworkError("Cannot read website response.", GetLastError()));
        if (auto ready = Wait(*pending.state, request); !ready)
            return std::unexpected(ready.error());
        const auto received = pending.state->length.load();
        if (!received)
            break;
        if (received > pending.state->buffer.size() || response.body.size() + received > request.maxBytes)
            return std::unexpected(NetworkError("Website response exceeds its size limit."));
        response.body.insert(response.body.end(), pending.state->buffer.begin(),
                             pending.state->buffer.begin() + received);
    }
    return response;
}

Result<WebMetadata> WebMetadataProvider::Fetch(std::string_view toolId, std::string_view url, bool needTitle,
                                               std::stop_token stop, uint32_t targetPx) {
    if (!IsValidToolId(toolId) || !IsSafeWebUrl(url))
        return std::unexpected(Error{ErrorCode::InvalidURL, "Invalid website metadata key or HTTPS URL."});
    const auto deadline = Clock::now() + std::chrono::seconds(6);
    if (stop.stop_requested())
        return std::unexpected(Error{ErrorCode::Cancelled, "Website metadata request cancelled."});
    WebMetadata metadata;
    for (const auto *extension : {L".icon", L".png", L".ico"}) {
        auto cached = IconProvider::DecodeFile(cache_ / (Utf16(toolId) + extension), targetPx);
        if (cached) {
            metadata.icon = std::move(*cached);
            break;
        }
    }
    const auto failed = cache_ / (Utf16(toolId) + L".failed");
    const auto qualityFile = cache_ / (Utf16(toolId) + L".quality-v1.json");
    bool qualityCached = false;
    // Original source bytes remain reusable at every DPI; old caches get one bounded upgrade attempt.
    std::error_code qualityError;
    const auto qualitySize = std::filesystem::file_size(qualityFile, qualityError);
    if (!qualityError && qualitySize < 16384) {
        std::ifstream file(qualityFile);
        try {
            const std::string text((std::istreambuf_iterator<char>(file)), {});
            const auto quality = detail::ParseJson(text, ErrorCode::IoError);
            qualityCached = quality.value("url", "") == url;
            if (qualityCached)
                metadata.svgIcon = quality.value("svg", "");
        } catch (const std::exception &) {
        } catch (const Error &) {
        }
    }
    const bool suppressIcon = (metadata.icon.has_value() && qualityCached) || FailureCached(failed);
    if (!needTitle && suppressIcon)
        return metadata;
    Error lastError = NetworkError("Website metadata unavailable; use the shortcut fallback.");
    std::optional<Resource> selected;
    const auto saveIcon = [&](const Resource &resource) -> bool {
        auto icon = IconProvider::DecodeBytes(resource.response.body, targetPx);
        if (!icon) {
            lastError = icon.error();
            return false;
        }
        if (!metadata.icon || std::min(icon->sourceWidth, icon->sourceHeight) >=
                                  std::min(metadata.icon->sourceWidth, metadata.icon->sourceHeight)) {
            metadata.icon = std::move(*icon);
            metadata.icon->source = resource.url;
            selected = resource;
        }
        return metadata.icon && std::min(metadata.icon->sourceWidth, metadata.icon->sourceHeight) >=
                                    IconProvider::SourcePixels(targetPx);
    };
    std::vector<std::string> attemptedIcons;
    const auto fetchIcon = [&](const std::string &iconUrl) -> bool {
        if (attemptedIcons.size() >= 8 ||
            std::find(attemptedIcons.begin(), attemptedIcons.end(), iconUrl) != attemptedIcons.end())
            return false;
        attemptedIcons.push_back(iconUrl);
        auto response = Download(*client_, iconUrl, IconLimit, deadline, stop);
        if (!response) {
            lastError = response.error();
            return false;
        }
        return saveIcon(*response);
    };
    std::string fallback = Origin(url) + "/favicon.ico";
    auto html = Download(*client_, std::string(url), HtmlLimit, deadline, stop);
    if (html) {
        fallback = Origin(html->url) + "/favicon.ico";
        const auto mime = Lower(html->response.contentType);
        if (mime.empty() || mime.starts_with("text/html") || mime.starts_with("application/xhtml+xml")) {
            auto parsed = ParseHtml(html->response.body, html->url);
            if (needTitle)
                metadata.title = std::move(parsed.title);
            if (!suppressIcon) {
                const auto rank = [targetPx](const auto &a, const auto &b) {
                    const bool smallA = a.size && a.size < targetPx, smallB = b.size && b.size < targetPx;
                    if (smallA != smallB)
                        return !smallA;
                    if (a.priority != b.priority)
                        return a.priority < b.priority;
                    return a.size > b.size;
                };
                std::stable_sort(parsed.icons.begin(), parsed.icons.end(), rank);
                bool satisfied = false;
                for (const auto &candidate : parsed.icons) {
                    if (candidate.svg) {
                        if (metadata.svgIcon.empty())
                            metadata.svgIcon = candidate.url;
                        continue;
                    }
                    // Try high-quality HTML sources before spending the deadline on a manifest.
                    if (candidate.priority < 3 && (!candidate.size || candidate.size >= targetPx) &&
                        attemptedIcons.size() < 7 && fetchIcon(candidate.url)) {
                        satisfied = true;
                        break;
                    }
                }
                if (!satisfied && !parsed.manifest.empty()) {
                    auto manifest = Download(*client_, parsed.manifest, HtmlLimit, deadline, stop);
                    if (manifest)
                        try {
                            const std::string text(manifest->response.body.begin(), manifest->response.body.end());
                            const auto json = detail::ParseJson(text, ErrorCode::IoError);
                            if (json.contains("icons") && json["icons"].is_array())
                                for (const auto &entry : json["icons"]) {
                                    if (parsed.icons.size() >= 48)
                                        break;
                                    if (!entry.is_object() || !entry.contains("src") || !entry["src"].is_string())
                                        continue;
                                    const auto href = Resolve(manifest->url, entry["src"].get<std::string>());
                                    if (!href.empty())
                                        parsed.icons.push_back(
                                            Candidate(href, entry.value("type", ""), entry.value("sizes", ""), 3));
                                }
                        } catch (const std::exception &) { /* Try HTML icons and favicon. */
                        } catch (const Error &) {          /* Bounded JSON parser rejected the manifest. */
                        }
                }
                std::stable_sort(parsed.icons.begin(), parsed.icons.end(), rank);
                for (const auto &candidate : parsed.icons) {
                    // WIC has no built-in SVG decoder. Retain its URL as metadata; use raster fallback.
                    if (candidate.svg) {
                        if (metadata.svgIcon.empty())
                            metadata.svgIcon = candidate.url;
                        continue;
                    }
                    if (satisfied || attemptedIcons.size() >= 7)
                        break; // Reserve a request for favicon.ico.
                    if (fetchIcon(candidate.url)) {
                        satisfied = true;
                        break;
                    }
                }
            }
        }
    } else {
        lastError = html.error();
    }
    if (!suppressIcon && (!metadata.icon || std::min(metadata.icon->sourceWidth, metadata.icon->sourceHeight) <
                                                IconProvider::SourcePixels(targetPx)))
        fetchIcon(fallback);
    if (stop.stop_requested())
        return std::unexpected(Error{ErrorCode::Cancelled, "Website metadata request cancelled."});
    if (selected) {
        // Keep original encoded bytes, never the display-size bitmap, in the disk cache.
        if (CacheBytes(cache_ / (Utf16(toolId) + L".icon"), selected->response.body)) {
            const auto quality = nlohmann::json{{"url", url},
                                                {"selected", selected->url},
                                                {"svg", metadata.svgIcon},
                                                {"width", metadata.icon->sourceWidth},
                                                {"height", metadata.icon->sourceHeight}}
                                     .dump();
            (void)CacheBytes(qualityFile, {reinterpret_cast<const uint8_t *>(quality.data()), quality.size()});
        }
        std::error_code ec;
        std::filesystem::remove(failed, ec);
    } else if (!suppressIcon) {
        if (!metadata.svgIcon.empty()) {
            const auto vectorMetadata = nlohmann::json{{"url", url}, {"svg", metadata.svgIcon}}.dump();
            (void)CacheBytes(qualityFile,
                             {reinterpret_cast<const uint8_t *>(vectorMetadata.data()), vectorMetadata.size()});
        }
        constexpr std::array<uint8_t, 1> marker{'1'};
        (void)CacheBytes(failed, marker);
    }
    if (metadata.title.empty() && !metadata.icon && metadata.svgIcon.empty())
        return std::unexpected(std::move(lastError));
    return metadata;
}
} // namespace poetoolbox
