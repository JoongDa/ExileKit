#include "application.h"
#include <chrono>
namespace poetoolbox {
namespace {
std::int64_t Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
} // namespace
bool ApplicationServices::HasTool(std::string_view id) const {
    return data_ && (data_->registry.FindTool(id) || data_->config.customTools.contains(id));
}
std::vector<std::string> ApplicationServices::HomeIds() const {
    if (!data_)
        return {};
    auto ids = HomeService::Visible(data_->config, Now());
    std::erase_if(ids, [this](const std::string &id) { return !HasTool(id); });
    return ids;
}
void ApplicationServices::RefreshHome() {
    if (!data_)
        return;
    HomeService::Evaluate(data_->config, Now());
    SaveConfig();
}
void ApplicationServices::AddToHome(std::string id) {
    if (!HasTool(id))
        return;
    HomeService::Add(data_->config, id, Now());
    SaveConfig();
}
void ApplicationServices::PinToHome(std::string id) {
    if (!HasTool(id))
        return;
    HomeService::Pin(data_->config, id, Now());
    SaveConfig();
}
void ApplicationServices::Unpin(std::string id) {
    if (!HasTool(id))
        return;
    HomeService::Unpin(data_->config, id);
    HomeService::Evaluate(data_->config, Now());
    SaveConfig();
}
void ApplicationServices::RemoveFromHome(std::string id) {
    if (!HasTool(id))
        return;
    HomeService::Remove(data_->config, id);
    SaveConfig();
}
void ApplicationServices::SetHomeAutoRemoval(int days) {
    if (!data_ || (days != 0 && days != 7 && days != 30 && days != 90))
        return;
    data_->config.homeAutoRemoveDays = days;
    RefreshHome();
}
void ApplicationServices::FinishLaunch(std::string id, Status result) {
    if (!result) {
        if (result.error().code == ErrorCode::ExecutableNotFound)
            data_->installed.erase(id);
        ReportError(result.error());
        return;
    }
    HomeService::RecordSuccessfulLaunch(data_->config, id, Now());
    statusKey_ = "status.opened";
    error_.reset();
    SaveConfig();
}
} // namespace poetoolbox
