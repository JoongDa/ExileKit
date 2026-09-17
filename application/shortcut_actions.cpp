#include "application.h"
#include "custom_shortcut.h"
#include "web_metadata.h"
#include "utf.h"
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
namespace poetoolbox {
namespace {
std::int64_t Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
std::string NewId() {
    GUID id{};
    if (FAILED(CoCreateGuid(&id)))
        return {};
    wchar_t text[40]{};
    StringFromGUID2(id, text, 40);
    auto value = Utf8(text);
    value.erase(std::remove(value.begin(), value.end(), '{'), value.end());
    value.erase(std::remove(value.begin(), value.end(), '}'), value.end());
    for (auto &ch : value)
        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
    return "user-" + value;
}
} // namespace
void ApplicationServices::AddCustomShortcut(std::string input) {
    if (!data_)
        return;
    Enqueue([this, input = std::move(input), candidate = data_->config, revision = configRevision_]() mutable {
        const auto id = NewId();
        auto parsed = ParseCustomShortcut(input, id);
        Status valid;
        if (parsed) {
            for (const auto &[existingId, existing] : candidate.customTools)
                if (existing.kind == parsed->kind && existing.target == parsed->target) {
                    *parsed = existing;
                    break;
                }
            candidate.customTools[parsed->id] = *parsed;
            HomeService::Add(candidate, parsed->id, Now());
            valid = ConfigManager::Validate(candidate);
        }
        Complete([this, input, revision, valid, parsed = std::move(parsed)] {
            if (!parsed) {
                ReportError({ErrorCode::InvalidShortcut, parsed.error().message, parsed.error().nativeCode});
                return;
            }
            // Configuration may change while parsing. Recheck the latest snapshot before committing.
            if (revision != configRevision_) {
                AddCustomShortcut(input);
                return;
            }
            if (!valid) {
                ReportError(valid.error());
                return;
            }
            for (const auto &[id, existing] : data_->config.customTools)
                if (existing.kind == parsed->kind && existing.target == parsed->target) {
                    HomeService::Add(data_->config, id, Now());
                    error_.reset();
                    statusKey_ = "status.saved";
                    SaveConfig();
                    return;
                }
            data_->config.customTools[parsed->id] = *parsed;
            HomeService::Add(data_->config, parsed->id, Now());
            error_.reset();
            statusKey_ = "status.saved";
            SaveConfig();
            RequestIcon(parsed->id);
            if (parsed->kind == ShortcutKind::Url)
                QueueWebMetadata(parsed->id, true);
        });
    });
}
void ApplicationServices::ApplyWebsiteTitle(std::string id, std::string url, std::string title) {
    const auto custom = data_->config.customTools.find(id);
    if (custom == data_->config.customTools.end() || custom->second.target != url || custom->second.name == title)
        return;
    auto candidate = data_->config;
    candidate.customTools.at(id).name = title;
    Enqueue([this, id, url, title, candidate = std::move(candidate), revision = configRevision_] {
        const auto valid = ConfigManager::Validate(candidate);
        Complete(
            [this, id, url, title, revision, valid] {
                if (!valid)
                    return; // Keep the domain/name fallback if metadata cannot be persisted.
                if (revision != configRevision_) {
                    ApplyWebsiteTitle(id, url, title);
                    return;
                }
                data_->config.customTools.at(id).name = title;
                SaveConfig();
            },
            id);
    });
}
void ApplicationServices::EnqueueNetwork(std::function<void(std::stop_token)> job) {
    std::lock_guard lock(networkMutex_);
    if (networkStopping_ || networkJobs_.size() >= 128)
        return;
    networkJobs_.push_back(std::move(job));
    if (!networkWorker_.joinable()) {
        networkWorker_ = std::jthread([this](std::stop_token stop) {
            const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            while (!stop.stop_requested()) {
                std::function<void(std::stop_token)> next;
                {
                    std::unique_lock guard(networkMutex_);
                    networkCondition_.wait(guard,
                                           [this, stop] { return stop.stop_requested() || !networkJobs_.empty(); });
                    if (stop.stop_requested())
                        break;
                    next = std::move(networkJobs_.front());
                    networkJobs_.pop_front();
                }
                try {
                    next(stop);
                } catch (const std::exception &) { /* Local name / placeholder remain usable. */
                }
            }
            if (SUCCEEDED(com))
                CoUninitialize();
        });
    }
    networkCondition_.notify_one();
}
void ApplicationServices::QueueWebMetadata(std::string id, bool needTitle) {
    if (!data_ || !data_->paths || remoteRequested_.contains(id))
        return;
    std::string url;
    if (const auto custom = data_->config.customTools.find(id); custom != data_->config.customTools.end()) {
        if (custom->second.kind != ShortcutKind::Url)
            return;
        url = custom->second.target;
    } else if (const auto *tool = data_->registry.FindTool(id)) {
        if (!tool->manifest.icon.empty() ||
            (tool->manifest.type != ToolType::Web && tool->manifest.type != ToolType::ExternalLink))
            return;
        url = tool->manifest.launch.url;
    } else
        return;
    remoteRequested_.insert(id);
    EnqueueNetwork([this, id, url, needTitle, cache = data_->paths->IconCacheDirectory(),
                    targetPx = iconTargetPx_](std::stop_token stop) {
        auto result = WebMetadataProvider(cache, httpClient_).Fetch(id, url, needTitle, stop, targetPx);
        if (!result || stop.stop_requested())
            return;
        Complete(
            [this, id, url, targetPx, result = std::move(*result)] {
                if (!HasTool(id))
                    return;
                if (result.icon) {
                    // Invalidate any older disk read still pending on the local worker.
                    iconsRequested_[id] = ++nextIconTicket_;
                    if (targetPx == iconTargetPx_)
                        data_->icons[id] = std::make_shared<IconPixels>(std::move(*result.icon));
                    else {
                        // Decode the original newly cached bytes at the latest DPI, never resize an old display bitmap.
                        iconsRequested_.erase(id);
                        RequestIcon(id);
                    }
                }
                if (!result.title.empty()) {
                    ApplyWebsiteTitle(id, url, std::move(result.title));
                }
            },
            id);
    });
}
} // namespace poetoolbox
