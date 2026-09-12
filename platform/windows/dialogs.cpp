#include "dialogs.h"
#include <shobjidl.h>
#include <wrl/client.h>
namespace poetoolbox {
namespace {
Result<std::filesystem::path> Pick(HWND owner, std::wstring_view title, const std::filesystem::path &current,
                                   bool directory, bool shortcuts = false) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr))
        return std::unexpected(
            Error{ErrorCode::IoError, "Cannot open the Windows file picker.", static_cast<std::uint32_t>(hr)});
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR |
                       (directory ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
    const std::wstring ownedTitle(title);
    dialog->SetTitle(ownedTitle.c_str());
    if (!directory) {
        const auto filterLabel = ownedTitle + (shortcuts ? L" (*.exe; *.lnk)" : L" (*.exe)");
        const COMDLG_FILTERSPEC filter[] = {{filterLabel.c_str(), shortcuts ? L"*.exe;*.lnk" : L"*.exe"}};
        dialog->SetFileTypes(1, filter);
        if (shortcuts)
            dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR |
                               FOS_FILEMUSTEXIST | FOS_NODEREFERENCELINKS);
    }
    if (!current.empty()) {
        Microsoft::WRL::ComPtr<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(current.c_str(), nullptr, IID_PPV_ARGS(&folder))))
            dialog->SetFolder(folder.Get());
    }
    hr = dialog->Show(owner);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return std::unexpected(Error{ErrorCode::Cancelled, "Selection cancelled."});
    if (FAILED(hr))
        return std::unexpected(
            Error{ErrorCode::IoError, "The Windows file picker failed.", static_cast<std::uint32_t>(hr)});
    Microsoft::WRL::ComPtr<IShellItem> item;
    if (FAILED(hr = dialog->GetResult(&item)))
        return std::unexpected(
            Error{ErrorCode::IoError, "Cannot read the selected path.", static_cast<std::uint32_t>(hr)});
    PWSTR path = nullptr;
    if (FAILED(hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
        return std::unexpected(
            Error{ErrorCode::IoError, "Cannot read the selected path.", static_cast<std::uint32_t>(hr)});
    std::filesystem::path result(path);
    CoTaskMemFree(path);
    if (!directory && _wcsicmp(result.extension().c_str(), L".exe") != 0 &&
        !(shortcuts && _wcsicmp(result.extension().c_str(), L".lnk") == 0))
        return std::unexpected(Error{ErrorCode::InvalidPath, "Select a Windows .exe application."});
    return result;
}
} // namespace
Result<std::filesystem::path> PickExecutable(HWND owner, std::wstring_view title) {
    return Pick(owner, title, {}, false);
}
Result<std::filesystem::path> PickShortcut(HWND owner, std::wstring_view title) {
    return Pick(owner, title, {}, false, true);
}
Result<std::filesystem::path> PickDirectory(HWND owner, std::wstring_view title, const std::filesystem::path &current) {
    return Pick(owner, title, current, true);
}
} // namespace poetoolbox
