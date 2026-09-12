#pragma once
#include "poetoolbox/image.h"
#include "poetoolbox/result.h"
#include <chrono>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace poetoolbox {
struct HttpRequest {
    std::string url;
    size_t maxBytes = 0;
    std::chrono::steady_clock::time_point deadline;
    std::stop_token stop;
};
struct HttpResponse {
    unsigned status = 0;
    std::string location;
    std::string contentType;
    std::vector<uint8_t> body;
};
// The boundary returns one response without following redirects or retrying.
class HttpClient {
  public:
    virtual ~HttpClient() = default;
    [[nodiscard]] virtual Result<HttpResponse> Get(const HttpRequest &request) = 0;
};
class WinHttpClient final : public HttpClient {
  public:
    [[nodiscard]] Result<HttpResponse> Get(const HttpRequest &request) override;
};
struct WebMetadata {
    std::string title;
    std::optional<IconPixels> icon;
};
// Invoke on the dedicated networking worker after first paint. COM must be initialized there.
// Fetches share a six-second deadline, at most three redirects per resource, and no retries.
class WebMetadataProvider final {
  public:
    explicit WebMetadataProvider(std::filesystem::path cache, HttpClient *client = nullptr)
        : cache_(std::move(cache)), client_(client ? client : &system_) {}
    [[nodiscard]] Result<WebMetadata> Fetch(std::string_view toolId, std::string_view url, bool needTitle,
                                            std::stop_token stop = {});

  private:
    std::filesystem::path cache_;
    WinHttpClient system_;
    HttpClient *client_;
};
} // namespace poetoolbox
