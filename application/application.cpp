#include "application.h"
#include "icon_provider.h"
#include "launcher.h"
#include <chrono>
#include <exception>
#include <objbase.h>
#include <windows.h>

namespace poetoolbox {
namespace {
using Clock = std::chrono::steady_clock;
double Elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
bool ExistingExe(const std::filesystem::path &path) {
    std::error_code ec;
    auto extension = path.extension().wstring();
    for (auto &c : extension)
        c = static_cast<wchar_t>(towlower(c));
    return path.is_absolute() && extension == L".exe" && std::filesystem::is_regular_file(path, ec);
}
std::int64_t Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
} // namespace
ApplicationServices::ApplicationServices(std::filesystem::path resourcesRoot, std::filesystem::path dataRoot,
                                         HttpClient *httpClient)
    : root_(std::move(resourcesRoot)), dataRoot_(std::move(dataRoot)), httpClient_(httpClient) {}
ApplicationServices::~ApplicationServices() {
    Stop();
}
void ApplicationServices::Start(std::function<void()> notify) {
    if (worker_.joinable())
        return;
    notify_ = std::move(notify);
    worker_ = std::jthread([this] {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                if (jobs_.empty() && stopping_)
                    break;
                job = std::move(jobs_.front());
                jobs_.pop_front();
                busy_ = true;
            }
            try {
                job();
            } catch (const std::exception &e) {
                Complete([this, message = std::string(e.what())] { ReportError({ErrorCode::IoError, message}); });
            }
            {
                std::lock_guard lock(mutex_);
                busy_ = false;
            }
            condition_.notify_all();
        }
        if (SUCCEEDED(com))
            CoUninitialize();
    });
    Enqueue([this] {
        auto loaded = std::make_shared<AppData>();
        std::optional<Error> failure;
        auto paths = PathManager::Create(dataRoot_);
        if (paths) {
            loaded->paths = *paths;
            const auto ensure = paths->EnsureDirectories();
            if (!ensure)
                failure = ensure.error();
            const auto file = paths->ConfigDirectory() / L"settings.json";
            std::error_code ec;
            if (!std::filesystem::exists(file, ec) && !paths->LegacyConfigFile().empty() &&
                std::filesystem::is_regular_file(paths->LegacyConfigFile(), ec)) {
                std::filesystem::copy_file(paths->LegacyConfigFile(), file, std::filesystem::copy_options::none, ec);
                if (ec)
                    failure = Error{ErrorCode::IoError, "Cannot copy the previous configuration.",
                                    static_cast<uint32_t>(ec.value())};
            }
            const auto config = ConfigManager(file).Load();
            if (config)
                loaded->config = *config;
            else {
                failure = config.error();
                auto backup = file;
                backup += L".recovery-" + std::to_wstring(GetTickCount64()) + L".json";
                ec.clear();
                std::filesystem::copy_file(file, backup, std::filesystem::copy_options::none, ec);
                loaded->canSaveConfig = !ec;
            }
            if (loaded->config.managedToolsDirectory.empty())
                loaded->config.managedToolsDirectory = paths->DefaultManagedToolsDirectory();
        } else
            failure = paths.error();
        auto start = Clock::now();
        const auto locale = loaded->locale.Load(root_ / L"resources" / L"locales");
        loaded->localizationMs = Elapsed(start);
        if (!locale)
            failure = locale.error();
        (void)loaded->locale.SetLanguage(loaded->config.language.empty() ? SystemDefaultLanguage()
                                                                         : loaded->config.language);
        start = Clock::now();
        const auto registry = loaded->registry.Load(root_ / L"tools" / L"manifests");
        loaded->manifestMs = Elapsed(start);
        if (!registry)
            failure = registry.error();
        for (const auto &[id, path] : loaded->config.executablePaths)
            if (ExistingExe(path))
                loaded->installed.insert(id);
        HomeService::Evaluate(loaded->config, Now());
        Complete([this, loaded, failure] {
            data_ = std::make_unique<AppData>(std::move(*loaded));
            error_ = failure;
            statusKey_ = failure ? "status.error" : "status.ready";
            SaveConfig();
        });
    });
}
void ApplicationServices::Stop() {
    if (!worker_.joinable())
        return;
    {
        std::lock_guard lock(networkMutex_);
        networkStopping_ = true;
        networkJobs_.clear();
    }
    if (networkWorker_.joinable()) {
        networkWorker_.request_stop();
        networkCondition_.notify_all();
        networkWorker_.join();
    }
    // Drain action completions and their queued config writes before shutting the worker down.
    for (;;) {
        Drain();
        std::unique_lock lock(mutex_);
        if (jobs_.empty() && !busy_ && completions_.empty()) {
            stopping_ = true;
            break;
        }
        condition_.wait(lock, [this] { return (jobs_.empty() && !busy_) || !completions_.empty(); });
    }
    condition_.notify_all();
    if (worker_.joinable())
        worker_.join();
    notify_ = {};
}
void ApplicationServices::Enqueue(std::function<void()> job) {
    {
        std::lock_guard lock(mutex_);
        if (stopping_)
            return;
        jobs_.push_back(std::move(job));
    }
    condition_.notify_one();
}
void ApplicationServices::Complete(std::function<void()> update, std::string icon) {
    {
        std::lock_guard lock(mutex_);
        completions_.push_back({std::move(update), std::move(icon)});
    }
    condition_.notify_all();
    if (notify_)
        notify_();
}
AppChanges ApplicationServices::Drain() {
    std::deque<Completion> pending;
    {
        std::lock_guard lock(mutex_);
        pending.swap(completions_);
    }
    AppChanges changes;
    for (auto &completion : pending) {
        completion.update();
        if (completion.icon.empty())
            changes.full = true;
        else
            changes.icons.push_back(std::move(completion.icon));
    }
    return changes;
}
std::string ApplicationServices::Tr(std::string_view key, std::string_view fallback) const {
    return data_ ? data_->locale.GetString(key, fallback) : std::string(fallback.empty() ? key : fallback);
}
void ApplicationServices::ReportError(Error error) {
    error_ = std::move(error);
    statusKey_ = "status.error";
}
void ApplicationServices::SaveConfig() {
    if (!data_ || !data_->paths)
        return;
    ++configRevision_;
    if (!data_->canSaveConfig) {
        ReportError({ErrorCode::InvalidConfig, "Cannot preserve the existing configuration; saving is disabled."});
        return;
    }
    const auto snapshot = data_->config;
    const auto file = data_->paths->ConfigDirectory() / L"settings.json";
    Enqueue([this, snapshot, file] {
        const auto result = ConfigManager(file).Save(snapshot);
        if (!result)
            Complete([this, error = result.error()] { ReportError(error); });
    });
}
void ApplicationServices::SetLanguage(std::string language) {
    if (!data_ || !data_->locale.SetLanguage(language))
        return;
    data_->config.language = std::move(language);
    SaveConfig();
}
void ApplicationServices::SetGame(GameFilter game) {
    if (data_) {
        data_->config.selectedGame = game;
        SaveConfig();
    }
}
void ApplicationServices::ToggleFavorite(std::string id) {
    if (!data_ || !data_->registry.FindTool(id))
        return;
    if (!data_->config.favorites.erase(id))
        data_->config.favorites.insert(std::move(id));
    SaveConfig();
}
void ApplicationServices::ConfigureExecutable(std::string id, std::filesystem::path path) {
    if (!data_)
        return;
    const auto *tool = data_->registry.FindTool(id);
    if (!tool || tool->manifest.type != ToolType::Application) {
        ReportError({ErrorCode::ToolNotFound, id});
        return;
    }
    Enqueue([this, id, path] {
        if (!ExistingExe(path)) {
            Complete([this] { ReportError({ErrorCode::ExecutableNotFound, "Selected executable is unavailable."}); });
            return;
        }
        Complete([this, id, path] {
            data_->config.executablePaths[id] = path;
            data_->installed.insert(id);
            data_->icons.erase(id);
            iconsRequested_.erase(id);
            error_.reset();
            statusKey_ = "status.saved";
            SaveConfig();
        });
    });
}
void ApplicationServices::SetManagedDirectory(std::filesystem::path path) {
    if (!data_ || !data_->paths)
        return;
    Enqueue([this, path] {
        const auto valid = PathManager::ValidateManagedDirectory(path);
        Complete([this, valid, path] {
            if (!valid) {
                ReportError(valid.error());
                return;
            }
            data_->config.managedToolsDirectory = path;
            error_.reset();
            statusKey_ = "status.saved";
            SaveConfig();
        });
    });
}
void ApplicationServices::Launch(std::string id, bool riskAccepted) {
    if (!data_)
        return;
    if (const auto custom = data_->config.customTools.find(id); custom != data_->config.customTools.end()) {
        statusKey_ = "status.opening";
        error_.reset();
        Enqueue([this, tool = custom->second] {
            const auto result = ToolLauncher().LaunchCustom(tool);
            Complete([this, id = tool.id, result] { FinishLaunch(id, result); });
        });
        return;
    }
    const auto *tool = data_->registry.FindTool(id);
    if (!tool) {
        ReportError({ErrorCode::ToolNotFound, id});
        return;
    }
    const auto path = data_->config.executablePaths.find(id);
    const auto executable = path == data_->config.executablePaths.end() ? std::filesystem::path{} : path->second;
    statusKey_ = "status.opening";
    error_.reset();
    Enqueue([this, id, manifest = tool->manifest, executable, riskAccepted] {
        const auto result = ToolLauncher().Launch(manifest, executable, riskAccepted);
        Complete([this, id, result] { FinishLaunch(id, result); });
    });
}
void ApplicationServices::OpenDownloadPage(std::string id) {
    if (!data_)
        return;
    const auto *tool = data_->registry.FindTool(id);
    if (!tool)
        return;
    ToolManifest link;
    link.id = id;
    link.type = ToolType::ExternalLink;
    link.launch.url = tool->manifest.downloadPage.empty() ? tool->manifest.homepage : tool->manifest.downloadPage;
    Enqueue([this, link] {
        const auto result = ToolLauncher().Launch(link);
        if (!result)
            Complete([this, error = result.error()] { ReportError(error); });
    });
}
void ApplicationServices::SetIconMetrics(float dip, float dpi) {
    const auto pixels = IconProvider::TargetPixels(dip, dpi);
    if (pixels == iconTargetPx_)
        return;
    iconTargetPx_ = pixels;
    iconsRequested_.clear();
    if (data_)
        data_->icons.clear();
}
void ApplicationServices::RequestIcon(std::string id, bool allowRemote) {
    if (!data_ || !data_->paths)
        return;
    if (allowRemote)
        QueueWebMetadata(id);
    if (iconsRequested_.contains(id))
        return;
    const auto targetPx = iconTargetPx_;
    const auto ticket = ++nextIconTicket_;
    if (const auto custom = data_->config.customTools.find(id); custom != data_->config.customTools.end()) {
        iconsRequested_[id] = ticket;
        Enqueue([this, tool = custom->second, cache = data_->paths->IconCacheDirectory(), targetPx, ticket] {
            const auto icon = IconProvider(cache).LoadCustom(tool, targetPx);
            if (icon) {
                auto pixels = std::make_shared<IconPixels>(*icon);
                Complete(
                    [this, id = tool.id, pixels, ticket] {
                        if (const auto request = iconsRequested_.find(id);
                            request != iconsRequested_.end() && request->second == ticket)
                            data_->icons[id] = pixels;
                    },
                    tool.id);
            }
        });
        return;
    }
    const auto *tool = data_->registry.FindTool(id);
    if (!tool)
        return;
    iconsRequested_[id] = ticket;
    const auto path = data_->config.executablePaths.find(id);
    const auto executable = path == data_->config.executablePaths.end() ? std::filesystem::path{} : path->second;
    Enqueue([this, id, manifest = tool->manifest, executable, cache = data_->paths->IconCacheDirectory(), targetPx,
             ticket] {
        const auto icon = IconProvider(cache).Load(manifest, executable, root_, targetPx);
        if (icon) {
            auto pixels = std::make_shared<IconPixels>(*icon);
            Complete(
                [this, id, pixels, executable, ticket] {
                    const auto entry = data_->config.executablePaths.find(id);
                    const auto current =
                        entry == data_->config.executablePaths.end() ? std::filesystem::path{} : entry->second;
                    const auto request = iconsRequested_.find(id);
                    if (current == executable && request != iconsRequested_.end() && request->second == ticket)
                        data_->icons[id] = pixels;
                },
                id);
        }
        // A failed local icon is terminal for this session; placeholder remains. No retry loop.
    });
}
} // namespace poetoolbox
