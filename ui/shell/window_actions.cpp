#include "dialogs.h"
#include "main_window.h"
#include "utf.h"
#include "shortcut_dialog.h"
#include <commctrl.h>
#include <algorithm>
namespace poetoolbox::ui {
void MainWindow::HideToolTooltip() {
    if (tooltip_) {
        SendMessageW(tooltip_, TTM_POP, 0, 0);
        SendMessageW(tooltip_, TTM_ACTIVATE, FALSE, 0);
    }
    toolTooltip_.clear();
}
void MainWindow::UpdateToolTooltip(D2D1_POINT_2F point) {
    if (!tooltip_)
        return;
    const auto content = page_.Tooltip(point);
    if (!content) {
        if (!toolTooltip_.empty())
            HideToolTooltip();
        return;
    }
    const float scale = dpi_ / 96;
    const RECT rect{static_cast<LONG>(content->rect.left * scale), static_cast<LONG>(content->rect.top * scale),
                    static_cast<LONG>(content->rect.right * scale), static_cast<LONG>(content->rect.bottom * scale)};
    if (toolTooltip_ == content->text && EqualRect(&tooltipRect_, &rect))
        return;
    SendMessageW(tooltip_, TTM_POP, 0, 0);
    toolTooltip_ = content->text;
    tooltipRect_ = rect;
    TOOLINFOW tip{sizeof(tip)};
    tip.hwnd = window_;
    tip.uId = 1;
    tip.rect = rect;
    tip.lpszText = toolTooltip_.data();
    SendMessageW(tooltip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tip));
    SendMessageW(tooltip_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tip));
    SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, static_cast<LPARAM>(320 * scale));
    SendMessageW(tooltip_, TTM_ACTIVATE, TRUE, 0);
}
void MainWindow::RefreshContent(bool resetScroll) {
    page_.Refresh(resetScroll);
    controlsUpdating_ = true;
    const auto cue = Utf16(services_.Tr("search.placeholder"));
    SendMessageW(search_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cue.c_str()));
    SetWindowTextW(window_, (L"ExileKit — " + page_.Title()).c_str());
    HideToolTooltip();
    controlsUpdating_ = false;
    Layout();
    UpdateScrollBar();
    RequestVisibleIcons();
    InvalidateRect(window_, nullptr, FALSE);
}
void MainWindow::RequestVisibleIcons() {
    for (const auto &id : page_.VisibleToolIds())
        services_.RequestIcon(id, userEngaged_);
}
void MainWindow::ApplyCompletions() {
    const auto changes = services_.Drain();
    if (changes.full) {
        if (const auto *data = services_.Data(); data && !loggerReady_) {
            if (data->paths)
                logger_.Initialize(data->paths->LogsDirectory());
            loggerReady_ = true;
            for (const auto &error : data->registry.Diagnostics())
                logger_.Write(LogLevel::Warning, Utf16(error.message));
            SetPropW(window_, L"POEToolbox.RegistryReady", reinterpret_cast<HANDLE>(1));
        }
        if (services_.LastError())
            logger_.Write(LogLevel::Warning, Utf16(services_.LastError()->message));
        RefreshContent();
    }
    for (const auto &id : changes.icons) {
        page_.UpdateIcon(id);
        for (const auto &r : page_.IconRects(id)) {
            const float scale = dpi_ / 96;
            RECT rect{static_cast<LONG>(r.left * scale), static_cast<LONG>(r.top * scale),
                      static_cast<LONG>(r.right * scale + 1), static_cast<LONG>(r.bottom * scale + 1)};
            if (rect.bottom > rect.top)
                InvalidateRect(window_, &rect, FALSE);
        }
    }
}
void MainWindow::HandleAction(PageAction action) {
    if (action.kind == PageActionKind::None)
        return;
    const auto *data = services_.Data();
    if (!data)
        return;
    HideToolTooltip();
    switch (action.kind) {
    case PageActionKind::AddShortcut: {
        const auto value = ShowAddShortcutDialog(window_, services_);
        if (value)
            services_.AddCustomShortcut(*value);
        else if (value.error().code != ErrorCode::Cancelled)
            services_.ReportError(value.error());
        break;
    }
    case PageActionKind::AddHome:
        services_.AddToHome(action.id);
        break;
    case PageActionKind::PinHome:
        services_.PinToHome(action.id);
        break;
    case PageActionKind::Unpin:
        services_.Unpin(action.id);
        break;
    case PageActionKind::RemoveHome:
        services_.RemoveFromHome(action.id);
        break;
    case PageActionKind::AutoRemove7:
        services_.SetHomeAutoRemoval(7);
        break;
    case PageActionKind::AutoRemove30:
        services_.SetHomeAutoRemoval(30);
        break;
    case PageActionKind::AutoRemove90:
        services_.SetHomeAutoRemoval(90);
        break;
    case PageActionKind::AutoRemoveNever:
        services_.SetHomeAutoRemoval(0);
        break;
    case PageActionKind::ContextMenu: {
        POINT point{};
        GetCursorPos(&point);
        ShowToolMenu(action.id, point);
        return;
    }
    case PageActionKind::Favorite:
        services_.ToggleFavorite(action.id);
        break;
    case PageActionKind::LanguageEnglish:
        services_.SetLanguage("en-US");
        break;
    case PageActionKind::LanguageChinese:
        services_.SetLanguage("zh-CN");
        break;
    case PageActionKind::Open: {
        const auto *tool = data->registry.FindTool(action.id);
        if (!tool) {
            if (data->config.customTools.contains(action.id))
                services_.Launch(action.id, false);
            break;
        }
        if (tool->manifest.type == ToolType::Application && !data->installed.contains(action.id) &&
            tool->manifest.distribution != DistributionType::Managed) {
            HandleAction({PageActionKind::Locate, action.id});
            return;
        }
        if (tool->manifest.distribution == DistributionType::Managed) {
            services_.ReportError({ErrorCode::UnsupportedOperation, "Package management is not available."});
            break;
        }
        bool accepted = false;
        if (tool->manifest.riskLevel == RiskLevel::GameModifying) {
            const auto message = Utf16(tool->manifest.name) + L"\n\n" + Utf16(services_.Tr("risk.confirm"));
            const auto title = Utf16(services_.Tr("risk.title"));
            accepted = MessageBoxW(window_, message.c_str(), title.c_str(),
                                   MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) == IDOK;
            if (!accepted)
                break;
        }
        services_.Launch(action.id, accepted);
        break;
    }
    case PageActionKind::Locate: {
        auto path = PickExecutable(window_, Utf16(services_.Tr("action.locate")));
        if (path)
            services_.ConfigureExecutable(action.id, *path);
        else if (path.error().code != ErrorCode::Cancelled)
            services_.ReportError(path.error());
        break;
    }
    case PageActionKind::Download:
        services_.OpenDownloadPage(action.id);
        break;
    case PageActionKind::ChangeDirectory: {
        auto path = PickDirectory(window_, Utf16(services_.Tr("settings.managedDirectory")),
                                  data->config.managedToolsDirectory);
        if (path)
            services_.SetManagedDirectory(*path);
        else if (path.error().code != ErrorCode::Cancelled)
            services_.ReportError(path.error());
        break;
    }
    case PageActionKind::ResetDirectory:
        if (data->paths)
            services_.SetManagedDirectory(data->paths->DefaultManagedToolsDirectory());
        break;
    default:
        break;
    }
    RefreshContent();
}
void MainWindow::ShowToolMenu(const std::string &id, POINT point) {
    const auto *data = services_.Data();
    if (!data)
        return;
    const auto *tool = data->registry.FindTool(id);
    if (!tool && !data->config.customTools.contains(id))
        return;
    const auto menu = CreatePopupMenu();
    if (!menu)
        return;
    std::vector<PageActionKind> actions;
    auto add = [&](PageActionKind action, const char *key) {
        actions.push_back(action);
        const auto title = Utf16(services_.Tr(key));
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(actions.size()), title.c_str());
    };
    add(PageActionKind::Open, "action.open");
    const auto entry = data->config.home.find(id);
    const bool pinned = entry != data->config.home.end() && entry->second.pinned && !entry->second.hiddenFromHome;
    const auto homeIds = services_.HomeIds();
    const bool inHome = std::find(homeIds.begin(), homeIds.end(), id) != homeIds.end();
    if (!inHome)
        add(PageActionKind::AddHome, "home.add");
    add(pinned ? PageActionKind::Unpin : PageActionKind::PinHome, pinned ? "home.unpin" : "home.pin");
    if (inHome)
        add(PageActionKind::RemoveHome, "home.remove");
    if (tool) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        add(PageActionKind::Favorite, data->config.favorites.contains(id) ? "favorite.remove" : "favorite.add");
        if (tool->manifest.type == ToolType::Application) {
            add(PageActionKind::Locate, "action.locateShort");
            if (!tool->manifest.homepage.empty() || !tool->manifest.downloadPage.empty())
                add(PageActionKind::Download, "action.download");
        }
    }
    const auto command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, window_, nullptr);
    DestroyMenu(menu);
    if (command > 0 && command <= actions.size())
        HandleAction({actions[command - 1], id});
}
} // namespace poetoolbox::ui
