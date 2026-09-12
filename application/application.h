#pragma once
#include "paths.h"
#include "poetoolbox/config.h"
#include "poetoolbox/home.h"
#include "poetoolbox/image.h"
#include "poetoolbox/localization.h"
#include "poetoolbox/registry.h"
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace poetoolbox {
class HttpClient;
struct AppData {
    ToolRegistry registry;
    LocalizationManager locale;
    UserConfig config;
    std::optional<PathManager> paths;
    std::set<std::string, std::less<>> installed;
    std::map<std::string, std::shared_ptr<const IconPixels>, std::less<>> icons;
    double manifestMs = 0, localizationMs = 0;
    bool canSaveConfig = true;
};
struct AppChanges {
    bool full = false;
    std::vector<std::string> icons;
};

// UI-thread facade. Local I/O and launching use one worker; cancellable HTTP uses another.
class ApplicationServices final {
  public:
    explicit ApplicationServices(std::filesystem::path resourcesRoot, std::filesystem::path dataRoot = {},
                                 HttpClient *httpClient = nullptr);
    ~ApplicationServices();
    void Start(std::function<void()> notify);
    void Stop();
    AppChanges Drain();
    [[nodiscard]] const AppData *Data() const { return data_.get(); }
    [[nodiscard]] std::string Tr(std::string_view key, std::string_view fallback = {}) const;
    [[nodiscard]] const std::optional<Error> &LastError() const { return error_; }
    [[nodiscard]] const std::string &StatusKey() const { return statusKey_; }
    void SetLanguage(std::string language);
    void SetGame(GameFilter game);
    void ToggleFavorite(std::string id);
    void AddToHome(std::string id);
    void PinToHome(std::string id);
    void Unpin(std::string id);
    void RemoveFromHome(std::string id);
    void SetHomeAutoRemoval(int days);
    void RefreshHome();
    [[nodiscard]] std::vector<std::string> HomeIds() const;
    void AddCustomShortcut(std::string input);
    void ConfigureExecutable(std::string id, std::filesystem::path path);
    void SetManagedDirectory(std::filesystem::path path);
    void Launch(std::string id, bool riskAccepted);
    void OpenDownloadPage(std::string id);
    void RequestIcon(std::string id, bool allowRemote = false);
    void ReportError(Error error);

  private:
    void Enqueue(std::function<void()> job);
    void Complete(std::function<void()> update, std::string icon = {});
    void SaveConfig();
    void QueueWebMetadata(std::string id, bool needTitle = false);
    void ApplyWebsiteTitle(std::string id, std::string url, std::string title);
    void EnqueueNetwork(std::function<void(std::stop_token)> job);
    void FinishLaunch(std::string id, Status result);
    [[nodiscard]] bool HasTool(std::string_view id) const;
    struct Completion {
        std::function<void()> update;
        std::string icon;
    };
    std::filesystem::path root_;
    std::filesystem::path dataRoot_;
    std::unique_ptr<AppData> data_;
    std::uint64_t configRevision_ = 0;
    std::optional<Error> error_;
    std::string statusKey_ = "status.loading";
    std::set<std::string, std::less<>> iconsRequested_;
    std::set<std::string, std::less<>> remoteRequested_;
    std::mutex networkMutex_;
    std::condition_variable networkCondition_;
    std::deque<std::function<void(std::stop_token)>> networkJobs_;
    std::jthread networkWorker_;
    bool networkStopping_ = false;
    HttpClient *httpClient_ = nullptr; // Optional test boundary, caller owns its lifetime.
    std::function<void()> notify_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<std::function<void()>> jobs_;
    std::deque<Completion> completions_;
    bool stopping_ = false;
    bool busy_ = false;
    std::jthread worker_;
};
} // namespace poetoolbox
